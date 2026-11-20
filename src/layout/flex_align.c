#include "layout/internal.h"

/* count is at least 1 wherever this is called. AROUND and EVENLY divide by
 * count or count + 1 so the halves land on both edges even for one item. */
static line_spacing justify_spacing(cf_justify mode, uint32_t count, float extra) {
    const float boxes = (float)count;
    line_spacing spacing = {0.0f, 0.0f};

    switch (mode) {
    case CF_JUSTIFY_START:
        break;
    case CF_JUSTIFY_CENTER:
        spacing.before = extra * 0.5f;
        break;
    case CF_JUSTIFY_END:
        spacing.before = extra;
        break;
    case CF_JUSTIFY_SPACE_BETWEEN:
        if (count > 1u) {
            spacing.between = extra / (boxes - 1.0f);
        }
        break;
    case CF_JUSTIFY_SPACE_AROUND:
        spacing.between = extra / boxes;
        spacing.before = spacing.between * 0.5f;
        break;
    case CF_JUSTIFY_SPACE_EVENLY:
        spacing.between = extra / (boxes + 1.0f);
        spacing.before = spacing.between;
        break;
    }

    return spacing;
}

/* §9.6.2: how the leftover cross space falls between lines. Stretch grows the
 * lines themselves instead, so it never reaches here. */
static line_spacing content_spacing(cf_align_content mode, uint32_t count, float extra) {
    const float boxes = (float)count;
    line_spacing spacing = {0.0f, 0.0f};

    switch (mode) {
    case CF_ALIGN_CONTENT_START:
        break;
    case CF_ALIGN_CONTENT_CENTER:
        spacing.before = extra * 0.5f;
        break;
    case CF_ALIGN_CONTENT_END:
        spacing.before = extra;
        break;
    case CF_ALIGN_CONTENT_SPACE_BETWEEN:
        if (count > 1u) {
            spacing.between = extra / (boxes - 1.0f);
        }
        break;
    case CF_ALIGN_CONTENT_SPACE_AROUND:
        spacing.between = extra / boxes;
        spacing.before = spacing.between * 0.5f;
        break;
    case CF_ALIGN_CONTENT_SPACE_EVENLY:
        spacing.between = extra / (boxes + 1.0f);
        spacing.before = spacing.between;
        break;
    case CF_ALIGN_CONTENT_AUTO:
    case CF_ALIGN_CONTENT_STRETCH:
        break;
    }

    return spacing;
}

static void place_lines(const flex_context *f, flex_line *lines, uint32_t line_count) {
    /* A container that cannot wrap has nothing to distribute between lines:
     * align-content behaves as flex-start there. */
    cf_align_content mode = f->style->align_content;
    if (mode == CF_ALIGN_CONTENT_AUTO) {
        mode = CF_ALIGN_CONTENT_STRETCH;
    }
    if (line_count == 1u || f->style->wrap == CF_NO_WRAP) {
        mode = CF_ALIGN_CONTENT_START;
    }

    float used = f->gap_cross * (float)(line_count > 1u ? line_count - 1u : 0u);
    for (uint32_t l = 0; l < line_count; ++l) {
        used += lines[l].cross;
    }

    const float extra = clamp_non_negative(f->cross_size - used);
    line_spacing spacing = {0.0f, 0.0f};

    if (mode == CF_ALIGN_CONTENT_STRETCH) {
        const float grow = extra / (float)line_count;
        for (uint32_t l = 0; l < line_count; ++l) {
            lines[l].cross += grow;
        }
    } else {
        spacing = content_spacing(mode, line_count, extra);
    }

    float cursor = spacing.before;
    for (uint32_t l = 0; l < line_count; ++l) {
        lines[l].offset_cross = cursor;
        cursor += lines[l].cross + f->gap_cross + spacing.between;
    }

    /* §9.4: wrap-reverse stacks the lines from the cross end, so every offset
     * measured from cross-start has its distance read off the far edge. */
    if (f->cross_reverse) {
        const float stack = f->cross_definite ? f->cross_size : used;
        for (uint32_t l = 0; l < line_count; ++l) {
            lines[l].offset_cross = stack - lines[l].offset_cross - lines[l].cross;
        }
    }
}

/* §9.6.3: an item's own alignment wins. Auto on the item means whatever the
 * container does, and auto on the container means stretch. */
