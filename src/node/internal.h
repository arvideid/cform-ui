#ifndef CFORM_NODE_INTERNAL_H
#define CFORM_NODE_INTERNAL_H

#include "cf/node.h"

/*
 * One retained node, addressed by store slot. Identity is the (depth, key)
 * pair - or, without a key, the ordinal under the parent - so depth strictly
 * increases along every parent chain and the root's parent is CF_NO_LINK: a
 * cycle would need a depth that does not increase, which this tree cannot
 * express.
 *
 * `ordinal` is the slot's place among the records that share its depth and
 * key, fixed when the slot joined the group: a declaration's n-th occurrence
 * meets the member with ordinal n, so duplicates keep the slots they kept
 * however the group grows.
 *
 * `touched` is the frame id of the last diff that claimed the slot. A slot
 * the current frame did not claim is dead or stale, and the store makes
 * removing either a no-op.
 *
 * `handlers` is the declaration's current pair, refreshed by every diff
 * that claims the slot and read by dispatch alone.
 *
 * `role` and `disabled` ride the same refresh: the theme resolver reads
 * them off the retained record after the diff, when the declaration that
 * stated them is already gone.
 *
 * `signature` hashes what the last claim declared - style, measure, handler
 * pointers, role, disabled and the child count - so a diff sees a change
 * without keeping the declaration. `dirty` says this frame changed the node,
 * by that comparison or by hand, and lives until the next frame begins.
 */
typedef struct node_record {
    cf_name key;
    uint32_t depth;
    uint32_t parent;
    uint32_t child_first;
    uint32_t next_sibling;
    uint32_t subtree_size;
    uint32_t ordinal;
    uint32_t touched;
    uint32_t signature;
    bool dirty;
    cf_handlers handlers;
    cf_role role;
    bool disabled;
} node_record;

/* The record a store slot holds. Slots below the store's extent always
 * address memory; whether it belongs to a live node is the store's answer. */
static inline node_record *record_of(const cf_ctx *ctx, uint32_t slot) {
    return cf_store_at(&ctx->nodes, slot);
}

/* One declared node resolved to the slot it keeps this frame, in preorder. */
typedef struct claim_entry {
    uint32_t slot;
    const cf_node *decl;
} claim_entry;

/*
 * Writes the structure the claimed declaration describes: sibling chains,
 * subtree sizes, the borrowed styles, and the compressed child rows
 * cf_layout reads. False when the frame arena cannot hold the view; the
 * frame then yields none.
 */
bool tree_rebuild(cf_ctx *ctx, const claim_entry *flat, uint32_t declared);

/*
 * One state block, addressed by store slot. The block's bytes sit in the
 * persistent arena and are orphaned when the entry retires; the record is
 * what retires, and the store hands its slot to the next request.
 */
typedef struct state_entry {
    cf_name key;
    void *block;
    size_t size;
    uint32_t touched;
} state_entry;

/* Drops every entry the frame that just ended did not request. */
void state_gc(cf_ctx *ctx);

/*
 * States why this frame could not be completed: the console reads it as it
 * happens, and `cf_error_message` answers it to the program that has to decide
 * what to draw instead. The message is a literal naming the refused request.
 */
void reject_frame(cf_ctx *ctx, const char *message);

#endif
