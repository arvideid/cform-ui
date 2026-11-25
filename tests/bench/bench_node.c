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

#include "bench_meta.h"

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

/*
 * Warm-change mode: half the cells get a different width after the idle pass.
 * The diff must see the signature change, dirty those nodes, and relayout.
 * A realistic case: a column resize or a flex-grow animation that changes a
 * subset of the tree every frame.
 */
static void perturb_cells(void) {
    for (uint32_t r = 0; r < ROWS; ++r) {
        for (uint32_t c = 0; c < ROW_CHILDREN; ++c) {
            if ((r + c) % 2u == 0u) {
                cells[r][c].style.width = CF_PX(40.0f + (float)((c + 1u) % 5u) * 10.0f);
            }
        }
    }
}

/*
 * Churn mode: a different tree shape entirely — half the row count but twice
 * the cells per row, so the store retires the surplus and cold-builds the rest
 * through a reused context. This is the case where a panel closes and another
 * opens with different dimensions.
 */
#define CHURN_ROWS (ROWS / 2u)
#define CHURN_CELLS (ROW_CHILDREN * 2u)
#define CHURN_COUNT (1u + CHURN_ROWS + CHURN_ROWS * CHURN_CELLS)

static cf_node churn_cells[CHURN_ROWS][CHURN_CELLS];
static cf_node churn_rows[CHURN_ROWS];
static cf_node churn_root;

static void build_churn_tree(cf_interner *names) {
    churn_root = (cf_node){.style = cf_style_default()};
    churn_root.style.width = CF_PX(1280.0f);
    churn_root.style.height = CF_PX(720.0f);

    for (uint32_t r = 0; r < CHURN_ROWS; ++r) {
        churn_rows[r] = (cf_node){.style = cf_style_default()};
        churn_rows[r].key = cf_intern_text(names, "row");
        churn_rows[r].style.height = CF_PX(20.0f);
        churn_rows[r].children = churn_cells[r];
        churn_rows[r].child_count = CHURN_CELLS;

        for (uint32_t c = 0; c < CHURN_CELLS; ++c) {
            churn_cells[r][c] = (cf_node){.style = cf_style_default()};
            churn_cells[r][c].style.width = CF_PX(20.0f);
            churn_cells[r][c].style.height = CF_PX(20.0f);
        }
    }

    churn_root.children = churn_rows;
    churn_root.child_count = CHURN_ROWS;
}

/*
 * State-key lookup mode: a page that keeps a small block per row, so the walk
 * visits `state_rows` interned names per frame. The roadmap Section 6 item 4
 * says the choice of a hashed entry table depends on what this actually costs
 * at the size of a real page, so the rig prints the fastest pass across a
 * range of row counts and lets a review decide whether the O(S) sweep in
 * `entry_of_key` is worth an index. Each name is unique because a real page
 * keys state per widget rather than reusing one key across the tree.
 */
#define STATE_ROWS_MAX 1000u
#define STATE_FRAMES 20u

static cf_name state_keys[STATE_ROWS_MAX];

static void seed_state_keys(cf_interner *names) {
    char buf[32];

    for (uint32_t i = 0u; i < STATE_ROWS_MAX; ++i) {
        snprintf(buf, sizeof buf, "state/%u", i);
        state_keys[i] = cf_intern_text(names, buf);
    }
}

/* One pass that touches `wanted` state keys and re-reads them every frame.
 * The first cf_state per key allocates; later calls hit `entry_of_key`. */
static double state_fastest_ms(cf_ctx *ctx, uint32_t wanted, uint32_t frames) {
    double best = -1.0;

    for (uint32_t f = 0u; f < frames; ++f) {
        struct timespec started;
        struct timespec finished;
        timespec_get(&started, TIME_UTC);

        for (uint32_t i = 0u; i < wanted; ++i) {
            uint32_t *held = cf_state(ctx, state_keys[i], sizeof(uint32_t));

            if (held != NULL) {
                *held += 1u;
            }
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
    bench_meta("bench_node", "10k-flat-row-tree-diff-cold-warm-change-churn-state-lookup");
    bench_result("nodes", (double)NODE_COUNT, "count");
    bench_result("frames_per_point", (double)FRAMES, "count");
    bench_result("frame_bytes", (double)FRAME_BYTES, "bytes");

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
    bench_result("cold_build_ms", elapsed_ms(&started, &finished), "ms");

    /* The same declaration again and again: the idle path, where nothing
     * changes and nothing should be dirtied. */
    const size_t idle_state = cf_state_bytes(&ctx);
    const double diff_ms = frame_fastest_ms(&ctx, &root, FRAMES, false);
    bench_result("idle_diff_ms", diff_ms, "ms");

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
    bench_result("diff_plus_layout_ms", laid_ms, "ms");

    /* Warm-change: mutate half the cell widths, then diff+layout. The store
     * is already populated so this measures the cost of detecting and
     * processing partial invalidation. */
    perturb_cells();
    const double change_ms = frame_fastest_ms(&ctx, &root, FRAMES, true);
    bench_result("warm_change_ms", change_ms, "ms");

    /* Churn: replace the tree with a structurally different shape, forcing
     * retirement of surplus nodes and cold-building of new ones within an
     * already-allocated context. */
    build_churn_tree(&ctx.names);
    cf_frame_begin(&ctx);
    timespec_get(&started, TIME_UTC);
    const uint32_t churn_slot = cf_diff(&ctx, &churn_root);
    timespec_get(&finished, TIME_UTC);

    CF_CHECK(churn_slot != CF_NO_LINK, "the churn diff answers the new tree");
    bench_result("churn_build_ms", elapsed_ms(&started, &finished), "ms");
    bench_result("churn_nodes", (double)CHURN_COUNT, "count");

    /* State-key lookup at three sizes a page actually hits: a small panel,
     * a table of widgets, and a heavy page. The pass prints the fastest
     * per-frame time across STATE_FRAMES runs so a reviewer can compare
     * what the O(S) sweep in entry_of_key costs before and after any
     * hashed-index change. */
    seed_state_keys(&ctx.names);

    static const uint32_t wanted[] = {50u, 250u, STATE_ROWS_MAX};

    for (uint32_t i = 0u; i < sizeof wanted / sizeof wanted[0]; ++i) {
        char key[40];
        /* Warm the entries before the measured pass so the first-touch
         * allocates are not what the number describes. */
        (void)state_fastest_ms(&ctx, wanted[i], 1u);
        const double ms = state_fastest_ms(&ctx, wanted[i], STATE_FRAMES);
        snprintf(key, sizeof key, "state_lookup_%ums", wanted[i]);
        bench_result(key, ms, "ms");
        snprintf(key, sizeof key, "state_lookup_%uns_per_key", wanted[i]);
        bench_result(key, ms * NS_PER_MS / (double)wanted[i], "ns");
    }

    cf_ctx_free(&ctx);

    return CF_REPORT();
}
