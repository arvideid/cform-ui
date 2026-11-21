#ifndef CF_NODE_H
#define CF_NODE_H

#include "cf/base.h"
#include "cf/layout.h"

/*
 * One declared node: a value the build assembles and the diff reads, never
 * the engine's to keep. `children` and `measure_data` are borrowed for the
 * diff call alone.
 *
 * `key` is the node's identity, an interned name that every build meaning
 * the same node reuses. A node without a key is reidentified by position:
 * the child that sat at its ordinal under the same parent keeps the slot.
 */
typedef struct cf_node {
    cf_name key;
    cf_style style;
    cf_measure_fn measure;
    void *measure_data;
    const struct cf_node *children;
    uint32_t child_count;
} cf_node;

/*
 * The context a build declares into and the diff reconciles against. The
 * frame arena holds everything one frame builds and reads - the view the
 * diff hands cf_layout, the walk's scratch - and is emptied when the next
 * frame begins. The node store holds the retained tree across frames, its
 * slots addressed by cf_id, so an id held past a rebuild reads as absent
 * rather than as someone else's node.
 */
typedef struct cf_ctx {
    cf_arena frame;
    cf_interner names;
    cf_store nodes;
    /* The view cf_layout reads, rebuilt by every diff into the frame arena. */
    cf_tree layout;
    uint32_t root_slot;
    uint32_t frame_id;
    bool layout_ready;
} cf_ctx;

bool cf_ctx_init(cf_ctx *ctx, size_t frame_bytes);
void cf_ctx_free(cf_ctx *ctx);

/*
 * Starts a frame: the frame arena is emptied and the frame counter advances,
 * so a node the coming build does not redeclare is one the next diff
 * retires. A build and its diff run inside a frame.
 */
void cf_frame_begin(cf_ctx *ctx);

/*
 * Reconciles a declaration against the retained tree. A node that declares
 * the same depth and key keeps its slot, wherever it now sits; a node
 * without a key keeps the slot its ordinal held last frame; everything else
 * is created, and every retained slot the declaration does not reach is
 * retired. Two nodes that share a depth and a key are two nodes: the second
 * is new.
 *
 * Returns the root's slot, CF_NO_LINK when nothing was declared or the
 * frame arena could not serve the walk - the frame then yields no view.
 */
uint32_t cf_diff(cf_ctx *ctx, const cf_node *root);

/* The view cf_layout reads, or NULL before this frame's diff has built one. */
const cf_tree *cf_frame_tree(const cf_ctx *ctx);

#endif
