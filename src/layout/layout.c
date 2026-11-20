#include "layout/internal.h"

bool definite_offer(cf_length offer, float *out) {
    /* A pass settles an axis in pixels or leaves it auto. A percentage offer
     * would be a percentage of something else, so it reads as no offer. */
    if (offer.kind != CF_LENGTH_PX) {
        return false;
    }

    *out = offer.value;
    return true;
}

float clamp_non_negative(float value) {
    return value < 0.0f ? 0.0f : value;
}

float clamp_range(float value, float floor_value, float ceiling) {
    value = clamp_non_negative(value);

    if (value > ceiling) {
        value = ceiling;
    }

    return value < floor_value ? floor_value : value;
}

float larger(float first, float second) {
    return first > second ? first : second;
}

uint32_t child_count(const layout_ctx *ctx, uint32_t slot) {
    return ctx->tree->child_start[slot + 1u] - ctx->tree->child_start[slot];
}

/*
 * One axis of a relative box's offset: the start inset moves it along, the end
 * inset moves it back, and a box named on both edges goes where the start edge
 * says, which is CSS in a left-to-right writing mode. An inset of `auto`, and a
 * percentage of an extent still being measured, move it nowhere.
 */
static float inset_offset(cf_length start, cf_length end, cf_length box) {
    float offset;

    if (cf_length_of(start, box, &offset)) {
        return offset;
    }

    if (cf_length_of(end, box, &offset)) {
        return -offset;
    }

    return 0.0f;
}

/*
 * A relative box leaves the place its line gave it and takes its content along.
 * The box the parent offered is its containing block, so an inset written as a
 * percentage is a percentage of that box's own extent on the axis it names.
 */
static cf_point relative_origin(const cf_style *style, cf_available constraint, cf_point origin) {
    if (style->position != CF_POSITION_RELATIVE) {
        return origin;
    }

    return (cf_point){
        .x = origin.x + inset_offset(style->inset.left, style->inset.right, constraint.width),
        .y = origin.y + inset_offset(style->inset.top, style->inset.bottom, constraint.height)};
}

/*
 * Padding wider than the box leaves a content extent of zero rather than a
 * negative one: every offset a child computes from the extent would otherwise
 * invert.
 */
void layout_subtree(const layout_ctx *ctx, uint32_t slot, cf_available constraint,
                    cf_available verdict, cf_point origin) {
    const cf_size border = border_size_of(ctx, slot, CF_INTRINSIC_MAX_CONTENT, constraint, verdict);
    const cf_style *style = &ctx->tree->styles[slot];
    const cf_point placed = relative_origin(style, constraint, origin);

    ctx->rects[slot] =
        (cf_rect){.x = placed.x, .y = placed.y, .width = border.width, .height = border.height};

    if (child_count(ctx, slot) == 0u) {
        return;
    }

    float percent_base = 0.0f;
    definite_offer(constraint.width, &percent_base);
    const cf_edges inset = content_insets(ctx, slot, percent_base);

    const cf_size content = {
        .width = clamp_non_negative(border.width - inset.left - inset.right),
        .height = clamp_non_negative(border.height - inset.top - inset.bottom),
    };

    const cf_available box = {.width = CF_PX(content.width), .height = CF_PX(content.height)};
    const cf_point content_origin = {.x = placed.x + inset.left, .y = placed.y + inset.top};

    if (style->display == CF_DISPLAY_GRID) {
        grid_arrange(ctx, slot, box, content_origin);

        return;
    }

    const flex_pass pass = {
        .constraint = box,
        .origin = content_origin,
        .mode = FLEX_ARRANGE,
    };

    flex_arrange(ctx, slot, &pass);
}

void cf_layout(cf_arena *scratch, const cf_tree *tree, uint32_t root, cf_available available,
               cf_point origin, cf_rect *rects) {
    if (scratch == NULL || tree == NULL || rects == NULL || root >= tree->node_count) {
        return;
    }

    const layout_ctx ctx = {.scratch = scratch, .tree = tree, .rects = rects};
    const cf_available undecided = {.width = CF_AUTO, .height = CF_AUTO};

    layout_subtree(&ctx, root, available, undecided, origin);
}
