#include "layout/internal.h"

/*
 * One physical axis of an out-of-flow child. `block` is the containing block's
 * extent on that axis: definite, because a box only becomes a containing block
 * once it has been sized - which is what makes every percentage here, and the
 * two-inset sizing below, always measurable. The margins are resolved against
 * it for the same reason.
 */
typedef struct absolute_axis {
    cf_length declared;
    cf_length minimum;
    cf_length maximum;
    cf_length start;
    cf_length end;
    float margin_start;
    float margin_end;
    cf_length block;
    bool width_axis;
} absolute_axis;

static absolute_axis horizontal(const cf_style *child, const cf_available *block) {
    float extent = 0.0f;
    definite_offer(block->width, &extent);

    return (absolute_axis){
        .declared = child->width,
        .minimum = child->min_width,
        .maximum = child->max_width,
        .start = child->inset.left,
        .end = child->inset.right,
        .margin_start = cf_length_resolve(child->margin.left, extent),
        .margin_end = cf_length_resolve(child->margin.right, extent),
        .block = block->width,
        .width_axis = true,
    };
}

static absolute_axis vertical(const cf_style *child, const cf_available *block) {
    float extent = 0.0f;
    definite_offer(block->height, &extent);

    return (absolute_axis){
        .declared = child->height,
        .minimum = child->min_height,
        .maximum = child->max_height,
        .start = child->inset.top,
        .end = child->inset.bottom,
        .margin_start = cf_length_resolve(child->margin.top, extent),
        .margin_end = cf_length_resolve(child->margin.bottom, extent),
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
            size = extent - start - end - axis->margin_start - axis->margin_end;
        } else {
            const cf_available undecided = {.width = CF_AUTO, .height = CF_AUTO};
            const cf_size natural =
                border_size_of(ctx, slot, CF_INTRINSIC_MAX_CONTENT, constraint, undecided, false);

            size = axis->width_axis ? natural.width : natural.height;
        }
    }

    return cf_size_clamp(size, axis->minimum, axis->maximum, axis->block);
}

/*
 * Where the child's edge sits on this axis: the start inset counted from the
 * containing block's start edge, else the far inset counted inwards from its
 * far edge, else the static position, which the container's flow has already
 * worked out and the caller hands over with the child's own margin added.
 */
static float absolute_position(const absolute_axis *axis, float size, float block_start,
                               float static_start) {
    float extent = 0.0f;
    definite_offer(axis->block, &extent);

    float start;
    float end;

    if (cf_length_of(axis->start, axis->block, &start)) {
        return block_start + start + axis->margin_start;
    }

    if (extent > 0.0f && cf_length_of(axis->end, axis->block, &end)) {
        return block_start + extent - end - size - axis->margin_end;
    }

    return static_start;
}

/* Where a container leaves the one box it packs on an axis. */
typedef enum box_pack { BOX_PACK_START, BOX_PACK_CENTER, BOX_PACK_END } box_pack;

/* A lone box has no gaps between neighbours to distribute, so the keywords
 * that spread a line out leave it where a single box of their own would sit:
 * centred, except for `space-between`, which gives it no slack in front. */
static box_pack main_packing(cf_justify packing) {
    switch (packing) {
    case CF_JUSTIFY_CENTER:
    case CF_JUSTIFY_SPACE_AROUND:
    case CF_JUSTIFY_SPACE_EVENLY:
        return BOX_PACK_CENTER;
    case CF_JUSTIFY_END:
        return BOX_PACK_END;
    case CF_JUSTIFY_START:
    case CF_JUSTIFY_SPACE_BETWEEN:
        break;
    }

    return BOX_PACK_START;
}

/* Stretch grows the box to the extent instead of sitting inside it, and
 * baseline needs a text metric a box outside the flow never reports; both
 * reach the start edge, which is where the box would be without them. */
static box_pack cross_packing(cf_align alignment) {
    switch (alignment) {
    case CF_ALIGN_CENTER:
        return BOX_PACK_CENTER;
    case CF_ALIGN_END:
        return BOX_PACK_END;
    case CF_ALIGN_AUTO:
    case CF_ALIGN_START:
    case CF_ALIGN_STRETCH:
    case CF_ALIGN_BASELINE:
        break;
    }

    return BOX_PACK_START;
}

