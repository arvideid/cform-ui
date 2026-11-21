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
 * `touched` is the frame id of the last diff that claimed the slot. A slot
 * the current frame did not claim is dead or stale, and the store makes
 * removing either a no-op.
 */
typedef struct node_record {
    cf_name key;
    uint32_t depth;
    uint32_t parent;
    uint32_t child_first;
    uint32_t next_sibling;
    uint32_t subtree_size;
    uint32_t touched;
} node_record;

/* The record a store slot holds. Slots below the store's extent always
 * address memory; whether it belongs to a live node is the store's answer. */
static inline node_record *record_of(cf_ctx *ctx, uint32_t slot) {
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

#endif
