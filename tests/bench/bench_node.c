/*
 * Diff throughput at the size the budget names. The cold build answers once;
 * the run that matters is the same declaration re-reconciled frame after
 * frame - the idle path a real interface spends most of its time on. The
 * rigs assert nothing about time, and print what the machine managed.
 */
#include <stdio.h>
#include <time.h>

#include "cf/node.h"
#include "cform_test.h"

#define ROWS 99u
#define ROW_CHILDREN 100u
#define NODE_COUNT (1u + ROWS + ROWS * ROW_CHILDREN)
#define FRAMES 50u
#define NS_PER_MS 1000000.0
#define FRAME_BYTES (8u * 1024u * 1024u)

static cf_node cells[ROWS][ROW_CHILDREN];
static cf_node rows[ROWS];
static cf_node root;

static void build_tree(cf_interner *names) {
    root = (cf_node){.style = cf_style_default()};
    root.style.width = CF_PX(1280.0f);
    root.style.height = CF_PX(720.0f);

    for (uint32_t r = 0; r < ROWS; ++r) {
        rows[r] = (cf_node){.style = cf_style_default()};
        rows[r].key = cf_intern_text(names, "row");
        rows[r].style.height = CF_PX(20.0f);
        rows[r].children = cells[r];
        rows[r].child_count = ROW_CHILDREN;

        for (uint32_t c = 0; c < ROW_CHILDREN; ++c) {
            cells[r][c] = (cf_node){.style = cf_style_default()};
            cells[r][c].style.width = CF_PX(40.0f + (float)(c % 5u) * 10.0f);
            cells[r][c].style.height = CF_PX(20.0f);
        }
    }

    root.children = rows;
    root.child_count = ROWS;
}

static double elapsed_ms(const struct timespec *started, const struct timespec *finished) {
    const double seconds = (double)finished->tv_sec - (double)started->tv_sec;
    const double nanos = (double)finished->tv_nsec - (double)started->tv_nsec;

    return (seconds * 1000000000.0 + nanos) / NS_PER_MS;
}

/* The fastest pass rather than the average: a shared runner spends its wall
 * time on somebody else's process, and only an uncontended pass says what
 * the engine did. */
static double frame_fastest_ms(cf_ctx *ctx, const cf_node *tree, uint32_t frames,
                               bool with_layout) {
    double best = -1.0;

    for (uint32_t i = 0; i < frames; ++i) {
        struct timespec started;
        struct timespec finished;

        timespec_get(&started, TIME_UTC);
        cf_frame_begin(ctx);
        cf_diff(ctx, tree);

        if (with_layout) {
            cf_frame_layout(ctx, (cf_available){.width = CF_PX(1280.0f), .height = CF_PX(720.0f)},
                            (cf_point){.x = 0.0f, .y = 0.0f});
        }

        timespec_get(&finished, TIME_UTC);

        const double ms = elapsed_ms(&started, &finished);

        if (best < 0.0 || ms < best) {
            best = ms;
        }
    }

    return best;
}

int main(void) {
    printf("node diff\n");

    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, FRAME_BYTES)) {
        CF_CHECK(false, "the context starts");
        return CF_REPORT();
    }

    build_tree(&ctx.names);

    /* The cold build: every slot of the tree is new to the store. */
    struct timespec started;
    struct timespec finished;

    cf_frame_begin(&ctx);
    timespec_get(&started, TIME_UTC);
    const uint32_t root_slot = cf_diff(&ctx, &root);
    timespec_get(&finished, TIME_UTC);

    CF_CHECK(root_slot != CF_NO_LINK, "the diff answers the 10k declaration");
    CF_CHECK(cf_frame_tree(&ctx) != NULL, "the diff builds a view of it");
    printf("  cold build, %u nodes:  %8.3f ms\n", NODE_COUNT, elapsed_ms(&started, &finished));

    /* The same declaration again and again: the idle path, where nothing
     * changes and nothing should be dirtied. */
    const size_t idle_state = cf_state_bytes(&ctx);
    const double diff_ms = frame_fastest_ms(&ctx, &root, FRAMES, false);
    printf("  idle diff, %u nodes:   %8.3f ms per frame\n", NODE_COUNT, diff_ms);

    /* The budget a frame that changes nothing has to hit: no state asked for
     * that the frame before it did not already hold, and no node left standing
     * dirty, which is what tells the layout and the paint there is nothing to
     * do. */
    CF_CHECK(cf_state_bytes(&ctx) == idle_state,
             "an idle frame holds no state the frame before it did not");

    uint32_t dirtied = 0u;

    for (uint32_t slot = 0u; slot < NODE_COUNT; ++slot) {
        dirtied += cf_dirty(&ctx, slot) ? 1u : 0u;
    }

    CF_CHECK(dirtied == 0u, "an idle frame dirties no node");

    const double laid_ms = frame_fastest_ms(&ctx, &root, FRAMES, true);
    printf("  diff + layout, %u nodes: %5.3f ms per frame\n", NODE_COUNT, laid_ms);

    cf_ctx_free(&ctx);

    return CF_REPORT();
}
