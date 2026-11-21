#include <string.h>

#include "cf/node.h"

#include "node/internal.h"

/* The declaration's claims and the walk's open parents are collected in
 * arena chunks. Nothing is ever rewound inside a diff - a pop would lower
 * the bump pointer below memory a chunk still links - so a walk's memory
 * accumulates to its high-water mark and dies with the frame's arena. */
#define CLAIM_CHUNK_ENTRIES 64
#define WALK_CHUNK_ENTRIES 32

typedef struct claim_chunk {
    struct claim_chunk *next;
    uint32_t count;
    claim_entry entries[CLAIM_CHUNK_ENTRIES];
} claim_chunk;

/*
 * One open parent in the declaration walk: the parent being iterated, the
 * slot it kept, the depth its children sit at, and the retained slot the
 * next keyless child compares against.
 */
typedef struct walk_frame {
    const cf_node *parent;
    uint32_t slot;
    uint32_t depth;
    uint32_t next_child;
    uint32_t cursor;
} walk_frame;

typedef struct walk_chunk {
    struct walk_chunk *next;
    uint32_t count;
    walk_frame frames[WALK_CHUNK_ENTRIES];
} walk_chunk;

/* A stack of chunks: the active chain holds the frames, and emptied chunks
 * are kept on the spare list to be filled again before allocating new ones. */
typedef struct walk_stack {
    walk_chunk *active;
    walk_chunk *spare;
} walk_stack;

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

static bool slot_is_live(cf_ctx *ctx, uint32_t slot) {
    return cf_store_get(&ctx->nodes, cf_store_id(&ctx->nodes, slot)) != NULL;
}

static uint32_t identity_hash(cf_name key, uint32_t depth) {
    const uint64_t pair = ((uint64_t)key << 32) | depth;
    return cf_hash_bytes(&pair, sizeof pair);
}

/* The full identity of a keyed member. The staging bytes start zeroed so
 * the alignment gaps ride the same hash every frame. */
static uint32_t member_hash(cf_name key, uint32_t depth, uint32_t ordinal) {
    struct {
        cf_name key;
        uint32_t depth;
        uint32_t ordinal;
    } identity;

    memset(&identity, 0, sizeof identity);

    identity.key = key;
    identity.depth = depth;
    identity.ordinal = ordinal;

    return cf_hash_bytes(&identity, sizeof identity);
}

/* What a change dirties: the style, the measure and handlers the build
 * attached, and the shape of the child list. The measure's data is
 * borrowed and unread, so it is none of the comparison's business, and
 * the staging bytes start zeroed so the alignment gaps ride the same
 * hash every frame. */
static uint32_t signature_of(const cf_node *decl) {
    struct {
        cf_style style;
        cf_measure_fn measure;
        cf_handlers handlers;
        uint32_t child_count;
    } declared;

    memset(&declared, 0, sizeof declared);

    declared.style = decl->style;
    declared.measure = decl->measure;
    declared.handlers = decl->handlers;
    declared.child_count = decl->child_count;

    return cf_hash_bytes(&declared, sizeof declared);
}

static bool table_init(cf_ctx *ctx, match_table *table) {
    uint32_t capacity = 16u;

    while ((uint64_t)capacity < (uint64_t)ctx->nodes.alive * 2u) {
        capacity *= 2u;
    }

    member_cell *members = cf_alloc(&ctx->frame, capacity * sizeof *members);
    cf_name *counter_keys = cf_alloc(&ctx->frame, capacity * sizeof *counter_keys);
    uint32_t *counter_depths = cf_alloc(&ctx->frame, capacity * sizeof *counter_depths);
    uint32_t *counter_consumed = cf_alloc(&ctx->frame, capacity * sizeof *counter_consumed);

    if (members == NULL || counter_keys == NULL || counter_depths == NULL ||
        counter_consumed == NULL) {
        return false;
    }

    /* Zeroed keys read as empty on both tables; the counts start at zero
     * takes. */
    memset(members, 0, capacity * sizeof *members);
    memset(counter_keys, 0, capacity * sizeof *counter_keys);
    memset(counter_consumed, 0, capacity * sizeof *counter_consumed);

    for (uint32_t slot = 0; slot < ctx->nodes.extent; ++slot) {
        const node_record *record = record_of(ctx, slot);

        if (record->key == CF_NO_NAME || !slot_is_live(ctx, slot)) {
            continue;
        }

        uint32_t at = member_hash(record->key, record->depth, record->ordinal) & (capacity - 1u);

        while (members[at].key != CF_NO_NAME) {
            at = (at + 1u) & (capacity - 1u);
        }

        members[at] = (member_cell){
            .key = record->key, .depth = record->depth, .ordinal = record->ordinal, .slot = slot};
    }

    *table = (match_table){.member_capacity = capacity,
                           .members = members,
                           .counter_capacity = capacity,
                           .counter_used = 0u,
                           .counter_keys = counter_keys,
                           .counter_depths = counter_depths,
                           .counter_consumed = counter_consumed};
    return true;
}

