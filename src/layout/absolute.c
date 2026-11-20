#include "layout/internal.h"

/*
 * One physical axis of an out-of-flow child. `container` is the content box's
 * extent here, still auto while the pass has not settled it - which is also
 * what makes a percentage inset and the far inset unusable, because there is
 * nothing left to measure them against.
 */
typedef struct absolute_axis {
    cf_length declared;
    cf_length minimum;
    cf_length maximum;
    cf_length start;
    cf_length end;
    cf_length margin_start;
    cf_length margin_end;
    cf_length container;
    bool width_axis;
} absolute_axis;

static absolute_axis horizontal(const cf_style *child, const cf_available *box) {
    return (absolute_axis){
        .declared = child->width,
        .minimum = child->min_width,
        .maximum = child->max_width,
        .start = child->inset.left,
        .end = child->inset.right,
        .margin_start = child->margin.left,
        .margin_end = child->margin.right,
        .container = box->width,
        .width_axis = true,
    };
}

static absolute_axis vertical(const cf_style *child, const cf_available *box) {
    return (absolute_axis){
        .declared = child->height,
        .minimum = child->min_height,
        .maximum = child->max_height,
        .start = child->inset.top,
        .end = child->inset.bottom,
        .margin_start = child->margin.top,
        .margin_end = child->margin.bottom,
        .container = box->height,
    };
}

/*
 * The size the child takes on this axis: what it declared, else the room two
 * definite insets leave, else what its content asks for - which is the
 * shrink-to-fit a box in the flow would get, because an out-of-flow child has
 * no line to stretch across. min and max bound whichever answer comes out.
 */
static float absolute_size(const layout_ctx *ctx, uint32_t slot, cf_available constraint,
                           const absolute_axis *axis) {
    float box = 0.0f;
    definite_offer(axis->container, &box);

    float size;
    float start;
    float end;

    if (!cf_length_of(axis->declared, axis->container, &size)) {
        if (cf_length_of(axis->start, axis->container, &start) &&
            cf_length_of(axis->end, axis->container, &end)) {
            size = box - start - end - cf_length_resolve(axis->margin_start, box) -
                   cf_length_resolve(axis->margin_end, box);
        } else {
            const cf_available undecided = {.width = CF_AUTO, .height = CF_AUTO};
            const cf_size natural =
                border_size_of(ctx, slot, CF_INTRINSIC_MAX_CONTENT, constraint, undecided);

            size = axis->width_axis ? natural.width : natural.height;
        }
    }

    return cf_size_clamp(size, axis->minimum, axis->maximum, axis->container);
}

/*
 * How far the child sits from the containing block's start edge: the start
 * inset, else the far inset measured inwards from an extent the pass has
 * settled, else the static position - the content origin plus its own margin,
 * which is all an inset of `auto` means here.
 */
static float absolute_offset(const absolute_axis *axis, float size) {
    float box = 0.0f;
    definite_offer(axis->container, &box);

    float start;
    float end;

    if (cf_length_of(axis->start, axis->container, &start)) {
        return start + cf_length_resolve(axis->margin_start, box);
    }

    if (box > 0.0f && cf_length_of(axis->end, axis->container, &end)) {
        return box - end - size - cf_length_resolve(axis->margin_end, box);
    }

    return cf_length_resolve(axis->margin_start, box);
}

void place_absolute(const layout_ctx *ctx, uint32_t slot, cf_available box, cf_point origin) {
    const uint32_t first = ctx->tree->child_start[slot];
    const uint32_t count = child_count(ctx, slot);

    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t child_slot = ctx->tree->child_slots[first + i];
        const cf_style *child = &ctx->tree->styles[child_slot];

        if (child->position != CF_POSITION_ABSOLUTE) {
            continue;
        }

        const absolute_axis x_axis = horizontal(child, &box);
        const absolute_axis y_axis = vertical(child, &box);

        const float width = absolute_size(ctx, child_slot, box, &x_axis);
        const float height = absolute_size(ctx, child_slot, box, &y_axis);

        const cf_point placed = {
            .x = origin.x + absolute_offset(&x_axis, width),
            .y = origin.y + absolute_offset(&y_axis, height),
        };

        const size_t mark = cf_arena_mark(ctx->scratch);
        layout_subtree(ctx, child_slot, box,
                       (cf_available){.width = CF_PX(width), .height = CF_PX(height)}, placed);
        cf_arena_rewind(ctx->scratch, mark);
    }
}