static cf_align align_of(const flex_context *f, const flex_item *item) {
    const cf_align self = f->ctx->tree->styles[item->slot].align_self;

    if (self != CF_ALIGN_AUTO) {
        return self;
    }

    if (f->style->align_items != CF_ALIGN_AUTO) {
        return f->style->align_items;
    }

    return CF_ALIGN_STRETCH;
}

/*
 * Where an item sits across its line. Baseline alignment needs a first-baseline
 * measurement the text layer has not reported yet, so it behaves as start.
 */
static float cross_offset(const flex_context *f, const flex_line *line, flex_item *item) {
    const float extra = clamp_non_negative(line->cross - item_outer_cross(item));
    const uint32_t auto_margins =
        (item->auto_margin_cross_start ? 1u : 0u) + (item->auto_margin_cross_end ? 1u : 0u);

    /* Auto margins claim the space before alignment sees it (§9.6.3). */
    if (auto_margins > 0u) {
        return item->margin_cross_start + extra / (float)auto_margins;
    }

    const cf_align alignment = align_of(f, item);

    if (alignment == CF_ALIGN_STRETCH && item->cross_is_auto) {
        item->cross = clamp_range(line->cross - item->margin_cross_start - item->margin_cross_end,
                                  item->min_cross, item->max_cross);
        return item->margin_cross_start;
    }

    if (alignment == CF_ALIGN_CENTER) {
        return item->margin_cross_start + extra * 0.5f;
    }

    if (alignment == CF_ALIGN_END) {
        return item->margin_cross_start + extra;
    }

    return item->margin_cross_start;
}

/* §9.5 through §9.6: where every item lands, then what it does with the box it
 * was given. */
void arrange_items(const layout_ctx *ctx, const flex_context *f, flex_item *items, flex_line *lines,
                   uint32_t line_count) {
    place_lines(f, lines, line_count);

    for (uint32_t l = 0; l < line_count; ++l) {
        flex_line *line = &lines[l];
        const uint32_t end = line->first + line->count;

        /* §9.5.2: auto margins on the main axis claim the leftover before
         * justify-content sees any, so a line holding one gets no spacing. */
        uint32_t auto_margins = 0u;
        for (uint32_t i = line->first; i < end; ++i) {
            auto_margins += (items[i].auto_margin_main_start ? 1u : 0u) +
                            (items[i].auto_margin_main_end ? 1u : 0u);
        }

        const float extra = clamp_non_negative(f->main_size - line_outer_main(f, items, line));
        const line_spacing justify =
            auto_margins > 0u ? (line_spacing){0.0f, 0.0f}
                              : justify_spacing(f->style->justify_content, line->count, extra);
        const float auto_share = auto_margins > 0u ? extra / (float)auto_margins : 0.0f;

        float cursor = justify.before;
        for (uint32_t i = line->first; i < end; ++i) {
            flex_item *item = &items[i];

            const float main_start =
                item->auto_margin_main_start ? auto_share : item->margin_main_start;
            const float main_end = item->auto_margin_main_end ? auto_share : item->margin_main_end;

            /* Distances are counted from main-start; a reversed direction
             * reads the same distance from the far edge inwards. */
            const float logical = cursor + main_start;
            const float main_position = f->reverse ? f->main_size - logical - item->main : logical;

            /* A reversed wrap counts across the line the same way, so the box
             * lands against the other side of the band its line was given. */
            const float within = cross_offset(f, line, item);
            const float cross_position =
                f->cross_reverse ? line->offset_cross + line->cross - within - item->cross
                                 : line->offset_cross + within;

            const cf_point origin = f->row ? (cf_point){.x = f->origin.x + main_position,
                                                        .y = f->origin.y + cross_position}
                                           : (cf_point){.x = f->origin.x + cross_position,
                                                        .y = f->origin.y + main_position};

            const size_t child_mark = cf_arena_mark(ctx->scratch);
            layout_subtree(ctx, item->slot, f->constraint,
                           verdict_for(f, CF_PX(item->main), CF_PX(item->cross)), origin,
                           f->containing);
            cf_arena_rewind(ctx->scratch, child_mark);

            cursor = logical + item->main + main_end + f->gap_main + justify.between;
        }
    }
}
