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
 * pointers, role, disabled, the child count, the announced role and
 * focusability, the live-region level, the boxes named as related and the grid
 * track arrays by value (a borrowed array whose in-place edits the style bytes
 * alone would miss) - so a diff sees a change without keeping the declaration. The announced words
 * are hashed by value too, because a build that assembles a name into one steady buffer every frame
 * changes the bytes without changing the pointer. `dirty` says this frame changed the node, by that
 * comparison or by hand, and lives until the next frame begins.
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
    /* Booked from the declaration for the bridge walk: what the box is called, in the
     * build's own UTF-8 bytes, borrowed for the frame that declared them the way the
     * measured text beside them is. */
    cf_access access;
    cf_string name;
    cf_string description;
    /* Booked from the declaration: the level a live region covers this box with, as the build
     * stated it. The walk inherits it downward, so only the region's own box carries a level here.
     */
    cf_announce announce;
    /* Booked from the declaration: the keys of the boxes this one is read with. They name boxes
     * rather than point at them, so the walk resolves them against the tree it is already in. */
    cf_name described_by;
    cf_name controls;
    /* Booked from the declaration: the keyboard reaches the boxes that say so,
     * and neither the role nor a handler is the question. */
    bool focusable;
    /* Booked from the declaration: a switch's own state, read by the resolver for the
     * theme's checked variant and by the bridge walk for the state it announces. */
    bool checked;
} node_record;

/* The record a store slot holds. Slots below the store's extent always
 * address memory; whether it belongs to a live node is the store's answer. */
static inline node_record *record_of(const cf_ctx *ctx, uint32_t slot) {
    return cf_store_at(&ctx->nodes, slot);
}

/* A slot whose record the store still hands out: one removed since the last
 * diff keeps its bytes until the arena dies, and answers no lookup. */
static inline bool slot_is_live(const cf_ctx *ctx, uint32_t slot) {
    return cf_store_get(&ctx->nodes, cf_store_id(&ctx->nodes, slot)) != NULL;
}

/*
 * The retained keyed members indexed by their full (depth, key, ordinal)
 * identity, rebuilt from the store at the start of every diff. An ordinal
 * is unique inside its (depth, key) group, so the triple names one record
 * and a take answers in one probe. The consumed count per (depth, key)
 * lives in its own probed array - a group with no members yet has no cell
 * to keep it on - and every take consumes one ordinal, hit or miss.
 */
typedef struct member_cell {
    cf_name key; /* CF_NO_NAME ends a probe */
    uint32_t depth;
    uint32_t ordinal;
    uint32_t slot;
} member_cell;

typedef struct match_table {
    uint32_t member_capacity;
    member_cell *members;
    uint32_t counter_capacity;
    uint32_t counter_used;
    cf_name *counter_keys; /* CF_NO_NAME ends a probe */
    uint32_t *counter_depths;
    uint32_t *counter_consumed;
} match_table;

/* Indexes every live keyed record. False when the frame arena cannot hold the
 * table, which leaves the frame without a tree. */
bool match_index(cf_ctx *ctx, match_table *table);

/*
 * The slot a declaration of this depth and key matches: the kept member whose
 * ordinal is the group's consumed count, or CF_NO_LINK - the caller then
 * claims a fresh member at that ordinal, which `out_ordinal` reports either
 * way.
 */
uint32_t match_take(cf_ctx *ctx, match_table *table, cf_name key, uint32_t depth,
                    uint32_t *out_ordinal);

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
 * Brings the box at this slot inside every scrolling pane above it, measured against the rects
 * the frame last laid out. The keyboard's walk calls it where it lands, and the layout pass
 * honours a reveal a build asked for with it. The rects have to exist: cf_reveal is the public
 * door that waits for them.
 */
void cf_reveal_slot(cf_ctx *ctx, uint32_t slot);

/*
 * Puts the keyboard back where it came from when the box holding it left the view -
 * retired, hidden or switched off - so a diff that ends a dialog does not end the
 * page's ability to answer a key. Called once the new view exists, because the
 * box's reachability is read from it.
 */
void cf_focus_repair(cf_ctx *ctx);

/*
 * The booked interaction state, read by this frame's array slot. The resolver walks every node of
 * the view and asks about each in turn, so it reads the state where it is held instead of turning
 * each slot into a reference on the way past; anyone outside the frame asks the public query by id.
 */
bool slot_hovered(const cf_ctx *ctx, uint32_t slot);
bool slot_pressed(const cf_ctx *ctx, uint32_t slot);
bool slot_focused(const cf_ctx *ctx, uint32_t slot);

/*
 * One state block, addressed by store slot. The block's bytes are the heap's and
 * the entry owns them: retiring the record hands the bytes back, and the store
 * gives the slot to the next request.
 */
typedef struct state_entry {
    cf_name key;
    void *block;
    size_t size;
    uint32_t touched;
} state_entry;

/* Drops every entry the frame that just ended did not request. */
void state_gc(cf_ctx *ctx);

/* Hands back every block the context dies holding. */
void state_free(cf_ctx *ctx);

/*
 * States why this frame could not be completed: the console reads it as it
 * happens, and `cf_error_message` answers it to the program that has to decide
 * what to draw instead. The message is a literal naming the refused request.
 */
void reject_frame(cf_ctx *ctx, const char *message);

#endif
