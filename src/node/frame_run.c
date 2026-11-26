#include "cf/node.h"

/*
 * The pipeline cf_frame_begin through cf_paint, composed in the one order the
 * layers may run: the declaration is diffed while the build's page still
 * lives on this frame's stack, the resolver rewrites what the diff built, the
 * layout reads the styles the resolver left, and the paint walks the rects the
 * layout wrote.
 *
 * The interval is handed in rather than read: measuring the wall belongs to
 * whoever waited on it, so the shell reports what passed between its frames and
 * a caller stepping the pipeline itself states what a frame covers.
 */
void cf_frame_run(cf_ctx *ctx, cf_build_fn build, cf_size offer, float seconds,
                  cf_command_list *out) {
    if (ctx == NULL || build == NULL || out == NULL) {
        return;
    }

    /* A host that measures its own interval may hand a negative or NaN
     * reading from a broken clock or a first frame with no predecessor.
     * Clamping to zero means tweens hold still rather than jumping or
     * poisoning every later arithmetic that reads frame_seconds. */
    if (!(seconds >= 0.0f)) {
        seconds = 0.0f;
    }

    cf_frame_begin(ctx);
    /* The frame replaces the stream rather than adding to it: a list carried
     * over from an earlier run would append onto pointers into the arena that
     * cf_frame_begin has just cleared. Zeroing it hands cf_paint an empty list
     * to fill from scratch, so the run's output is only this frame's commands. */
    *out = (cf_command_list){0};
    /* The begin clears it, so the run is the one that states it. */
    ctx->frame_seconds = seconds;
    const cf_node root = build(ctx);
    const uint32_t root_slot = cf_diff(ctx, &root);
    cf_frame_resolve(ctx);
    cf_frame_layout(ctx, (cf_available){.width = CF_PX(offer.width), .height = CF_PX(offer.height)},
                    (cf_point){0.0f, 0.0f});
    cf_paint(&ctx->frame, cf_frame_tree(ctx), ctx->rects, root_slot,
             (cf_rect){0.0f, 0.0f, offer.width, offer.height}, out);
}

float cf_frame_seconds(const cf_ctx *ctx) {
    return ctx == NULL ? 0.0f : ctx->frame_seconds;
}

void cf_request_frame(cf_ctx *ctx) {
    if (ctx == NULL) {
        return;
    }

    ctx->frame_requested = true;
}

bool cf_frame_requested(const cf_ctx *ctx) {
    return ctx != NULL && ctx->frame_requested;
}

void cf_request_quit(cf_ctx *ctx) {
    if (ctx == NULL) {
        return;
    }

    ctx->quit_requested = true;
}

bool cf_quit_requested(const cf_ctx *ctx) {
    return ctx != NULL && ctx->quit_requested;
}
