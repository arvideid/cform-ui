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

/* A state block is zeroed when it is served: the zero of a block is "the build
 * has never seen this name", the one state every caller can read. */
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

        /* cf_resize_block grows in place where it can and, when it must move
         * the block, hands back the bytes already carried over and the tail
         * zeroed. Copying from the old address after it would read memory
         * realloc has just handed back. A refusal leaves the smaller block
         * whole and still the entry's, so nothing is orphaned either way. */
        void *grown = cf_resize_block(entry->block, entry->size, size);

        if (grown == NULL) {
            reject_frame(ctx, "the heap cannot grow a state block");
            return NULL;
        }

        entry->block = grown;
        entry->size = size;
        return grown;
    }

    void *block = cf_alloc_block(size);

    if (block == NULL) {
        reject_frame(ctx, "the heap cannot hold a state block");
        return NULL;
    }

    cf_id id;
    state_entry *fresh = cf_store_add(&ctx->states, &id);

    if (fresh == NULL) {
        /* The block was already paid for; a store that will not take it hands
         * the bytes back rather than leak them on the way out. */
        cf_free_block(block);
        reject_frame(ctx, "the state store cannot hold another block");
        return NULL;
    }

    *fresh = (state_entry){.key = key, .block = block, .size = size, .touched = ctx->frame_id};
    return block;
}

size_t cf_state_bytes(const cf_ctx *ctx) {
    if (ctx == NULL) {
        return 0u;
    }

    size_t bytes = 0u;

    for (uint32_t slot = 0; slot < ctx->states.extent; ++slot) {
        const state_entry *entry = cf_store_get(&ctx->states, cf_store_id(&ctx->states, slot));

        if (entry != NULL) {
            bytes += entry->size;
        }
    }

    return bytes;
}

/* Runs at cf_frame_begin with the counter already advanced. An entry is
 * kept when the frame that just ended requested it, so a name the builds
 * stop declaring loses its block one frame later: its bytes go back to the
 * heap and the store hands its slot to the next request. */
void state_gc(cf_ctx *ctx) {
    for (uint32_t slot = 0; slot < ctx->states.extent; ++slot) {
        const cf_id id = cf_store_id(&ctx->states, slot);
        const state_entry *entry = cf_store_get(&ctx->states, id);

        if (entry != NULL && entry->touched != ctx->frame_id - 1u) {
            cf_free_block(entry->block);
            cf_store_remove(&ctx->states, id);
        }
    }
}

/* Hands back every block still live when the context dies. The store's own
 * teardown would forget them: they were never its bytes. */
void state_free(cf_ctx *ctx) {
    for (uint32_t slot = 0; slot < ctx->states.extent; ++slot) {
        const cf_id id = cf_store_id(&ctx->states, slot);
        const state_entry *entry = cf_store_get(&ctx->states, id);

        if (entry != NULL) {
            cf_free_block(entry->block);
        }
    }
}
