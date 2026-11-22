#include <string.h>

#include "cf/node.h"

#include "node/internal.h"

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

bool match_index(cf_ctx *ctx, match_table *table) {
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

uint32_t match_take(cf_ctx *ctx, match_table *table, cf_name key, uint32_t depth,
                    uint32_t *out_ordinal) {
    if ((uint64_t)table->counter_used * 2u >= table->counter_capacity &&
        !counters_grow(ctx, table)) {
        reject_frame(ctx, "the frame arena cannot index the declaration's keys");
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
