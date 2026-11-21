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
 * frame begins. The persistent arena holds the state blocks cf_state hands
 * out; it is never cleared, so a block outlives every frame and a retired
 * one leaves its bytes orphaned until the context dies.
 *
 * The node store holds the retained tree across frames, its slots addressed
 * by cf_id, so an id held past a rebuild reads as absent rather than as
 * someone else's node. The state store holds one record per state block.
 */
typedef struct cf_ctx {
    cf_arena frame;
    cf_arena persistent;
    cf_interner names;
    cf_store nodes;
    cf_store states;
    /* The view cf_layout reads, rebuilt by every diff into the frame arena. */
    cf_tree layout;
    uint32_t root_slot;
    uint32_t frame_id;
    bool layout_ready;
} cf_ctx;

bool cf_ctx_init(cf_ctx *ctx, size_t frame_bytes, size_t persistent_bytes);
void cf_ctx_free(cf_ctx *ctx);

/*
 * Starts a frame: the frame arena is emptied and the frame counter advances,
 * so a node the coming build does not redeclare is one the next diff
 * retires. A build and its diff run inside a frame.
 */
void cf_frame_begin(cf_ctx *ctx);

/*
 * A block of build state keyed by an interned name. Every request under the
 * same name answers the same memory, whatever the declaration around it
 * does, and the block starts zeroed. A request larger than the one before
 * it moves the block and keeps the bytes it already held, so a pointer into
 * state is valid through the frame that obtained it and the one after.
 *
 * A name the builds stop requesting loses its block one frame later: the
 * next request under it answers fresh, zeroed memory. CF_NO_NAME names no
 * block, a zero size asks for none, and a persistent arena too small for
 * the request all answer NULL.
 */
void *cf_state(cf_ctx *ctx, cf_name key, size_t size);

/*
 * Reconciles a declaration against the retained tree. A node that declares
 * the same depth and key keeps its slot, wherever it now sits; a node
 * without a key keeps the slot its ordinal held last frame; everything else
 * is created, and every retained slot the declaration does not reach is
 * retired. Nodes that share a depth and a key match in declaration order
 * against the slots they share, so duplicates keep the slots they kept and
 * one beyond them is new.
 *
 * Returns the root's slot, CF_NO_LINK when nothing was declared or the
 * frame arena could not serve the walk - the frame then yields no view.
 */
uint32_t cf_diff(cf_ctx *ctx, const cf_node *root);

/* The view cf_layout reads, or NULL before this frame's diff has built one. */
const cf_tree *cf_frame_tree(const cf_ctx *ctx);

#endif