/* A frame may declare groups the store has never held, so the counter
 * array grows with them instead of sizing from the retained tree. */
static bool counters_grow(cf_ctx *ctx, match_table *table) {
    const uint32_t capacity = table->counter_capacity * 2u;

    cf_name *keys = cf_alloc(&ctx->frame, capacity * sizeof *keys);
    uint32_t *depths = cf_alloc(&ctx->frame, capacity * sizeof *depths);
    uint32_t *consumed = cf_alloc(&ctx->frame, capacity * sizeof *consumed);

    if (keys == NULL || depths == NULL || consumed == NULL) {
        return false;
    }

    memset(keys, 0, capacity * sizeof *keys);
    memset(consumed, 0, capacity * sizeof *consumed);

    for (uint32_t from = 0u; from < table->counter_capacity; ++from) {
        if (table->counter_keys[from] == CF_NO_NAME) {
            continue;
        }

        uint32_t into =
            identity_hash(table->counter_keys[from], table->counter_depths[from]) & (capacity - 1u);

        while (keys[into] != CF_NO_NAME) {
            into = (into + 1u) & (capacity - 1u);
        }

        keys[into] = table->counter_keys[from];
        depths[into] = table->counter_depths[from];
        consumed[into] = table->counter_consumed[from];
    }

    table->counter_keys = keys;
    table->counter_depths = depths;
    table->counter_consumed = consumed;
    table->counter_capacity = capacity;
    return true;
}

/* The slot a declaration of this depth and key matches: the kept member
 * whose ordinal is the group's consumed count, or CF_NO_LINK - the caller
 * then claims a fresh member at that ordinal. */
static uint32_t table_take(cf_ctx *ctx, match_table *table, cf_name key, uint32_t depth,
                           uint32_t *out_ordinal) {
    if ((uint64_t)table->counter_used * 2u >= table->counter_capacity &&
        !counters_grow(ctx, table)) {
        cf_logf(CF_LOG_CORE, "node: the frame arena cannot index the declaration's keys");
        *out_ordinal = 0u;
        return CF_NO_LINK;
    }

    const uint32_t hash = identity_hash(key, depth);
    uint32_t at = hash & (table->counter_capacity - 1u);

    while (table->counter_keys[at] != CF_NO_NAME &&
           !(table->counter_keys[at] == key && table->counter_depths[at] == depth)) {
        at = (at + 1u) & (table->counter_capacity - 1u);
    }

    if (table->counter_keys[at] == CF_NO_NAME) {
        table->counter_keys[at] = key;
        table->counter_depths[at] = depth;
        table->counter_used++;
    }

    const uint32_t taken = table->counter_consumed[at];
    uint32_t candidate = CF_NO_LINK;

    uint32_t probe = member_hash(key, depth, taken) & (table->member_capacity - 1u);

    while (table->members[probe].key != CF_NO_NAME) {
        const member_cell *cell = &table->members[probe];

        if (cell->key == key && cell->depth == depth && cell->ordinal == taken) {
            candidate = cell->slot;
            break;
        }

        probe = (probe + 1u) & (table->member_capacity - 1u);
    }

    table->counter_consumed[at] = taken + 1u;
    *out_ordinal = taken;
    return candidate;
}

/* A slot the store hands out that nothing declared yet. CF_NO_LINK when the
 * store cannot grow - the frame is abandoned, reported once. */
static uint32_t claim_fresh(cf_ctx *ctx, const cf_node *decl, uint32_t depth, uint32_t parent,
                            uint32_t ordinal) {
    cf_id id;
    node_record *record = cf_store_add(&ctx->nodes, &id);

    if (record == NULL) {
        cf_logf(CF_LOG_CORE, "node: the store cannot hold another node");
        return CF_NO_LINK;
    }

    record->key = decl->key;
    record->depth = depth;
    record->parent = parent;
    record->ordinal = ordinal;
    record->child_first = CF_NO_LINK;
    record->next_sibling = CF_NO_LINK;
    record->subtree_size = 1u;
    record->touched = ctx->frame_id;
    /* A slot the store just handed out was not there last frame; whatever
     * sits at it now is new to every system that reads dirty bits. */
    record->dirty = true;

    return id.index;
}

