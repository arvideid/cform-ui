#include <float.h>

#include "layout/internal.h"

/*
 * A line whose hypothetical main size exceeds the container's by less than this
 * is a float artifact rather than a real overflow, and breaking it would move
 * an item onto a second line the declaration never asked for.
 */
#define MAIN_FIT_TOLERANCE 0.01f

/*
 * §9.2.1 through §9.7, one entry per child. Lengths that still carry a unit
 * (`main_min` above all) survive because resolving them early would either drop
 * the automatic minimum size on the floor or resolve a percentage against a
 * container that has no size yet.
 */
typedef struct flex_item {
    uint32_t slot;
    float min_main;
    float max_main;
    float min_cross;
    float max_cross;
    float basis;
    float hypothetical_main;
    float main;
    float cross;
    float margin_main_start;
    float margin_main_end;
    float margin_cross_start;
    float margin_cross_end;
    float grow;
    float shrink;
    bool min_is_auto;
    bool cross_is_auto;
    bool auto_margin_main_start;
    bool auto_margin_main_end;
    bool auto_margin_cross_start;
    bool auto_margin_cross_end;
    bool frozen;
} flex_item;

/* A run of consecutive items, packed along the main axis. */
typedef struct flex_line {
    uint32_t first;
    uint32_t count;
    float outer_main;
    float cross;
    float offset_main;
    float offset_cross;
} flex_line;

/*
 * One container seen from its own axes. `main` and `cross` are the layout
 * axes, `row` says which of width and height each is, and `reverse` says which
 * physical edge main-start is.
 */
typedef struct flex_context {
    const layout_ctx *ctx;
    const cf_style *style;
    cf_available constraint;
    cf_point origin;
    cf_length main_offer;
    cf_length cross_offer;
    cf_intrinsic intrinsic;
    float main_size;
    float cross_size;
    float gap_main;
    float gap_cross;
    float percent_base;
    bool main_definite;
    bool cross_definite;
    bool row;
    bool reverse;
    bool measuring;
} flex_context;

static cf_length main_axis(const flex_context *f, cf_available box) {
    return f->row ? box.width : box.height;
}

static cf_available verdict_for(const flex_context *f, cf_length main, cf_length cross) {
    return f->row ? (cf_available){.width = main, .height = cross}
                  : (cf_available){.width = cross, .height = main};
}

static void axes_of_size(const flex_context *f, cf_size box, float *main, float *cross) {
    *main = f->row ? box.width : box.height;
    *cross = f->row ? box.height : box.width;
}

static cf_size size_from_extents(const flex_context *f, float main, float cross) {
    return f->row ? (cf_size){.width = main, .height = cross}
                  : (cf_size){.width = cross, .height = main};
}

static float larger(float first, float second) {
    return first > second ? first : second;
}

/* A float forced inside its bounds. The floor keeps the last word when the
 * pair contradicts, so a misconfigured item still lands somewhere. */
static float clamp_range(float value, float floor_value, float ceiling) {
    value = clamp_non_negative(value);

    if (value > ceiling) {
        value = ceiling;
    }

    return value < floor_value ? floor_value : value;
}

/*
 * A bound as a float. An absent bound is 0 below and unlimited above: a
 * percentage against a container that is still being measured bounds nothing,
 * and treating it as zero would collapse every box it applied to.
 */
static float resolve_bound(cf_length bound, cf_length offer, float absent) {
    float value;
    if (!cf_length_of(bound, offer, &value)) {
        return absent;
    }

    return clamp_non_negative(value);
}

static float item_outer_main(const flex_item *item) {
    return item->main + item->margin_main_start + item->margin_main_end;
}

static float item_outer_cross(const flex_item *item) {
    return item->cross + item->margin_cross_start + item->margin_cross_end;
}

static float line_outer_main(const flex_context *f, const flex_item *items, const flex_line *line) {
    float total = f->gap_main * (float)(line->count > 1u ? line->count - 1u : 0u);

    for (uint32_t i = line->first; i < line->first + line->count; ++i) {
        total += item_outer_main(&items[i]);
    }

    return total;
}

/*
 * §9.7 restarts from the sizes a round begins with: an item already frozen
 * contributes the size it was clamped to, one still flexing contributes its
 * hypothetical size rather than the target the previous round handed it.
 */
