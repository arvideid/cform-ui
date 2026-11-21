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
 * The retained slots indexed by (depth, key), rebuilt from the store at the
 * start of every diff. A cell holds slot + 1, so zero ends a probe.
 */
typedef struct match_table {
    uint32_t capacity;
    uint32_t *slots;
    uint32_t *depths;
    cf_name *keys;
} match_table;

static bool slot_is_live(cf_ctx *ctx, uint32_t slot) {
    return cf_store_get(&ctx->nodes, cf_store_id(&ctx->nodes, slot)) != NULL;
}

static uint32_t identity_hash(cf_name key, uint32_t depth) {
    const uint64_t pair = ((uint64_t)key << 32) | depth;
    return cf_hash_bytes(&pair, sizeof pair);
}

static bool table_init(cf_ctx *ctx, match_table *table) {
    uint32_t capacity = 16u;

    while ((uint64_t)capacity < (uint64_t)ctx->nodes.alive * 2u) {
        capacity *= 2u;
    }

    uint32_t *slots = cf_alloc(&ctx->frame, capacity * sizeof *slots);
    uint32_t *depths = cf_alloc(&ctx->frame, capacity * sizeof *depths);
    cf_name *keys = cf_alloc(&ctx->frame, capacity * sizeof *keys);

    if (slots == NULL || depths == NULL || keys == NULL) {
        return false;
    }

    /* Zeroed cells read as empty, and the load stays under half. */
    memset(slots, 0, capacity * sizeof *slots);

    for (uint32_t slot = 0; slot < ctx->nodes.extent; ++slot) {
        const node_record *record = record_of(ctx, slot);

        if (record->key == CF_NO_NAME || !slot_is_live(ctx, slot)) {
            continue;
        }

        uint32_t at = identity_hash(record->key, record->depth) & (capacity - 1u);

        while (slots[at] != 0u) {
            at = (at + 1u) & (capacity - 1u);
        }

        slots[at] = slot + 1u;
        depths[at] = record->depth;
        keys[at] = record->key;
    }

    *table = (match_table){.capacity = capacity, .slots = slots, .depths = depths, .keys = keys};
    return true;
}

static uint32_t table_find(const match_table *table, cf_name key, uint32_t depth) {
    uint32_t at = identity_hash(key, depth) & (table->capacity - 1u);

    while (table->slots[at] != 0u) {
        if (table->keys[at] == key && table->depths[at] == depth) {
            return table->slots[at] - 1u;
        }

        at = (at + 1u) & (table->capacity - 1u);
    }

    return CF_NO_LINK;
}

/* A slot the store hands out that nothing declared yet. CF_NO_LINK when the
 * store cannot grow - the frame is abandoned, reported once. */
static uint32_t claim_fresh(cf_ctx *ctx, const cf_node *decl, uint32_t depth, uint32_t parent) {
    cf_id id;
    node_record *record = cf_store_add(&ctx->nodes, &id);

    if (record == NULL) {
        cf_logf(CF_LOG_CORE, "node: the store cannot hold another node");
        return CF_NO_LINK;
    }

    record->key = decl->key;
    record->depth = depth;
    record->parent = parent;
    record->child_first = CF_NO_LINK;
    record->next_sibling = CF_NO_LINK;
    record->subtree_size = 1u;
    record->touched = ctx->frame_id;

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
 * old occupant, or nothing - the caller claims a fresh slot then. */
static uint32_t resolve_slot(cf_ctx *ctx, const match_table *table, const walk_frame *frame,
                             const cf_node *decl) {
    if (decl->key != CF_NO_NAME) {
        const uint32_t found = table_find(table, decl->key, frame->depth);

        if (found != CF_NO_LINK && record_of(ctx, found)->touched != ctx->frame_id) {
            return found;
        }

        return CF_NO_LINK;
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

    if (root->key != CF_NO_NAME) {
        root_slot = table_find(&table, root->key, 0u);
    } else if (ctx->root_slot != CF_NO_LINK && slot_is_live(ctx, ctx->root_slot)) {
        const node_record *record = record_of(ctx, ctx->root_slot);

        if (record->key == CF_NO_NAME) {
            root_slot = ctx->root_slot;
        }
    }

    if (root_slot == CF_NO_LINK) {
        root_slot = claim_fresh(ctx, root, 0u, CF_NO_LINK);

        if (root_slot == CF_NO_LINK) {
            return CF_NO_LINK;
        }
    }

    node_record *root_record = record_of(ctx, root_slot);

    root_record->key = root->key;
    root_record->depth = 0u;
    root_record->parent = CF_NO_LINK;
    root_record->touched = ctx->frame_id;

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

        uint32_t slot = resolve_slot(ctx, &table, frame, decl);

        if (slot == CF_NO_LINK) {
            slot = claim_fresh(ctx, decl, frame->depth, frame->slot);

            if (slot == CF_NO_LINK) {
                return CF_NO_LINK;
            }
        }

        node_record *record = record_of(ctx, slot);

        record->key = decl->key;
        record->depth = frame->depth;
        record->parent = frame->slot;
        record->touched = ctx->frame_id;

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
