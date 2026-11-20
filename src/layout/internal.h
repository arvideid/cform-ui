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

#endif
