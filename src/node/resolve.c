#include "cf/node.h"

#include "node/internal.h"

/*
 * The states booked on the context, read by slot. A disabled box keeps its
 * bit whatever the pointer is doing - the resolver's variant order lets the
 * disabled paint win, so the states below it are still read, not masked.
 */
static uint32_t interact_of(const cf_ctx *ctx, const node_record *record, uint32_t slot) {
    uint32_t states = CF_INTERACT_NONE;

    if (record->disabled) {
        states |= (uint32_t)CF_INTERACT_DISABLED;
    }

    if (cf_hovered(ctx, slot)) {
        states |= (uint32_t)CF_INTERACT_HOVERED;
    }

    if (cf_pressed(ctx, slot)) {
        states |= (uint32_t)CF_INTERACT_PRESSED;
    }

    if (cf_focused(ctx, slot)) {
        states |= (uint32_t)CF_INTERACT_FOCUSED;
    }

    return states;
}

void cf_frame_resolve(cf_ctx *ctx) {
    if (ctx == NULL || ctx->theme == NULL || ctx->layout.styles == NULL) {
        return;
    }

    /* The view's styles are the layout engine's read-only borrow; the
     * resolver is the writer that runs between the diff and the layout, so
     * it takes the one mutable look at the array tree.c filled. */
    cf_style *const styles = (cf_style *)ctx->layout.styles;

    /* Slots the current frame did not claim hold no node: their view styles
     * are stale bytes the resolver must leave alone. */
    const uint32_t extent = (uint32_t)ctx->nodes.extent;

    for (uint32_t slot = 0u; slot < extent; ++slot) {
        const node_record *record = record_of(ctx, slot);

        if (record->touched != ctx->frame_id || record->role == CF_ROLE_NONE) {
            continue;
        }

        const uint32_t states = interact_of(ctx, record, slot);

        styles[slot] = cf_style_varied(styles[slot], &ctx->theme->variants[record->role], states);
    }
}
