#include "cf/node.h"

#include <string.h>

#include "node/internal.h"

bool cf_ctx_init(cf_ctx *ctx, size_t frame_bytes) {
    if (ctx == NULL || frame_bytes == 0u) {
        return false;
    }

    *ctx = (cf_ctx){0};

    if (!cf_arena_init(&ctx->frame, frame_bytes)) {
        return false;
    }

    if (!cf_interner_init(&ctx->names, 0u)) {
        cf_arena_free(&ctx->frame);
        *ctx = (cf_ctx){0};
        return false;
    }

    if (!cf_store_init(&ctx->nodes, sizeof(node_record), 0u)) {
        cf_interner_free(&ctx->names);
        cf_arena_free(&ctx->frame);
        *ctx = (cf_ctx){0};
        return false;
    }

    if (!cf_store_init(&ctx->states, sizeof(state_entry), 0u)) {
        cf_store_free(&ctx->nodes);
        cf_interner_free(&ctx->names);
        cf_arena_free(&ctx->frame);
        *ctx = (cf_ctx){0};
        return false;
    }

    /* An id of zeroed bytes is a valid slot; the interaction state starts
     * naming nothing. */
    ctx->hover_id = CF_NO_ID;
    ctx->press_id = CF_NO_ID;
    ctx->capture_id = CF_NO_ID;
    ctx->focus_id = CF_NO_ID;

    return true;
}

void cf_ctx_free(cf_ctx *ctx) {
    if (ctx == NULL) {
        return;
    }

    /* The blocks outlive the store that indexes them, so they go first: after
     * cf_store_free the records are gone and nothing remembers them. */
    state_free(ctx);
    cf_store_free(&ctx->states);
    cf_store_free(&ctx->nodes);
    cf_interner_free(&ctx->names);
    cf_arena_free(&ctx->frame);
    *ctx = (cf_ctx){0};
}

void cf_frame_begin(cf_ctx *ctx) {
    if (ctx == NULL) {
        return;
    }

    cf_arena_clear(&ctx->frame);
    ctx->frame_id++;
    state_gc(ctx);
    ctx->layout_ready = false;
    /* The frame's own two answers start fresh: no interval until the run
     * states one, no ask until a build makes one. */
    ctx->frame_seconds = 0.0f;
    ctx->frame_requested = false;
    /* A refusal belongs to the frame that made it; the rects lived in the
     * arena just cleared. */
    ctx->rejection = NULL;
    ctx->rects = NULL;

    /* The dirty bits answer one frame's changes; the coming build's diff
     * sets them again where the declaration moved. */
    for (uint32_t slot = 0; slot < ctx->nodes.extent; ++slot) {
        record_of(ctx, slot)->dirty = false;
    }
}

const cf_tree *cf_frame_tree(const cf_ctx *ctx) {
    if (ctx == NULL || !ctx->layout_ready) {
        return NULL;
    }

    return &ctx->layout;
}

void cf_frame_layout(cf_ctx *ctx, cf_available available, cf_point origin) {
    if (ctx == NULL || !ctx->layout_ready || ctx->root_slot == CF_NO_LINK) {
        return;
    }

    /* One rect per store slot. The array is zeroed before cf_layout writes into
     * it because a scratch refusal inside a container leaves the subtrees under
     * it unwritten, and a paint or an input walk reading a stale rect would
     * otherwise reach whatever the last frame had there. A zeroed cf_rect is a
     * valid empty box - it paints nothing and hits nothing - so an incomplete
     * pass still answers with a walk the caller can use. */
    cf_rect *rects = cf_alloc(&ctx->frame, ctx->layout.node_count * sizeof *rects);

    if (rects == NULL) {
        reject_frame(ctx, "the frame arena could not hold the layout rects");
        return;
    }

    memset(rects, 0, ctx->layout.node_count * sizeof *rects);
    cf_layout(&ctx->frame, &ctx->layout, ctx->root_slot, available, origin, rects);
    ctx->rects = rects;
}

const char *cf_error_message(const cf_ctx *ctx) {
    return ctx == NULL ? NULL : ctx->rejection;
}

void reject_frame(cf_ctx *ctx, const char *message) {
    ctx->rejection = message;
    cf_logf(CF_LOG_CORE, "node: %s", message);
}

bool cf_dirty(const cf_ctx *ctx, uint32_t slot) {
    if (ctx == NULL) {
        return false;
    }

    const node_record *record = cf_store_get(&ctx->nodes, cf_store_id(&ctx->nodes, slot));

    return record != NULL && record->dirty;
}

/* Marks the subtree under the slot; records cannot cycle, so the walk
 * visits each descendant once. */
static void dirty_walk(cf_ctx *ctx, uint32_t slot) {
    node_record *record = record_of(ctx, slot);

    record->dirty = true;

    for (uint32_t child = record->child_first; child != CF_NO_LINK;
         child = record_of(ctx, child)->next_sibling) {
        dirty_walk(ctx, child);
    }
}

void cf_dirty_subtree(cf_ctx *ctx, uint32_t slot) {
    if (ctx == NULL || cf_store_get(&ctx->nodes, cf_store_id(&ctx->nodes, slot)) == NULL) {
        return;
    }

    dirty_walk(ctx, slot);
}
