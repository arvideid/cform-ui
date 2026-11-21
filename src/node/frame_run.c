#include "cf/node.h"

/*
 * The pipeline cf_frame_begin through cf_paint, composed in the one order the
 * layers may run: the declaration is diffed while the build's frame still
 * holds it, the resolver rewrites what the diff built, the layout reads the
 * styles the resolver left, and the paint walks the rects the layout wrote.
 */
void cf_frame_run(cf_ctx *ctx, cf_build_fn build, cf_size offer, cf_command_list *out) {
    if (ctx == NULL || build == NULL || out == NULL) {
        return;
    }

    cf_frame_begin(ctx);
    const cf_node *root = build(ctx);
    const uint32_t root_slot = cf_diff(ctx, root);
    cf_frame_resolve(ctx);
    cf_frame_layout(ctx, (cf_available){.width = CF_PX(offer.width), .height = CF_PX(offer.height)},
                    (cf_point){0.0f, 0.0f});
    cf_paint(&ctx->frame, cf_frame_tree(ctx), ctx->rects, root_slot,
             (cf_rect){0.0f, 0.0f, offer.width, offer.height}, out);
}
