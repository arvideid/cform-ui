#include <stdlib.h>

#include "internal.h"

UI_Frame *ui_frame(UI_Context *ctx, UI_FrameConfig config) {
    if (ctx == NULL) {
        return NULL;
    }

    UI_Frame *frame = calloc(1, sizeof(UI_Frame));

    if (frame == NULL) {
        return NULL;
    }

    frame->config = config;

    /* Register the frame with the context so it is freed on ui_destroy(). */
    if (ctx->frame_count == ctx->frame_capacity) {
        int capacity = ctx->frame_capacity == 0 ? 8 : ctx->frame_capacity * 2;
        UI_Frame **grown = realloc(ctx->frames, (size_t)capacity * sizeof(UI_Frame *));

        if (grown == NULL) {
            free(frame);
            return NULL;
        }

        ctx->frames = grown;
        ctx->frame_capacity = capacity;
    }

    ctx->frames[ctx->frame_count++] = frame;

    return frame;
}

void ui_add(UI_Frame *parent, UI_Frame *child) {
    if (parent == NULL || child == NULL) {
        return;
    }

    if (parent->child_count == parent->child_capacity) {
        int capacity = parent->child_capacity == 0 ? 4 : parent->child_capacity * 2;
        UI_Frame **grown = realloc(
            parent->children,
            (size_t)capacity * sizeof(UI_Frame *)
        );

        if (grown == NULL) {
            return;
        }

        parent->children = grown;
        parent->child_capacity = capacity;
    }

    parent->children[parent->child_count++] = child;
    child->parent = parent;
}
