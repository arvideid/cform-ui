/*
 * Paint throughput at the size the budget names, and what a frame's draw list
 * costs the arena it grows in. The list doubles from one allocation to the next
 * inside the scratch it is handed, so the byte figure is what a host sizes its
 * frame arena against. The command count is asserted; the fastest of several
 * repaints is printed, because a shared runner spends its wall time on
 * somebody else's process.
 */
#include <stdio.h>
#include <time.h>

#include "cf/node.h"
#include "cf/paint.h"

#include "cform_test.h"

#include "bench_meta.h"

#define ROWS 99u
#define CELLS_PER_ROW 100u
#define NODE_COUNT (1u + ROWS + ROWS * CELLS_PER_ROW)
#define FRAMES 50u
#define NS_PER_MS 1000000.0
#define FRAME_BYTES (8u * 1024u * 1024u)

#define SURFACE_WIDTH 1280.0f
#define SURFACE_HEIGHT 720.0f

/*
 * Every box paints a fill and every fourth cell strokes a border over it, so
 * the stream carries both shapes a rounded rect asks a device for. The rows
 * and the cells together stay inside the surface, which is what lets the
 * count be a number rather than a range.
 */
#define BORDERED_EVERY 4u
#define EXPECTED_COMMANDS                                                                          \
    (1u + ROWS + ROWS * CELLS_PER_ROW + ROWS * (CELLS_PER_ROW / BORDERED_EVERY))

static cf_node cells[ROWS][CELLS_PER_ROW];
static cf_node rows[ROWS];
static cf_node root;

static cf_opt_color shade(uint8_t tone) {
    return (cf_opt_color){.present = true, .value = CF_COLOR(tone, tone, tone)};
}

static void build_tree(void) {
    root = (cf_node){.style = cf_style_default()};
    root.style.width = CF_PX(SURFACE_WIDTH);
    root.style.height = CF_PX(SURFACE_HEIGHT);
    root.style.direction = CF_DIRECTION_COLUMN;
    root.style.background = shade(24u);

    for (uint32_t r = 0u; r < ROWS; ++r) {
        rows[r] = (cf_node){.style = cf_style_default()};
        rows[r].style.height = CF_PX(7.0f);
        rows[r].style.background = shade(40u);
        rows[r].children = cells[r];
        rows[r].child_count = CELLS_PER_ROW;

        for (uint32_t c = 0u; c < CELLS_PER_ROW; ++c) {
            cells[r][c] = (cf_node){.style = cf_style_default()};
            cells[r][c].style.width = CF_PX(12.0f);
            cells[r][c].style.background = shade(120u);

            if (c % BORDERED_EVERY == 0u) {
                cells[r][c].style.border =
                    (cf_box){CF_PX(1.0f), CF_PX(1.0f), CF_PX(1.0f), CF_PX(1.0f)};
                cells[r][c].style.border_color = shade(220u);
                cells[r][c].style.radius = 3.0f;
            }
        }
    }

    root.children = rows;
    root.child_count = ROWS;
}

static cf_rect surface_viewport(void) {
    return (cf_rect){0.0f, 0.0f, SURFACE_WIDTH, SURFACE_HEIGHT};
}

static double elapsed_ms(const struct timespec *started, const struct timespec *finished) {
    const double seconds = (double)finished->tv_sec - (double)started->tv_sec;
    const double nanos = (double)finished->tv_nsec - (double)started->tv_nsec;

    return (seconds * 1000000000.0 + nanos) / NS_PER_MS;
}

/* The tree, laid out and ready to paint, with the arena mark that keeps the
 * rects alive while every later rewind drops only command lists. */
static size_t lay_out(cf_ctx *ctx) {
    cf_frame_begin(ctx);
    cf_diff(ctx, &root);
    cf_frame_resolve(ctx);
    cf_frame_layout(ctx,
                    (cf_available){.width = CF_PX(SURFACE_WIDTH), .height = CF_PX(SURFACE_HEIGHT)},
                    (cf_point){0.0f, 0.0f});

    return cf_arena_mark(&ctx->frame);
}

static cf_command_list paint(cf_ctx *ctx, cf_rect viewport) {
    cf_command_list list = {0};

    cf_paint(&ctx->frame, cf_frame_tree(ctx), ctx->rects, ctx->root_slot, viewport, &list);

    return list;
}

/* Whether every command in the stream is one of the two shapes this tree
 * declares: a fill, or a border stroked over one. */
static bool stream_holds_fills_and_borders(const cf_command_list *list) {
    for (uint32_t i = 0u; i < list->count; ++i) {
        const cf_command_kind kind = list->commands[i].kind;

        if (kind != CF_COMMAND_FILL && kind != CF_COMMAND_BORDER) {
            return false;
        }
    }

    return true;
}

int main(void) {
    bench_meta("bench_paint", "10k-rows-of-100-cells-fill-and-border");
    bench_result("nodes", (double)NODE_COUNT, "count");
    bench_result("frames_per_point", (double)FRAMES, "count");
    bench_result("frame_bytes", (double)FRAME_BYTES, "bytes");

    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, FRAME_BYTES)) {
        CF_CHECK(false, "the context starts");
        return CF_REPORT();
    }

    build_tree();
    const size_t laid_out = lay_out(&ctx);

    CF_CHECK(cf_frame_tree(&ctx) != NULL, "the 10k tree lays out");

    const cf_command_list full = paint(&ctx, surface_viewport());

    CF_CHECK(full.count == EXPECTED_COMMANDS, "one fill per box, a border on every fourth cell");
    CF_CHECK(stream_holds_fills_and_borders(&full), "the stream carries fills and borders only");
    CF_CHECK(full.count > 0u && full.commands[0].kind == CF_COMMAND_FILL,
             "the page's own fill leads the stream");

    bench_result("command_count", (double)full.count, "count");

    /* The reach check drops a box the viewport cannot see at all: with nothing
     * in front of it the walk appends nothing, which is the paint half of an
     * idle frame's zero. */
    cf_arena_rewind(&ctx.frame, laid_out);
    const cf_command_list empty = paint(&ctx, (cf_rect){0.0f, 0.0f, 0.0f, 0.0f});

    CF_CHECK(empty.count == 0u, "a viewport that reaches nothing paints nothing");

    /* Repaint the same laid-out tree into the same arena, rewinding first each
     * time: the per-frame cost a shell pays when something did change. */
    double best = -1.0;
    uint32_t last_count = 0u;
    size_t last_bytes = 0u;

    for (uint32_t i = 0u; i < FRAMES; ++i) {
        struct timespec started;
        struct timespec finished;

        cf_arena_rewind(&ctx.frame, laid_out);
        timespec_get(&started, TIME_UTC);
        const cf_command_list list = paint(&ctx, surface_viewport());
        timespec_get(&finished, TIME_UTC);

        const double ms = elapsed_ms(&started, &finished);

        if (best < 0.0 || ms < best) {
            best = ms;
        }

        last_count = list.count;
        last_bytes = cf_arena_mark(&ctx.frame) - laid_out;
    }

    CF_CHECK(last_count == EXPECTED_COMMANDS, "every rewind repaints the same stream");
    CF_CHECK(last_bytes > 0u, "the list costs the arena it grew in, not the system's");

    bench_result("repaint_ms", best, "ms");
    bench_result("stream_scratch_kib", (double)last_bytes / 1024.0, "KiB");

    cf_ctx_free(&ctx);

    return CF_REPORT();
}
