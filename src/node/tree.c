#include <string.h>

#include "cf/node.h"

#include "node/internal.h"

bool tree_rebuild(cf_ctx *ctx, const claim_entry *flat, uint32_t declared) {
    /* The store refuses a slot above 32 bits, so its extent always narrows. */
    const uint32_t extent = (uint32_t)ctx->nodes.extent;

    cf_style *styles = cf_alloc(&ctx->frame, extent * sizeof *styles);
    cf_measure_fn *measures = cf_alloc(&ctx->frame, extent * sizeof *measures);
    void **measure_data = cf_alloc(&ctx->frame, extent * sizeof *measure_data);
    uint32_t *child_counts = cf_alloc(&ctx->frame, extent * sizeof *child_counts);
    uint32_t *last_child = cf_alloc(&ctx->frame, extent * sizeof *last_child);
    uint32_t *sizes = cf_alloc(&ctx->frame, extent * sizeof *sizes);
    uint32_t *child_start = cf_alloc(&ctx->frame, (extent + 1u) * sizeof *child_start);
    uint32_t *child_slots =
        cf_alloc(&ctx->frame, (declared > 1u ? declared - 1u : 1u) * sizeof *child_slots);

    if (styles == NULL || measures == NULL || measure_data == NULL || child_counts == NULL ||
        last_child == NULL || sizes == NULL || child_start == NULL || child_slots == NULL) {
        reject_frame(ctx, "the frame arena cannot hold the declared view");
        return false;
    }

    memset(child_counts, 0, extent * sizeof *child_counts);
    memset(last_child, 0xFF, extent * sizeof *last_child);
    memset(sizes, 0, extent * sizeof *sizes);

    /* The chains are rebuilt from this frame's claims alone: a slot that was
     * a middle child last frame still carries the sibling it pointed at, and
     * a retired one still carries a whole subtree. */
    for (uint32_t i = 0; i < declared; ++i) {
        node_record *record = record_of(ctx, flat[i].slot);

        record->child_first = CF_NO_LINK;
        record->next_sibling = CF_NO_LINK;
    }

    /* Sibling chains are rebuilt from the declaration's order, which is also
     * paint order. */
    for (uint32_t i = 0; i < declared; ++i) {
        const uint32_t slot = flat[i].slot;
        const uint32_t parent = record_of(ctx, slot)->parent;

        if (parent == CF_NO_LINK) {
            continue;
        }

        child_counts[parent]++;

        if (last_child[parent] == CF_NO_LINK) {
            record_of(ctx, parent)->child_first = slot;
        } else {
            record_of(ctx, last_child[parent])->next_sibling = slot;
        }

        last_child[parent] = slot;
    }

    /* Reverse preorder visits every child before its parent, so one sweep
     * accumulates each subtree into its parent's count. */
    for (uint32_t i = declared; i-- > 0u;) {
        const uint32_t slot = flat[i].slot;
        node_record *record = record_of(ctx, slot);

        sizes[slot] += 1u;
        record->subtree_size = sizes[slot];

        if (record->parent != CF_NO_LINK) {
            sizes[record->parent] += sizes[slot];
        }
    }

    for (uint32_t i = 0; i < declared; ++i) {
        const uint32_t slot = flat[i].slot;
        const cf_node *decl = flat[i].decl;

        styles[slot] = decl->style;
        measures[slot] = decl->measure;
        measure_data[slot] = decl->measure_data;
    }

    child_start[0] = 0u;

    for (uint32_t slot = 0; slot < extent; ++slot) {
        child_start[slot + 1u] = child_start[slot] + child_counts[slot];
    }

    for (uint32_t slot = 0; slot < extent; ++slot) {
        uint32_t at = child_start[slot];

        /* Slots without children this frame keep whatever links a previous
         * life of the slot left behind; their row is empty and is not walked. */
        if (child_counts[slot] == 0u) {
            continue;
        }

        for (uint32_t child = record_of(ctx, slot)->child_first; child != CF_NO_LINK;
             child = record_of(ctx, child)->next_sibling) {
            child_slots[at] = child;
            at++;
        }
    }

    ctx->root_slot = flat[0].slot;
    ctx->layout = (cf_tree){.styles = styles,
                            .measures = measures,
                            .measure_data = measure_data,
                            .child_start = child_start,
                            .child_slots = child_slots,
                            .node_count = extent};
    ctx->layout_ready = true;

    return true;
}
