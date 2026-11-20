#ifndef CFORM_LAYOUT_INTERNAL_H
#define CFORM_LAYOUT_INTERNAL_H

#include "cf/layout.h"

/*
 * What a pass over a container's children is for. Arranging places every child
 * inside a content box whose extents are definite, writes rects and recurses.
 * Measuring reports the extent the children need and writes nothing; an axis
 * still being measured arrives as auto, so a percentage on it stays unresolved.
 */
typedef enum flex_mode { FLEX_ARRANGE, FLEX_MEASURE } flex_mode;

typedef struct layout_ctx {
    cf_arena *scratch;
    const cf_tree *tree;
    cf_rect *rects;
} layout_ctx;

typedef struct flex_pass {
    cf_available constraint;
    cf_point origin;
    cf_intrinsic intrinsic;
    flex_mode mode;
} flex_pass;

/*
 * One container seen from its own axes. `main` and `cross` are the layout
 * axes, `row` says which of width and height each is, and `reverse` says which
 * physical edge main-start is.
 */
typedef struct flex_context {
    const layout_ctx *ctx;
    const cf_style *style;
    uint32_t slot;
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
 * How much space a distribution mode leaves before the first box and between
 * each pair.
 */
typedef struct line_spacing {
    float before;
    float between;
} line_spacing;

/* A pass offers either a length it settled on or nothing at all, so a percent
 * of an offer is only a length when the offer is in pixels. */
bool definite_offer(cf_length offer, float *out);

/* A box has no negative extent: padding wider than the box, a callback that
 * answers in negatives and a shortfall that overflows all read as zero. */
float clamp_non_negative(float value);

uint32_t child_count(const layout_ctx *ctx, uint32_t slot);

/*
 * Lays one node out. `constraint` is the box it must fit - the parent's
 * content box - and `verdict` is what the parent already decided about each
 * axis. A verdict wins over the node's own declaration; an auto verdict lets
 * the node settle its axis from its declaration or its content.
 */
void layout_subtree(const layout_ctx *ctx, uint32_t slot, cf_available constraint,
                    cf_available verdict, cf_point origin);

/*
 * The content box of a node: its measure callback's answer when it has one and
 * its children's flex extent when it does not. Margins are not included.
 */
cf_size content_extent_of(const layout_ctx *ctx, uint32_t slot, cf_intrinsic mode,
                          cf_available constraint);

/* Padding and border as one set of resolved edges, summed per side. */
cf_edges content_insets(const layout_ctx *ctx, uint32_t slot, float percent_base);

/*
 * The border box a node takes for itself: its content grown by its insets and
 * forced inside its own min and max. Margins are not included; a flex line
 * adds the ones it resolved.
 */
cf_size border_size_of(const layout_ctx *ctx, uint32_t slot, cf_intrinsic mode,
                       cf_available constraint, cf_available verdict);

/* Flexbox §9.2 through §9.7 over slot's children. */
cf_size flex_extent_of(const layout_ctx *ctx, uint32_t slot, const flex_pass *pass);
void flex_arrange(const layout_ctx *ctx, uint32_t slot, const flex_pass *pass);

/*
 * A float forced inside its bounds. The floor keeps the last word when the
 * pair contradicts, so a misconfigured item still lands somewhere.
 */
float clamp_range(float value, float floor_value, float ceiling);

cf_available verdict_for(const flex_context *f, cf_length main, cf_length cross);
float larger(float first, float second);

/* An item's outer size: its box plus the margins the pass resolved for it. */
float item_outer_main(const flex_item *item);
float item_outer_cross(const flex_item *item);
float line_outer_main(const flex_context *f, const flex_item *items, const flex_line *line);

/*
 * §9.1 item 4 and §9.2.1: the item table for the children that stay in the
 * flow. `capacity` is every child, because the table is allocated before the
 * pass knows how many of them leave the flow; the count returned stays.
 */
uint32_t build_items(const flex_context *f, flex_item *items, uint32_t capacity);

/* §9.2.1 steps 16-18: an item whose main size the pass changed is measured
 * again, because its content can wrap differently at the size it will have. */
void remeasure_item_cross(const flex_context *f, flex_item *items, uint32_t count);

/* §9.5: break the items into lines, and §9.7 give them their main sizes. */
void collect_lines(const flex_context *f, const flex_item *items, uint32_t count, flex_line *lines,
                   uint32_t *line_count);
void grow_line(const flex_context *f, flex_item *items, const flex_line *line);
void shrink_line(const flex_context *f, flex_item *items, const flex_line *line);
void collect_line_cross(const flex_context *f, const flex_item *items, flex_line *lines,
                        uint32_t line_count);

/* §9.5.2 and §9.6: place the lines, then every item inside them. */
void arrange_items(const layout_ctx *ctx, const flex_context *f, flex_item *items, flex_line *lines,
                   uint32_t line_count);

/*
 * The children of `slot` that leave the flow: measured against the content box
 * `box` and placed from its `origin` by their insets, in declaration order.
 * Out-of-flow children add nothing to the container's own extent, which is why
 * a box holding only them measures as empty.
 */
void place_absolute(const layout_ctx *ctx, uint32_t slot, cf_available box, cf_point origin);

#endif
