#ifndef CFORM_LAYOUT_RECT_H
#define CFORM_LAYOUT_RECT_H

#include <stdbool.h>

#include "cf/types.h"

/*
 * The rectangle arithmetic the paint walk and the hit walk share. One home is
 * the point: both must cut a box to the same clip and agree on what survives
 * it, and a rule duplicated between them is how a node clipped out of the
 * picture came to take a click it should never have seen.
 */

/* Whether the two boxes reach each other at all. A zero-extent box does not. */
static inline bool rect_intersects(cf_rect box, cf_rect other) {
    return box.x < other.x + other.width && other.x < box.x + box.width &&
           box.y < other.y + other.height && other.y < box.y + box.height;
}

/*
 * The part of `box` that survives `clip`. This never asks about a disjoint
 * pair - the caller has already refused the box outright when rect_intersects
 * says they do not reach.
 */
static inline cf_rect rect_intersect(cf_rect box, cf_rect clip) {
    const float left = box.x > clip.x ? box.x : clip.x;
    const float top = box.y > clip.y ? box.y : clip.y;
    const float box_right = box.x + box.width;
    const float box_bottom = box.y + box.height;
    const float clip_right = clip.x + clip.width;
    const float clip_bottom = clip.y + clip.height;
    const float right = box_right < clip_right ? box_right : clip_right;
    const float bottom = box_bottom < clip_bottom ? box_bottom : clip_bottom;

    return (cf_rect){.x = left, .y = top, .width = right - left, .height = bottom - top};
}

/*
 * Whether `point` falls inside `r`, on its half-open edges like everything the
 * walks place: the near edge belongs to the box, the far edge to its neighbour.
 */
static inline bool rect_contains(cf_rect r, cf_point point) {
    return point.x >= r.x && point.x < r.x + r.width && point.y >= r.y && point.y < r.y + r.height;
}

#endif
