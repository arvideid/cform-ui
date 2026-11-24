#include "cf/node.h"

#include "node/internal.h"

bool cf_ctx_init(cf_ctx *ctx, size_t frame_bytes, size_t persistent_bytes) {
    if (ctx == NULL || frame_bytes == 0u || persistent_bytes == 0u) {
        return false;
    }

    *ctx = (cf_ctx){0};

    if (!cf_arena_init(&ctx->frame, frame_bytes)) {
        return false;
    }

    if (!cf_arena_init(&ctx->persistent, persistent_bytes)) {
        cf_arena_free(&ctx->frame);
        *ctx = (cf_ctx){0};
        return false;
    }

    if (!cf_interner_init(&ctx->names, 0u)) {
        cf_arena_free(&ctx->persistent);
        cf_arena_free(&ctx->frame);
        *ctx = (cf_ctx){0};
        return false;
    }

    if (!cf_store_init(&ctx->nodes, sizeof(node_record), 0u)) {
        cf_interner_free(&ctx->names);
        cf_arena_free(&ctx->persistent);
        cf_arena_free(&ctx->frame);
        *ctx = (cf_ctx){0};
        return false;
    }

    if (!cf_store_init(&ctx->states, sizeof(state_entry), 0u)) {
        cf_store_free(&ctx->nodes);
        cf_interner_free(&ctx->names);
        cf_arena_free(&ctx->persistent);
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

    cf_store_free(&ctx->states);
    cf_store_free(&ctx->nodes);
    cf_interner_free(&ctx->names);
    cf_arena_free(&ctx->persistent);
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
    /* The frame covers no interval until the run that begins it states one. */
    ctx->frame_seconds = 0.0f;
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

    /* One rect per store slot; the walk from the root only ever reads the
     * slots it reaches, so the rest staying untouched is harmless. */
    cf_rect *rects = cf_alloc(&ctx->frame, ctx->layout.node_count * sizeof *rects);

    if (rects == NULL) {
        reject_frame(ctx, "the frame arena could not hold the layout rects");
        return;
    }

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
