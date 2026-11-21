#include "layout/internal.h"

/*
 * §9.8: the distance from the top edge of a box down to the first baseline its
 * content establishes. A box the engine cannot look inside - a leaf, a node a
 * measure callback owns, a grid - has no baseline to offer and aligns on the
 * bottom edge of its own box, the way CSS aligns a box without a baseline.
 */
static float node_baseline(const layout_ctx *ctx, uint32_t slot, float width, float height,
                           float percent_base) {
    const cf_style *style = &ctx->tree->styles[slot];
    const cf_measure_fn *measures = ctx->tree->measures;
    const bool foreign =
        (measures != NULL && measures[slot] != NULL) || style->display == CF_DISPLAY_GRID;

    if (foreign || child_count(ctx, slot) == 0u) {
        return height;
    }

    float baseline = height;
    const cf_edges inset = content_insets(ctx, slot, percent_base);
    const flex_pass pass = {
        .constraint = {.width = CF_PX(clamp_non_negative(width - inset.left - inset.right)),
                       .height = CF_PX(clamp_non_negative(height - inset.top - inset.bottom))},
        .intrinsic = CF_INTRINSIC_MAX_CONTENT,
        .mode = FLEX_MEASURE,
        .baseline = &baseline,
    };
    flex_extent_of(ctx, slot, &pass);

    return inset.top + baseline;
}

float item_ascent(const flex_context *f, const flex_item *item) {
    const float vertical_margin = f->row ? item->margin_cross_start : item->margin_main_start;
    const float width = f->row ? item->main : item->cross;
    const float height = f->row ? item->cross : item->main;

    return vertical_margin + node_baseline(f->ctx, item->slot, width, height, f->percent_base);
}

bool line_baseline(const flex_context *f, const flex_item *items, const flex_line *line,
                   float *ascent, float *descent) {
    if (!f->row) {
        return false;
    }

    float top = 0.0f;
    float bottom = 0.0f;
    bool group = false;

    for (uint32_t i = line->first; i < line->first + line->count; ++i) {
        if (align_of(f, &items[i]) != CF_ALIGN_BASELINE) {
            continue;
        }

        const float item_top = item_ascent(f, &items[i]);
        group = true;
        top = larger(top, item_top);
        bottom = larger(bottom, item_outer_cross(&items[i]) - item_top);
    }

    if (!group) {
        return false;
    }

    *ascent = top;
    *descent = bottom;
    return true;
}

/*
 * The main-axis start the line's first item is packed at. A column's baseline
 * counts its first item down the main axis, so a justify spread ahead of that
 * item pushes the baseline down with it.
 */
static float first_main_offset(const flex_context *f, const flex_item *items,
                               const flex_line *line) {
    if (!f->main_definite) {
        return 0.0f;
    }

    return justify_spacing(f->style->justify_content, line->count,
                           f->main_size - line_outer_main(f, items, line))
        .before;
}

float flow_baseline(const flex_context *f, const flex_item *items, const flex_line *lines) {
    const flex_line *line = &lines[0];

    if (f->row) {
        float ascent;
        float descent;
        if (line_baseline(f, items, line, &ascent, &descent)) {
            return ascent;
        }

        return item_ascent(f, &items[line->first]);
    }

    return first_main_offset(f, items, line) + item_ascent(f, &items[line->first]);
}
