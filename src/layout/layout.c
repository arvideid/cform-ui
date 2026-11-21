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
 * Whether a node frames the absolutely positioned descendants below it. A
 * static box does not: it moves its children but owns no coordinate system for
 * them, so the block keeps travelling up until it finds a box that does. The
 * root frames its own because the engine is told no viewport to stand in for
 * the initial containing block.
 */
static bool frames_block(const layout_ctx *ctx, uint32_t slot, const cf_style *style) {
    return style->position != CF_POSITION_STATIC || slot == ctx->root;
}

/*
 * The padding box: a border box taken inside its own frame. Declared sizes are
 * border boxes here, so the frame comes back off both edges to reach the box
 * CSS hands to an absolutely positioned child.
 */
static cf_rect padding_box(cf_rect border_box, cf_edges frame) {
    return (cf_rect){.x = border_box.x + frame.left,
                     .y = border_box.y + frame.top,
                     .width = clamp_non_negative(border_box.width - frame.left - frame.right),
                     .height = clamp_non_negative(border_box.height - frame.top - frame.bottom)};
}

/*
 * Padding wider than the box leaves a content extent of zero rather than a
 * negative one: every offset a child computes from the extent would otherwise
 * invert.
 */
void layout_subtree(const layout_ctx *ctx, uint32_t slot, cf_available constraint,
                    cf_available verdict, cf_point origin, cf_rect containing) {
    const cf_size border =
        border_size_of(ctx, slot, CF_INTRINSIC_MAX_CONTENT, constraint, verdict, false);
    const cf_style *style = &ctx->tree->styles[slot];
    /* A root's own margin and insets were counted before the recursion began,
     * because nothing above it places it. */
    const cf_point placed = slot == ctx->root ? origin : relative_origin(style, constraint, origin);
    const cf_rect box = {
        .x = placed.x, .y = placed.y, .width = border.width, .height = border.height};

    ctx->rects[slot] = box;

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

    const cf_available content_box = {.width = CF_PX(content.width),
                                      .height = CF_PX(content.height)};
    const cf_point content_origin = {.x = box.x + inset.left, .y = box.y + inset.top};
    const cf_rect block = frames_block(ctx, slot, style)
                              ? padding_box(box, cf_box_resolve(style->border, percent_base))
                              : containing;

    if (style->display == CF_DISPLAY_GRID) {
        grid_arrange(ctx, slot, content_box, content_origin, block);

        return;
    }

    const flex_pass pass = {
        .constraint = content_box,
        .origin = content_origin,
        .containing = block,
        .mode = FLEX_ARRANGE,
    };

    flex_arrange(ctx, slot, &pass);
}

/*
 * The root is the only box the engine lays out in a flow nobody owns: the
 * caller names where it starts, and the root's own margin and insets move it
 * from there exactly as they move a box sitting in a containing block. Every
 * other node arrives at an origin its container has already counted its
 * margins into, so this belongs to `cf_layout` and not to the recursion.
 */
static cf_point root_origin(const cf_style *style, cf_available available, cf_point origin) {
    float percent_base = 0.0f;
    definite_offer(available.width, &percent_base);

    const cf_point margined = {.x = origin.x + cf_length_resolve(style->margin.left, percent_base),
                               .y = origin.y + cf_length_resolve(style->margin.top, percent_base)};

    if (style->position == CF_POSITION_STATIC) {
        return margined;
    }

    return (cf_point){
        .x = margined.x + inset_offset(style->inset.left, style->inset.right, available.width),
        .y = margined.y + inset_offset(style->inset.top, style->inset.bottom, available.height)};
}

void cf_layout(cf_arena *scratch, const cf_tree *tree, uint32_t root, cf_available available,
               cf_point origin, cf_rect *rects) {
    if (scratch == NULL || tree == NULL || rects == NULL || root >= tree->node_count) {
        return;
    }

    /* The cache and the per-pass tables live only as long as the layout:
     * a caller that runs layouts back to back on one arena finds it as it
     * left it. */
    const size_t mark = cf_arena_mark(scratch);

    layout_ctx ctx = {.scratch = scratch, .tree = tree, .rects = rects, .root = root};
    measure_cache_init(&ctx);
    const cf_available undecided = {.width = CF_AUTO, .height = CF_AUTO};
    /* Nothing is inherited above the root: it frames its own box, so this
     * block is never read. */
    const cf_rect nowhere = {0.0f, 0.0f, 0.0f, 0.0f};

    layout_subtree(&ctx, root, available, undecided,
                   root_origin(&tree->styles[root], available, origin), nowhere);

    cf_arena_rewind(scratch, mark);
}