static float line_flexing_outer_main(const flex_context *f, const flex_item *items,
                                     const flex_line *line) {
    float total = f->gap_main * (float)(line->count > 1u ? line->count - 1u : 0u);

    for (uint32_t i = line->first; i < line->first + line->count; ++i) {
        const float main = items[i].frozen ? items[i].main : items[i].hypothetical_main;
        total += main + items[i].margin_main_start + items[i].margin_main_end;
    }

    return total;
}

static flex_context context_of(const layout_ctx *ctx, uint32_t slot, const flex_pass *pass) {
    const cf_style *style = &ctx->tree->styles[slot];

    flex_context f = {
        .ctx = ctx,
        .style = style,
        .constraint = pass->constraint,
        .origin = pass->origin,
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
 * §9.3: reversing a direction moves which physical edge is main-start. The
 * cross axis keeps packing from the top in a row and from the left in a column,
 * because reversing the main axis does not flip the block or inline direction.
 */
static void assign_margins(const flex_context *f, const cf_box *margin, flex_item *item) {
    const cf_length main_start = f->row ? (f->reverse ? margin->right : margin->left)
                                        : (f->reverse ? margin->bottom : margin->top);
    const cf_length main_end = f->row ? (f->reverse ? margin->left : margin->right)
                                      : (f->reverse ? margin->top : margin->bottom);
    const cf_length cross_start = f->row ? margin->top : margin->left;
    const cf_length cross_end = f->row ? margin->bottom : margin->right;

    const cf_length sides[4] = {main_start, main_end, cross_start, cross_end};
    float *resolved[4] = {&item->margin_main_start, &item->margin_main_end,
                          &item->margin_cross_start, &item->margin_cross_end};
    bool *is_auto[4] = {&item->auto_margin_main_start, &item->auto_margin_main_end,
                        &item->auto_margin_cross_start, &item->auto_margin_cross_end};

    for (uint32_t side = 0; side < 4u; ++side) {
        *is_auto[side] = cf_length_is_auto(sides[side]);
        /* Auto reads as zero here, which is what a line's hypothetical outer
         * size needs; §9.5.2 gives the side its share later. */
        *resolved[side] = cf_length_resolve(sides[side], f->percent_base);
    }
}

static void measure_item(const flex_context *f, flex_item *item) {
    const cf_style *child = &f->ctx->tree->styles[item->slot];

    const cf_length main_declared = f->row ? child->width : child->height;
    const cf_length cross_declared = f->row ? child->height : child->width;
    const cf_length main_min = f->row ? child->min_width : child->min_height;
    const cf_length main_max = f->row ? child->max_width : child->max_height;
    const cf_length cross_min = f->row ? child->min_height : child->min_width;
    const cf_length cross_max = f->row ? child->max_height : child->max_width;

    item->min_main = resolve_bound(main_min, f->main_offer, 0.0f);
    item->max_main = resolve_bound(main_max, f->main_offer, FLT_MAX);
    item->min_cross = resolve_bound(cross_min, f->cross_offer, 0.0f);
    item->max_cross = resolve_bound(cross_max, f->cross_offer, FLT_MAX);
    item->min_is_auto = cf_length_is_auto(main_min);
    item->grow = child->flex_grow;
    item->shrink = child->flex_shrink;

    /* §9.2.1 steps 3-5: flex-basis, then the declared main size, then the
     * content. Nothing declared means nothing measured yet either. */
    float declared;
    if (cf_length_of(child->flex_basis, f->main_offer, &declared) ||
        cf_length_of(main_declared, f->main_offer, &declared)) {
        item->basis = clamp_non_negative(declared);
    } else {
        float content_main;
        float content_cross;
        axes_of_size(f,
                     border_size_of(f->ctx, item->slot, f->intrinsic, f->constraint,
                                    verdict_for(f, CF_AUTO, CF_AUTO)),
                     &content_main, &content_cross);
        item->basis = content_main;
        item->cross = content_cross;
    }

    /* The automatic minimum size cannot exceed the base size it is compared
     * against, so clamping here without resolving it loses nothing. */
    item->hypothetical_main = clamp_range(item->basis, item->min_main, item->max_main);
    item->main = item->hypothetical_main;

    float cross_settled;
    item->cross_is_auto = !cf_length_of(cross_declared, f->cross_offer, &cross_settled);

    if (!item->cross_is_auto) {
        item->cross = clamp_range(cross_settled, item->min_cross, item->max_cross);
    }
}

/*
 * The cross size of an item whose main size the pass has settled. The verdict
 * carries the main size so a wrapping callback sees the width the item will
 * actually have rather than the one it asked for.
 */
static void measure_item_cross(const flex_context *f, flex_item *item) {
    float main;
    axes_of_size(f,
                 border_size_of(f->ctx, item->slot, CF_INTRINSIC_MAX_CONTENT, f->constraint,
                                verdict_for(f, CF_PX(item->main), CF_AUTO)),
                 &main, &item->cross);

    item->cross = clamp_range(item->cross, item->min_cross, item->max_cross);
}

/* §9.1 item 4: items pack in `order` and declaration order breaks a tie. The
 * sort is stable and is skipped entirely when nothing declares an order, which
 * is every container that has never used the property. */
static void sort_by_order(const flex_context *f, flex_item *items, uint32_t count) {
    uint32_t first_with_order = 0;
    while (first_with_order < count &&
           f->ctx->tree->styles[items[first_with_order].slot].order == 0) {
        ++first_with_order;
    }

    if (first_with_order == count) {
        return;
    }

    for (uint32_t i = 1; i < count; ++i) {
        const flex_item moving = items[i];
        const int32_t moving_order = f->ctx->tree->styles[moving.slot].order;
        uint32_t tail = i;

        while (tail > 0u && f->ctx->tree->styles[items[tail - 1u].slot].order > moving_order) {
            items[tail] = items[tail - 1u];
            --tail;
        }

        items[tail] = moving;
    }
}

/*
 * §9.2.2: wrapping happens between items, never inside one. An intrinsic
 * measure is content-driven and has no main size to break against: max-content
 * keeps every item on one line and min-content gives each a line of its own,
 * which is how the narrowest a wrapped container can go is found.
 */
static void collect_lines(const flex_context *f, const flex_item *items, uint32_t count,
                          flex_line *lines, uint32_t *line_count) {
    const bool wraps = f->style->wrap != CF_NO_WRAP;
    const bool one_per_line = wraps && f->measuring && f->intrinsic == CF_INTRINSIC_MIN_CONTENT;
    const bool may_break = one_per_line || (wraps && !f->measuring);

    lines[0] = (flex_line){.first = 0u, .count = 0u, .outer_main = 0.0f};
    *line_count = 1u;

    for (uint32_t i = 0; i < count; ++i) {
        flex_line *line = &lines[*line_count - 1u];
        const float gap = line->count > 0u ? f->gap_main : 0.0f;
        const float outer = item_outer_main(&items[i]);
        const bool overflow =
            one_per_line || (!f->measuring && f->main_definite &&
                             line->outer_main + gap + outer - f->main_size > MAIN_FIT_TOLERANCE);

        if (may_break && line->count > 0u && overflow) {
            ++*line_count;
            line = &lines[*line_count - 1u];
            *line = (flex_line){.first = i, .count = 0u, .outer_main = 0.0f};
        }

        line->outer_main += gap + outer;
        line->count++;
    }
}

/*
 * §9.7 steps 1-2: grow. An item whose target breaks its maximum is frozen
 * there and the round restarts, so the room it cannot take goes to the rest.
 * Growing can only break a maximum - the base size is already at or above the
 * floor - and each round freezes at least one item, so the line's item count
 * bounds the loop.
 */
static void grow_line(const flex_context *f, flex_item *items, const flex_line *line) {
    const uint32_t end = line->first + line->count;

    for (uint32_t i = line->first; i < end; ++i) {
        items[i].frozen = items[i].grow <= 0.0f;
    }

    for (uint32_t round = 0; round < line->count; ++round) {
        const float free_space =
            clamp_non_negative(f->main_size - line_flexing_outer_main(f, items, line));

        float pool = 0.0f;
        for (uint32_t i = line->first; i < end; ++i) {
            if (!items[i].frozen) {
                pool += items[i].grow;
            }
        }

        if (free_space <= 0.0f || pool <= 0.0f) {
            return;
        }

        bool refroze = false;
        for (uint32_t i = line->first; i < end; ++i) {
            if (items[i].frozen) {
                continue;
            }

            const float target = items[i].hypothetical_main + free_space * items[i].grow / pool;
            if (target >= items[i].max_main) {
                items[i].main = items[i].max_main;
                items[i].frozen = true;
                refroze = true;
            } else {
                items[i].main = target;
            }
        }

        if (!refroze) {
            return;
        }
    }
}

/*
 * §9.9.1: with no declared minimum an item cannot be shrunk below its
 * content-based minimum, which is the smaller of its min-content contribution
 * and the size its own declaration asked for - and never below the base size
 * the pass already settled on. The answer costs a measure of the item's
 * content, so it is resolved on first violation and then kept in min_main.
 */
static float shrink_floor(const flex_context *f, flex_item *item) {
    if (!item->min_is_auto) {
        return item->min_main;
    }

    const cf_size content =
        content_extent_of(f->ctx, item->slot, CF_INTRINSIC_MIN_CONTENT, f->constraint);
    const cf_edges inset = content_insets(f->ctx, item->slot, f->percent_base);
    const float content_main = f->row ? content.width + inset.left + inset.right
                                      : content.height + inset.top + inset.bottom;

    item->min_is_auto = false;
    item->min_main = clamp_range(content_main, 0.0f, item->hypothetical_main);

    return item->min_main;
}

/*
 * §9.7 steps 3-5: shrink, weighted by flex-shrink times the base size so an
 * item with nothing to give gives nothing. An item that reaches its floor is
 * kept there while the rounds restart, so the space it cannot give goes to the
 * rest. An empty pool would leave the line overflowing even though its items
 * asked to shrink, so the shortfall is split between them instead.
 *
 * The used sizes are not re-initialised here: a line that grew has nothing to
 * shrink and returns from the first round, and rewriting the sizes would undo
 * what §9.7 steps 1-2 just decided.
 */
static void shrink_line(const flex_context *f, flex_item *items, const flex_line *line) {
    const uint32_t end = line->first + line->count;

    for (uint32_t i = line->first; i < end; ++i) {
        items[i].frozen = items[i].shrink <= 0.0f;
    }

    for (uint32_t round = 0; round < line->count; ++round) {
        float pool = 0.0f;
        uint32_t unfrozen = 0u;
        for (uint32_t i = line->first; i < end; ++i) {
            if (!items[i].frozen) {
                pool += items[i].shrink * items[i].basis;
                ++unfrozen;
            }
        }

        const float shortage = line_flexing_outer_main(f, items, line) - f->main_size;
        if (shortage <= 0.0f || unfrozen == 0u) {
            return;
        }

        /* Every base size is zero, so the weighted factors are too and the
         * shortfall has nothing to be weighted against. */
        const bool equal_split = pool <= 0.0f;

        bool refroze = false;
        for (uint32_t i = line->first; i < end; ++i) {
            if (items[i].frozen) {
                continue;
            }

            const float reduction = equal_split
                                        ? shortage / (float)unfrozen
                                        : shortage * (items[i].shrink * items[i].basis) / pool;
            const float target = items[i].hypothetical_main - reduction;

            /* The hypothetical size is already inside the maximum and shrinking
             * only moves it down, so the floor is the sole violation here - and
             * the floor keeps the last word when a min and a max contradict. */
            if (target <= shrink_floor(f, &items[i])) {
                items[i].main = items[i].min_main;
                items[i].frozen = true;
                refroze = true;
            } else {
                items[i].main = target;
            }
        }

        if (!refroze) {
            return;
        }
    }
}

/*
 * §9.2.1 steps 16-18 and §9.6.1: a line is as wide across as its tallest item.
 * An item whose main size changed since it was measured is measured again,
 * because its content can wrap differently at the size the pass settled on.
 */
static void collect_line_cross(const flex_context *f, flex_item *items, flex_line *lines,
                               uint32_t line_count) {
    for (uint32_t line_index = 0; line_index < line_count; ++line_index) {
        flex_line *line = &lines[line_index];
        line->cross = 0.0f;

        for (uint32_t i = line->first; i < line->first + line->count; ++i) {
            if (!f->measuring && items[i].cross_is_auto &&
                items[i].main != items[i].hypothetical_main) {
                measure_item_cross(f, &items[i]);
            }

            line->cross = larger(line->cross, item_outer_cross(&items[i]));
        }
    }

    /* §9.6.1: a single line in a container whose cross size is settled takes
     * that size, which is what lets align-items centre one item in a tall box.
     * An intrinsic measure has no settled cross size to take. */
    if (line_count == 1u && !f->measuring && f->cross_definite && lines[0].cross < f->cross_size) {
        lines[0].cross = f->cross_size;
    }
}

/* How much space a distribution mode leaves before the first box and between
 * each pair. */
typedef struct line_spacing {
    float before;
    float between;
} line_spacing;

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
    if (f->style->wrap == CF_WRAP_REVERSE) {
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
static void arrange_items(const layout_ctx *ctx, const flex_context *f, flex_item *items,
                          flex_line *lines, uint32_t line_count) {
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
            const float cross_position = line->offset_cross + cross_offset(f, line, item);

            const cf_point origin = f->row ? (cf_point){.x = f->origin.x + main_position,
                                                        .y = f->origin.y + cross_position}
                                           : (cf_point){.x = f->origin.x + cross_position,
                                                        .y = f->origin.y + main_position};

            const size_t child_mark = cf_arena_mark(ctx->scratch);
            layout_subtree(ctx, item->slot, f->constraint,
                           verdict_for(f, CF_PX(item->main), CF_PX(item->cross)), origin);
            cf_arena_rewind(ctx->scratch, child_mark);

            cursor = logical + item->main + main_end + f->gap_main + justify.between;
        }
    }
}

/*
 * The pass both entries share: §9.2.1 through §9.7 over slot's children. The
 * content extent the children ask for comes back either way; arranging has
 * already placed them by then.
 */
static cf_size flex_resolve(const layout_ctx *ctx, uint32_t slot, const flex_context *f) {
    const uint32_t count = child_count(ctx, slot);
    if (count == 0u) {
        return (cf_size){0};
    }

    const size_t mark = cf_arena_mark(ctx->scratch);
    flex_item *items = cf_alloc(ctx->scratch, (size_t)count * sizeof *items);
    flex_line *lines = items != NULL ? cf_alloc(ctx->scratch, (size_t)count * sizeof *lines) : NULL;

    if (items == NULL || lines == NULL) {
        cf_logf(CF_LOG_LAYOUT, "layout: scratch refused a container with %u children", count);
        cf_arena_rewind(ctx->scratch, mark);
        return (cf_size){0};
    }

    const uint32_t first = ctx->tree->child_start[slot];
    for (uint32_t i = 0; i < count; ++i) {
        flex_item *item = &items[i];
        *item = (flex_item){.slot = ctx->tree->child_slots[first + i]};
        assign_margins(f, &ctx->tree->styles[item->slot].margin, item);
        measure_item(f, item);
    }

    sort_by_order(f, items, count);

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

    collect_line_cross(f, items, lines, line_count);

    cf_size extents = {0.0f, 0.0f};

    if (f->measuring) {
        float main_extent = 0.0f;
        float cross_extent = 0.0f;

        for (uint32_t l = 0; l < line_count; ++l) {
            main_extent = larger(main_extent, lines[l].outer_main);
            cross_extent += lines[l].cross;
        }

        cross_extent += f->gap_cross * (float)(line_count > 1u ? line_count - 1u : 0u);
        extents = size_from_extents(f, main_extent, cross_extent);
    } else {
        arrange_items(ctx, f, items, lines, line_count);
    }

    cf_arena_rewind(ctx->scratch, mark);

    return extents;
}

cf_size flex_extent_of(const layout_ctx *ctx, uint32_t slot, const flex_pass *pass) {
    const flex_context f = context_of(ctx, slot, pass);

    return flex_resolve(ctx, slot, &f);
}

void flex_arrange(const layout_ctx *ctx, uint32_t slot, const flex_pass *pass) {
    const flex_context f = context_of(ctx, slot, pass);

    flex_resolve(ctx, slot, &f);
}
