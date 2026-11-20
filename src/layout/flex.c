#include "layout/internal.h"

static cf_length main_axis(const flex_context *f, cf_available box) {
    return f->row ? box.width : box.height;
}

cf_available verdict_for(const flex_context *f, cf_length main, cf_length cross) {
    return f->row ? (cf_available){.width = main, .height = cross}
                  : (cf_available){.width = cross, .height = main};
}

static cf_size size_from_extents(const flex_context *f, float main, float cross) {
    return f->row ? (cf_size){.width = main, .height = cross}
                  : (cf_size){.width = cross, .height = main};
}

float item_outer_main(const flex_item *item) {
    return item->main + item->margin_main_start + item->margin_main_end;
}

float item_outer_cross(const flex_item *item) {
    return item->cross + item->margin_cross_start + item->margin_cross_end;
}

float line_outer_main(const flex_context *f, const flex_item *items, const flex_line *line) {
    float total = f->gap_main * (float)(line->count > 1u ? line->count - 1u : 0u);

    for (uint32_t i = line->first; i < line->first + line->count; ++i) {
        total += item_outer_main(&items[i]);
    }

    return total;
}

static flex_context context_of(const layout_ctx *ctx, uint32_t slot, const flex_pass *pass) {
    const cf_style *style = &ctx->tree->styles[slot];

    flex_context f = {
        .ctx = ctx,
        .style = style,
        .slot = slot,
        .constraint = pass->constraint,
        .origin = pass->origin,
        .containing = pass->containing,
        .row = cf_direction_is_row(style->direction),
        .reverse = cf_direction_is_reverse(style->direction),
        .measuring = pass->mode == FLEX_MEASURE,
        .intrinsic = pass->mode == FLEX_MEASURE ? pass->intrinsic : CF_INTRINSIC_MAX_CONTENT,
    };

    f.main_offer = main_axis(&f, pass->constraint);
    f.cross_offer = f.row ? pass->constraint.height : pass->constraint.width;
    f.main_definite = definite_offer(f.main_offer, &f.main_size);
    f.cross_definite = definite_offer(f.cross_offer, &f.cross_size);

    /* Margins, padding and gaps written as percentages all resolve against the
     * container's inline size, which is its width in either direction. */
    definite_offer(pass->constraint.width, &f.percent_base);

    f.gap_main = cf_length_resolve(f.row ? style->column_gap : style->row_gap, f.percent_base);
    f.gap_cross = cf_length_resolve(f.row ? style->row_gap : style->column_gap, f.percent_base);

    return f;
}

/*
 * The line an out-of-flow child of this container would have filled had it
 * stayed in the flow: one box, packed by the container's own keywords across
 * the content box it was given.
 */
static absolute_flow sole_line(const flex_context *f) {
    return (absolute_flow){
        .origin = f->origin,
        .content = f->constraint,
        .main = f->style->justify_content,
        .cross = f->style->align_items,
        .row = f->row,
        .main_reverse = f->reverse,
        .cross_reverse = f->style->wrap == CF_WRAP_REVERSE,
    };
}

/*
 * The pass both entries share: §9.2.1 through §9.7 over slot's children. The
 * content extent the children ask for comes back either way; arranging has
 * already placed them by then.
 */
static cf_size flex_resolve(const flex_context *f) {
    const layout_ctx *ctx = f->ctx;
    const uint32_t capacity = child_count(ctx, f->slot);
    if (capacity == 0u) {
        return (cf_size){0};
    }

    const size_t mark = cf_arena_mark(ctx->scratch);
    flex_item *items = cf_alloc(ctx->scratch, (size_t)capacity * sizeof *items);
    flex_line *lines =
        items != NULL ? cf_alloc(ctx->scratch, (size_t)capacity * sizeof *lines) : NULL;

    if (items == NULL || lines == NULL) {
        cf_logf(CF_LOG_LAYOUT, "layout: scratch refused a container with %u children", capacity);
        cf_arena_rewind(ctx->scratch, mark);
        return (cf_size){0};
    }

    const uint32_t count = build_items(f, items, capacity);

    uint32_t line_count = 0u;
    collect_lines(f, items, count, lines, &line_count);

    /* Growing and shrinking distribute the space a container was given. A box
     * being measured has none: it reports what its content asks for and the
     * caller caps that afterwards. */
    if (!f->measuring) {
        for (uint32_t l = 0; l < line_count; ++l) {
            grow_line(f, items, &lines[l]);
            shrink_line(f, items, &lines[l]);
        }
    }

    remeasure_item_cross(f, items, count);
    collect_line_cross(f, items, lines, line_count);

    cf_size extents = {0.0f, 0.0f};

    if (f->measuring) {
        float main_extent = 0.0f;
        float cross_extent = 0.0f;

        for (uint32_t l = 0; l < line_count; ++l) {
            main_extent = larger(main_extent, lines[l].outer_main);
            cross_extent += lines[l].cross;
        }

        /* Max-content asks what the items want together, not what the widest
         * line of a wrapped container becomes: a container that breaks its line
         * at the width it was offered still claims the whole main axis for its
         * own size, and packs that width's worth of lines across it. */
        if (f->intrinsic == CF_INTRINSIC_MAX_CONTENT) {
            const flex_line unbroken = {.first = 0u, .count = count};
            main_extent = line_outer_main(f, items, &unbroken);
        }

        cross_extent += f->gap_cross * (float)(line_count > 1u ? line_count - 1u : 0u);
        extents = size_from_extents(f, main_extent, cross_extent);
    } else {
        const absolute_flow flow = sole_line(f);
        arrange_items(ctx, f, items, lines, line_count);
        place_absolute(ctx, f->slot, f->containing, &flow);
    }

    cf_arena_rewind(ctx->scratch, mark);

    return extents;
}

cf_size flex_extent_of(const layout_ctx *ctx, uint32_t slot, const flex_pass *pass) {
    const flex_context f = context_of(ctx, slot, pass);

    return flex_resolve(&f);
}

void flex_arrange(const layout_ctx *ctx, uint32_t slot, const flex_pass *pass) {
    const flex_context f = context_of(ctx, slot, pass);

    flex_resolve(&f);
}