/*
 * How far a box sits from the start edge of the axis packing it, counted to its
 * near margin edge. An overflowing box is centred or end-packed on the size it
 * overflows with rather than being pushed back to the start, which is what the
 * alignment of a too-large box does in the flow too.
 */
static float pack_from_start(box_pack pack, float extent, float size, float margin_start,
                             float margin_end) {
    const float extra = extent - margin_start - size - margin_end;

    switch (pack) {
    case BOX_PACK_CENTER:
        return margin_start + extra * 0.5f;
    case BOX_PACK_END:
        return extent - margin_end - size;
    case BOX_PACK_START:
        break;
    }

    return margin_start;
}

/*
 * The static position: where the container that owns the child would have put
 * it had it stayed in the flow as the only box on its line, which is what
 * Flexible Box Layout §4 asks of an absolute child with no insets. The
 * container's packing decides each axis, the item's own alignment wins on the
 * cross one, and an axis still being measured has no room to pack at all.
 *
 * Distances are counted from each axis's own start edge, where §9.3 puts a
 * reversed direction's start on the far physical edge and §9.4 does the same
 * for a reversed wrap: an axis reads its start margin and its far margin off
 * the pair of edges it packs between.
 */
static cf_point static_position(const absolute_flow *flow, const cf_style *child, cf_size size,
                                cf_edges margins) {
    float width = 0.0f;
    float height = 0.0f;
    definite_offer(flow->content.width, &width);
    definite_offer(flow->content.height, &height);

    const float main_extent = flow->row ? width : height;
    const float cross_extent = flow->row ? height : width;
    const float main_size = flow->row ? size.width : size.height;
    const float cross_size = flow->row ? size.height : size.width;

    const float main_start = flow->row ? (flow->main_reverse ? margins.right : margins.left)
                                       : (flow->main_reverse ? margins.bottom : margins.top);
    const float main_end = flow->row ? (flow->main_reverse ? margins.left : margins.right)
                                     : (flow->main_reverse ? margins.top : margins.bottom);
    const float cross_start = flow->row ? (flow->cross_reverse ? margins.bottom : margins.top)
                                        : (flow->cross_reverse ? margins.right : margins.left);
    const float cross_end = flow->row ? (flow->cross_reverse ? margins.top : margins.bottom)
                                      : (flow->cross_reverse ? margins.left : margins.right);

    const cf_align alignment = child->align_self != CF_ALIGN_AUTO ? child->align_self : flow->cross;
    const float main_logical =
        pack_from_start(main_packing(flow->main), main_extent, main_size, main_start, main_end);
    const float cross_logical =
        pack_from_start(cross_packing(alignment), cross_extent, cross_size, cross_start, cross_end);

    const float main_position =
        flow->main_reverse ? main_extent - main_logical - main_size : main_logical;
    const float cross_position =
        flow->cross_reverse ? cross_extent - cross_logical - cross_size : cross_logical;

    return (cf_point){.x = flow->origin.x + (flow->row ? main_position : cross_position),
                      .y = flow->origin.y + (flow->row ? cross_position : main_position)};
}

void place_absolute(const layout_ctx *ctx, uint32_t slot, cf_rect containing,
                    const absolute_flow *flow) {
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

        const cf_edges margins = {.left = x_axis.margin_start,
                                  .top = y_axis.margin_start,
                                  .right = x_axis.margin_end,
                                  .bottom = y_axis.margin_end};
        const cf_point flowed =
            static_position(flow, child, (cf_size){.width = width, .height = height}, margins);

        const cf_point placed = {
            .x = absolute_position(&x_axis, width, containing.x, flowed.x),
            .y = absolute_position(&y_axis, height, containing.y, flowed.y),
        };

        const size_t mark = cf_arena_mark(ctx->scratch);
        layout_subtree(ctx, child_slot, block,
                       (cf_available){.width = CF_PX(width), .height = CF_PX(height)}, placed,
                       containing);
        cf_arena_rewind(ctx->scratch, mark);
    }
}
