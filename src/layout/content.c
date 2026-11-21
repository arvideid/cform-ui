#include "layout/internal.h"

/*
 * The length an axis has without measuring: the parent's verdict, then the
 * node's own declaration. False means the axis is content-based and has to be
 * measured before it exists. A percentage declared inside a measuring pass is
 * a percentage of the extent that pass exists to find, so it waits for the
 * arranging pass, where the offer is the settled containing box.
 */
static bool settled_length(cf_length verdict, cf_length declaration, cf_length constraint,
                           bool measuring, float *out) {
    if (measuring && declaration.kind == CF_LENGTH_PERCENT) {
        return definite_offer(verdict, out);
    }

    return definite_offer(verdict, out) || cf_length_of(declaration, constraint, out);
}

/*
 * A callback claims a node's content even when the node has children: the
 * engine lays those children out inside whatever the callback reported, which
 * is the only way a foreign subtree can keep its own layout rules. Neither
 * table has to be there: a caller that measures nothing carries neither, and a
 * caller whose callbacks need no state carries only the function table.
 */
static bool measured_extent(const layout_ctx *ctx, uint32_t slot, cf_available constraint,
                            cf_intrinsic mode, cf_size *out) {
    const cf_measure_fn *measures = ctx->tree->measures;

    if (measures == NULL || measures[slot] == NULL) {
        return false;
    }

    void *user_data = ctx->tree->measure_data != NULL ? ctx->tree->measure_data[slot] : NULL;
    const cf_size measured = measures[slot](user_data, constraint, mode);

    *out = (cf_size){.width = clamp_non_negative(measured.width),
                     .height = clamp_non_negative(measured.height)};

    return true;
}

/*
 * How far the content box sits inside the border box. Padding and border are
 * only ever added together - the engine paints no separate frame - so they
 * resolve into one set of edges.
 */
cf_edges content_insets(const layout_ctx *ctx, uint32_t slot, float percent_base) {
    const cf_style *style = &ctx->tree->styles[slot];
    const cf_edges padding = cf_box_resolve(style->padding, percent_base);
    const cf_edges border = cf_box_resolve(style->border, percent_base);

    return (cf_edges){.left = padding.left + border.left,
                      .top = padding.top + border.top,
                      .right = padding.right + border.right,
                      .bottom = padding.bottom + border.bottom};
}

/*
 * The content extent of a node: what a callback reports, what its children
 * fill, or nothing at all when it is a leaf nobody can measure.
 */
cf_size content_extent_of(const layout_ctx *ctx, uint32_t slot, cf_intrinsic mode,
                          cf_available constraint) {
    cf_size measured;

    if (measured_extent(ctx, slot, constraint, mode, &measured)) {
        return measured;
    }

    if (child_count(ctx, slot) == 0u) {
        return (cf_size){0};
    }

    if (ctx->tree->styles[slot].display == CF_DISPLAY_GRID) {
        return grid_extent_of(ctx, slot, constraint, mode);
    }

    const flex_pass pass = {.constraint = constraint, .intrinsic = mode, .mode = FLEX_MEASURE};

    return flex_extent_of(ctx, slot, &pass);
}

cf_size border_size_of(const layout_ctx *ctx, uint32_t slot, cf_intrinsic mode,
                       cf_available constraint, cf_available verdict, bool measuring) {
    const cf_style *style = &ctx->tree->styles[slot];

    float percent_base = 0.0f;
    definite_offer(constraint.width, &percent_base);

    const cf_edges inset = content_insets(ctx, slot, percent_base);

    float width = 0.0f;
    float height = 0.0f;
    const bool has_width =
        settled_length(verdict.width, style->width, constraint.width, measuring, &width);
    const bool has_height =
        settled_length(verdict.height, style->height, constraint.height, measuring, &height);

    /* An axis that is already settled bounds the box the other axis measures
     * itself into, so a wrapped callback gets the width the node will have. */
    const cf_available content_offer = {
        .width = has_width ? CF_PX(width) : constraint.width,
        .height = has_height ? CF_PX(height) : constraint.height,
    };

    if (!has_width || !has_height) {
        const cf_size content = content_extent_of(ctx, slot, mode, content_offer);

        if (!has_width) {
            width = content.width + inset.left + inset.right;
        }
        if (!has_height) {
            height = content.height + inset.top + inset.bottom;
        }

        /* Shrink-to-fit, and only on the inline axis: content with no verdict
         * takes the width it was offered rather than pushing past it. CSS caps
         * no auto height against an offered height, so overflowing vertically
         * stays legal. */
        float offered_width;
        if (!has_width && mode == CF_INTRINSIC_MAX_CONTENT &&
            definite_offer(constraint.width, &offered_width) && width > offered_width) {
            width = offered_width;
        }
    }

    /* min and max clamp the border box, and min keeps the last word when the
     * pair contradicts, so a misconfigured node still lands somewhere. */
    const float clamped_width =
        cf_size_clamp(width, style->min_width, style->max_width, constraint.width);
    const float clamped_height =
        cf_size_clamp(height, style->min_height, style->max_height, constraint.height);

    /* A border box is drawn no smaller than the padding and border it has to
     * hold: the frame has the last word, the way min does when min and max
     * contradict. A declared or clamped size below it is impossible to paint. */
    return (cf_size){.width = larger(clamped_width, inset.left + inset.right),
                     .height = larger(clamped_height, inset.top + inset.bottom)};
}