static bool walk_push(cf_ctx *ctx, walk_stack *stack, const cf_node *parent, uint32_t slot,
                      uint32_t depth, uint32_t cursor) {
    walk_chunk *chunk = stack->active;

    if (chunk == NULL || chunk->count == WALK_CHUNK_ENTRIES) {
        chunk = stack->spare;

        if (chunk != NULL) {
            stack->spare = chunk->next;
        } else {
            chunk = cf_alloc(&ctx->frame, sizeof *chunk);

            if (chunk == NULL) {
                return false;
            }
        }

        chunk->next = stack->active;
        chunk->count = 0u;
        stack->active = chunk;
    }

    chunk->frames[chunk->count] = (walk_frame){
        .parent = parent, .slot = slot, .depth = depth, .next_child = 0u, .cursor = cursor};
    chunk->count++;
    return true;
}

static void walk_pop(walk_stack *stack) {
    walk_chunk *chunk = stack->active;

    chunk->count--;

    if (chunk->count == 0u) {
        stack->active = chunk->next;
        chunk->next = stack->spare;
        stack->spare = chunk;
    }
}

static bool claim_push(cf_ctx *ctx, claim_chunk **head, uint32_t slot, const cf_node *decl) {
    claim_chunk *chunk = *head;

    if (chunk == NULL || chunk->count == CLAIM_CHUNK_ENTRIES) {
        chunk = cf_alloc(&ctx->frame, sizeof *chunk);

        if (chunk == NULL) {
            return false;
        }

        chunk->next = *head;
        chunk->count = 0u;
        *head = chunk;
    }

    chunk->entries[chunk->count] = (claim_entry){.slot = slot, .decl = decl};
    chunk->count++;
    return true;
}

static claim_entry *claim_flatten(cf_ctx *ctx, const claim_chunk *head, uint32_t count) {
    claim_entry *flat = cf_alloc(&ctx->frame, count * sizeof *flat);

    if (flat == NULL) {
        return NULL;
    }

    uint32_t at = count;

    for (const claim_chunk *chunk = head; chunk != NULL; chunk = chunk->next) {
        for (uint32_t i = chunk->count; i-- > 0u;) {
            flat[--at] = chunk->entries[i];
        }
    }

    return flat;
}

static void retire_untouched(cf_ctx *ctx) {
    cf_store *store = &ctx->nodes;

    for (uint32_t slot = 0; slot < store->extent; ++slot) {
        const node_record *record = record_of(ctx, slot);

        if (record->touched != ctx->frame_id) {
            cf_store_remove(store, cf_store_id(store, slot));
        }
    }
}

/* One retained slot for a declared node: the keyed match, or the ordinal's
 * old occupant, or nothing - the caller claims a fresh slot then. The
 * ordinal of a keyed match rides out for the fresh claim to reuse. */
static uint32_t resolve_slot(cf_ctx *ctx, match_table *table, const walk_frame *frame,
                             const cf_node *decl, uint32_t *out_ordinal) {
    *out_ordinal = 0u;

    if (decl->key != CF_NO_NAME) {
        return table_take(ctx, table, decl->key, frame->depth, out_ordinal);
    }

    if (frame->cursor == CF_NO_LINK) {
        return CF_NO_LINK;
    }

    const node_record *candidate = record_of(ctx, frame->cursor);

    /* A keyless child continues the keyless child that held its ordinal: the
     * key has to agree, and the slot must still be free this frame. */
    if (candidate->key != CF_NO_NAME || candidate->touched == ctx->frame_id) {
        return CF_NO_LINK;
    }

    return frame->cursor;
}

