#ifndef CF_LAYOUT_H
#define CF_LAYOUT_H

#include "cf/base.h"
#include "cf/style.h"

/*
 * The space a node is offered. AUTO on an axis means indefinite: the node
 * measures its content and the caller accepts whatever comes out. A definite
 * length caps the axis, and a node that asks for more is shrunk or clipped to
 * it by the flex rules, never by this vocabulary.
 */
typedef struct cf_available {
    cf_length width;
    cf_length height;
} cf_available;

/*
 * Content size of a node the engine cannot see into — text, an image, a
 * foreign subtree. `kind` says whether the caller wants the smallest the
 * content can be packed into or its natural extent; `available` carries the
 * axes that are definite, so a wrap-aware implementation can honour them.
 * Returning a negative extent is a bug in the callback: the engine clamps it
 * to zero rather than trusting it.
 */
typedef cf_size (*cf_measure_fn)(void *user_data, cf_available available, cf_intrinsic kind);

/*
 * The tree as parallel arrays, indexed by slot. One job per array, so a
 * subtree can be moved, serialized or replayed without touching pointers.
 *
 * Children are a compressed sparse row: `child_start[slot]` and
 * `child_start[slot + 1]` delimit that node's children inside `child_slots`,
 * so `child_start` has node_count + 1 entries. `child_slots` holds slot
 * numbers in declaration order; paint order follows it.
 *
 * Invariant: the arrays describe a tree — no slot is its own descendant. The
 * solver recurses without visiting a cycle and a cycle would recurse forever,
 * so this is the caller's to keep, not the engine's to check.
 *
 * `measure_data` is handed to the callback unread and unchanged: the engine
 * never looks through it, so a callback may point it at whatever its own
 * state needs. Either array may be NULL, which is how a caller that measures
 * nothing writes the tree; a callback then reads as absent and its node is
 * measured from its children instead.
 */
typedef struct cf_tree {
    const cf_style *styles;
    const cf_measure_fn *measures;
    void **measure_data;
    const uint32_t *child_start;
    const uint32_t *child_slots;
    uint32_t node_count;
} cf_tree;

/*
 * Lays out the subtree rooted at `root`: every node it reaches gets an
 * absolute border-box rect at `rects[slot]`, in the coordinate space whose
 * origin is `origin`. Nodes it does not reach keep whatever rect they had, so
 * a caller may reuse one rect array across frames.
 *
 * `scratch` holds the per-node line and item tables. Each node hands its
 * scratch back before descending, so the high-water mark tracks the deepest
 * path rather than the node count, and a caller that clears the arena once per
 * frame keeps layout allocation-free overall.
 *
 * Failure policy, identical everywhere the engine can fail: when `scratch`
 * cannot serve a request, that node is laid out as a leaf - its own rect is
 * still written, its subtree keeps the rects it had - and the exhaustion is
 * reported once on CF_LOG_LAYOUT.
 *
 * A NULL tree or rect array is a no-op; a slot outside `node_count` is
 * ignored, which is how a stale id behaves.
 */
void cf_layout(cf_arena *scratch, const cf_tree *tree, uint32_t root, cf_available available,
               cf_point origin, cf_rect *rects);

#endif
