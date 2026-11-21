#include "cf/node.h"

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

    return true;
}

void cf_ctx_free(cf_ctx *ctx) {
    if (ctx == NULL) {
        return;
    }

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
    ctx->layout_ready = false;
}

const cf_tree *cf_frame_tree(const cf_ctx *ctx) {
    if (ctx == NULL || !ctx->layout_ready) {
        return NULL;
    }

    return &ctx->layout;
}