uint32_t cf_diff(cf_ctx *ctx, const cf_node *root) {
    if (ctx == NULL) {
        return CF_NO_LINK;
    }

    if (root == NULL) {
        /* Nothing was declared: the retained tree empties. */
        retire_untouched(ctx);
        ctx->root_slot = CF_NO_LINK;
        ctx->layout_ready = false;
        return CF_NO_LINK;
    }

    match_table table = {0};

    if (!table_init(ctx, &table)) {
        cf_logf(CF_LOG_CORE, "node: the frame arena cannot index the retained tree");
        return CF_NO_LINK;
    }

    /* The root matches by key at depth zero, or by its own last slot when
     * neither root declared a key. */
    uint32_t root_slot = CF_NO_LINK;
    uint32_t root_ordinal = 0u;

    if (root->key != CF_NO_NAME) {
        root_slot = table_take(ctx, &table, root->key, 0u, &root_ordinal);
    } else if (ctx->root_slot != CF_NO_LINK && slot_is_live(ctx, ctx->root_slot)) {
        const node_record *record = record_of(ctx, ctx->root_slot);

        if (record->key == CF_NO_NAME) {
            root_slot = ctx->root_slot;
        }
    }

    if (root_slot == CF_NO_LINK) {
        root_slot = claim_fresh(ctx, root, 0u, CF_NO_LINK, root_ordinal);

        if (root_slot == CF_NO_LINK) {
            return CF_NO_LINK;
        }
    }

    node_record *root_record = record_of(ctx, root_slot);

    root_record->key = root->key;
    root_record->depth = 0u;
    root_record->parent = CF_NO_LINK;
    root_record->ordinal = root_ordinal;
    root_record->touched = ctx->frame_id;
    root_record->handlers = root->handlers;

    const uint32_t root_signature = signature_of(root);

    root_record->dirty = root_record->dirty || root_record->signature != root_signature;
    root_record->signature = root_signature;

    claim_chunk *claimed = NULL;
    walk_stack stack = {0};

    if (!walk_push(ctx, &stack, root, root_slot, 1u, root_record->child_first) ||
        !claim_push(ctx, &claimed, root_slot, root)) {
        cf_logf(CF_LOG_CORE, "node: the frame arena cannot walk the declaration");
        return CF_NO_LINK;
    }

    while (stack.active != NULL) {
        walk_chunk *chunk = stack.active;
        walk_frame *frame = &chunk->frames[chunk->count - 1u];

        if (frame->next_child >= frame->parent->child_count || frame->parent->children == NULL) {
            walk_pop(&stack);
            continue;
        }

        const cf_node *decl = &frame->parent->children[frame->next_child];
        frame->next_child++;

        uint32_t ordinal = 0u;
        uint32_t slot = resolve_slot(ctx, &table, frame, decl, &ordinal);

        if (slot == CF_NO_LINK) {
            slot = claim_fresh(ctx, decl, frame->depth, frame->slot, ordinal);

            if (slot == CF_NO_LINK) {
                return CF_NO_LINK;
            }
        }

        node_record *record = record_of(ctx, slot);

        record->key = decl->key;
        record->depth = frame->depth;
        record->parent = frame->slot;
        record->ordinal = ordinal;
        record->touched = ctx->frame_id;
        record->handlers = decl->handlers;

        const uint32_t signature = signature_of(decl);

        /* A mark made earlier this frame survives an unchanged claim, and
         * a fresh slot arrives dirty from the claim itself. */
        record->dirty = record->dirty || record->signature != signature;
        record->signature = signature;

        if (!claim_push(ctx, &claimed, slot, decl)) {
            cf_logf(CF_LOG_CORE, "node: the frame arena cannot record the declaration");
            return CF_NO_LINK;
        }

        /* Every declared child consumes one ordinal, keyed or not, so the
         * cursor moves down last frame's chain once per child. */
        if (frame->cursor != CF_NO_LINK) {
            frame->cursor = record_of(ctx, frame->cursor)->next_sibling;
        }

        if (decl->child_count > 0u) {
            if (!walk_push(ctx, &stack, decl, slot, frame->depth + 1u,
                           record_of(ctx, slot)->child_first)) {
                cf_logf(CF_LOG_CORE, "node: the frame arena cannot walk the declaration");
                return CF_NO_LINK;
            }
        }
    }

    retire_untouched(ctx);

    uint32_t declared = 0;

    for (const claim_chunk *chunk = claimed; chunk != NULL; chunk = chunk->next) {
        declared += chunk->count;
    }

    claim_entry *flat = claim_flatten(ctx, claimed, declared);

    if (flat == NULL) {
        cf_logf(CF_LOG_CORE, "node: the frame arena cannot record the declaration");
        return CF_NO_LINK;
    }

    if (!tree_rebuild(ctx, flat, declared)) {
        return CF_NO_LINK;
    }

    return ctx->root_slot;
}
