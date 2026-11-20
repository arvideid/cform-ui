#include "layout/internal.h"

/*
 * One physical axis of an out-of-flow child. `block` is the containing block's
 * extent on that axis: definite, because a box only becomes a containing block
 * once it has been sized - which is what makes every percentage here, and the
 * two-inset sizing below, always measurable.
 */
typedef struct absolute_axis {
    cf_length declared;
    cf_length minimum;
    cf_length maximum;
    cf_length start;
    cf_length end;
    cf_length margin_start;
    cf_length margin_end;
    cf_length block;
    bool width_axis;
} absolute_axis;

static absolute_axis horizontal(const cf_style *child, const cf_available *block) {
    return (absolute_axis){
        .declared = child->width,
        .minimum = child->min_width,
        .maximum = child->max_width,
        .start = child->inset.left,
        .end = child->inset.right,
        .margin_start = child->margin.left,
        .margin_end = child->margin.right,
        .block = block->width,
        .width_axis = true,
    };
}

static absolute_axis vertical(const cf_style *child, const cf_available *block) {
    return (absolute_axis){
        .declared = child->height,
        .minimum = child->min_height,
        .maximum = child->max_height,
        .start = child->inset.top,
        .end = child->inset.bottom,
        .margin_start = child->margin.top,
        .margin_end = child->margin.bottom,
        .block = block->height,
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
    float extent = 0.0f;
    definite_offer(axis->block, &extent);

    float size;
    float start;
    float end;

    if (!cf_length_of(axis->declared, axis->block, &size)) {
        if (cf_length_of(axis->start, axis->block, &start) &&
            cf_length_of(axis->end, axis->block, &end)) {
            size = extent - start - end - cf_length_resolve(axis->margin_start, extent) -
                   cf_length_resolve(axis->margin_end, extent);
        } else {
            const cf_available undecided = {.width = CF_AUTO, .height = CF_AUTO};
            const cf_size natural =
                border_size_of(ctx, slot, CF_INTRINSIC_MAX_CONTENT, constraint, undecided);

            size = axis->width_axis ? natural.width : natural.height;
        }
    }

    return cf_size_clamp(size, axis->minimum, axis->maximum, axis->block);
}

/*
 * Where the child's edge sits on this axis: the start inset counted from the
 * containing block's start edge, else the far inset counted inwards from its
 * far edge, else the static position - where the flow would have put the child
 * in the container that owns it, plus its own margin, which is all an inset of
 * `auto` means here. Margins resolve against the block whatever the answer is.
 */
static float absolute_position(const absolute_axis *axis, float size, float block_start,
                               float static_start) {
    float extent = 0.0f;
    definite_offer(axis->block, &extent);

    float start;
    float end;

    if (cf_length_of(axis->start, axis->block, &start)) {
        return block_start + start + cf_length_resolve(axis->margin_start, extent);
    }

    if (extent > 0.0f && cf_length_of(axis->end, axis->block, &end)) {
        return block_start + extent - end - size - cf_length_resolve(axis->margin_end, extent);
    }

    return static_start + cf_length_resolve(axis->margin_start, extent);
}

void place_absolute(const layout_ctx *ctx, uint32_t slot, cf_rect containing, cf_point origin) {
    const uint32_t first = ctx->tree->child_start[slot];
    const uint32_t count = child_count(ctx, slot);
    const cf_available block = {.width = CF_PX(containing.width),
                                .height = CF_PX(containing.height)};

    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t child_slot = ctx->tree->child_slots[first + i];
        const cf_style *child = &ctx->tree->styles[child_slot];

        if (child->position != CF_POSITION_ABSOLUTE) {
            continue;
        }

        const absolute_axis x_axis = horizontal(child, &block);
        const absolute_axis y_axis = vertical(child, &block);

        const float width = absolute_size(ctx, child_slot, block, &x_axis);
        const float height = absolute_size(ctx, child_slot, block, &y_axis);

        const cf_point placed = {
            .x = absolute_position(&x_axis, width, containing.x, origin.x),
            .y = absolute_position(&y_axis, height, containing.y, origin.y),
        };

        const size_t mark = cf_arena_mark(ctx->scratch);
        layout_subtree(ctx, child_slot, block,
                       (cf_available){.width = CF_PX(width), .height = CF_PX(height)}, placed,
                       containing);
        cf_arena_rewind(ctx->scratch, mark);
    }
}
