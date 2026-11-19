#include <stdlib.h>

#include "internal.h"

UI_Context *ui_create(void) {
    UI_Context *ctx = calloc(1, sizeof(UI_Context));

    if (ctx == NULL) {
        return NULL;
    }

    return ctx;
}

void ui_destroy(UI_Context *ctx) {
    if (ctx == NULL) {
        return;
    }

    for (int i = 0; i < ctx->frame_count; i++) {
        free(ctx->frames[i]->children);
        free(ctx->frames[i]);
    }

    free(ctx->frames);
    free(ctx);
}
