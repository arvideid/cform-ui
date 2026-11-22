#include <string.h>

#include "cf/node.h"

#include "node/internal.h"

/* The live entry under key, or NULL. The count of state blocks is the count
 * of stateful nodes the build declares - far below the node count - so one
 * sweep of the store per request stays linear in what the frame holds. */
static state_entry *entry_of_key(cf_ctx *ctx, cf_name key) {
    for (uint32_t slot = 0; slot < ctx->states.extent; ++slot) {
        state_entry *entry = cf_store_get(&ctx->states, cf_store_id(&ctx->states, slot));

        if (entry != NULL && entry->key == key) {
            return entry;
        }
    }

    return NULL;
}

/* A fresh block from the persistent arena, zeroed: the zero of a state
 * block is "the build has never seen this name", the one state every
 * caller can read. */
static void *block_alloc(cf_ctx *ctx, size_t size) {
    void *block = cf_alloc(&ctx->persistent, size);

    if (block != NULL) {
        memset(block, 0, size);
    }

    return block;
}

void *cf_state(cf_ctx *ctx, cf_name key, size_t size) {
    if (ctx == NULL || key == CF_NO_NAME || size == 0u) {
        return NULL;
    }

    state_entry *entry = entry_of_key(ctx, key);

    if (entry != NULL) {
        entry->touched = ctx->frame_id;

        if (size <= entry->size) {
            return entry->block;
        }

        /* The block moves in the arena, whose old bytes are then orphaned:
         * the copy hands the build the state it wrote before the request
         * outgrew it. */
        void *grown = block_alloc(ctx, size);

        if (grown == NULL) {
            reject_frame(ctx, "the persistent arena cannot grow a state block");
            return NULL;
        }

        memcpy(grown, entry->block, entry->size);
        entry->block = grown;
        entry->size = size;
        return grown;
    }

    void *block = block_alloc(ctx, size);

    if (block == NULL) {
        reject_frame(ctx, "the persistent arena cannot hold a state block");
        return NULL;
    }

    cf_id id;
    state_entry *fresh = cf_store_add(&ctx->states, &id);

    if (fresh == NULL) {
        reject_frame(ctx, "the state store cannot hold another block");
        return NULL;
    }

    *fresh = (state_entry){.key = key, .block = block, .size = size, .touched = ctx->frame_id};
    return block;
}

/* Runs at cf_frame_begin with the counter already advanced. An entry is
 * kept when the frame that just ended requested it, so a name the builds
 * stop declaring loses its block one frame later and the store hands its
 * slot to the next request. The block's bytes are not freed - the
 * persistent arena has no free - they are orphaned until the context dies. */
void state_gc(cf_ctx *ctx) {
    for (uint32_t slot = 0; slot < ctx->states.extent; ++slot) {
        const cf_id id = cf_store_id(&ctx->states, slot);
        const state_entry *entry = cf_store_get(&ctx->states, id);

        if (entry != NULL && entry->touched != ctx->frame_id - 1u) {
            cf_store_remove(&ctx->states, id);
        }
    }
}
