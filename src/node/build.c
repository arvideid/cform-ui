#include <string.h>

#include "cf/build.h"
#include "node/internal.h"

const cf_node *cf_children(cf_ctx *ctx, const cf_node *children, uint32_t child_count) {
    if (ctx == NULL || child_count == 0u) {
        return NULL;
    }

    if (children == NULL) {
        reject_frame(ctx, "a declaration named children it did not hand over");
        return NULL;
    }

    cf_node *copy = cf_alloc(&ctx->frame, (size_t)child_count * sizeof *copy);

    if (copy == NULL) {
        reject_frame(ctx, "the frame arena could not hold the declared children");
        return NULL;
    }

    memcpy(copy, children, (size_t)child_count * sizeof *copy);

    return copy;
}

static cf_node container(cf_ctx *ctx, cf_direction direction, cf_style style,
                         const cf_node *children, uint32_t child_count) {
    if (ctx == NULL) {
        return (cf_node){0};
    }

    cf_node box = {.style = style};
    box.style.direction = direction;
    box.children = cf_children(ctx, children, child_count);
    box.child_count = box.children != NULL ? child_count : 0u;

    return box;
}

cf_node cf_col(cf_ctx *ctx, cf_style style, const cf_node *children, uint32_t child_count) {
    return container(ctx, CF_DIRECTION_COLUMN, style, children, child_count);
}

cf_node cf_row(cf_ctx *ctx, cf_style style, const cf_node *children, uint32_t child_count) {
    return container(ctx, CF_DIRECTION_ROW, style, children, child_count);
}
