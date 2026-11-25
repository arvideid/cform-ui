/*
 * What a page costs in bytes rather than in milliseconds. The time rigs say how
 * fast a frame is; this one says whether a page that stays open grows - the
 * question a kiosk and a text editor both have to answer before either ships.
 * The sizes print for the record; what the rig holds the run to is that none of
 * them move with the number of frames.
 */
#include <stdio.h>

#include "cf/node.h"
#include "cform_test.h"

#define ROWS 99u
#define ROW_CHILDREN 100u
#define NODE_COUNT (1u + ROWS + ROWS * ROW_CHILDREN)
#define IDLE_FRAMES 1000u
#define SESSION_ROUNDS 200u
#define ROUND_KEYS 40u
#define KEY_BYTES 64u
#define FRAME_BYTES (8u * 1024u * 1024u)
#define BYTES_PER_KIB 1024.0

static cf_node cells[ROWS][ROW_CHILDREN];
static cf_node rows[ROWS];
static cf_node page;

static void build_page(cf_interner *names) {
    page = (cf_node){.style = cf_style_default()};
    page.style.width = CF_PX(1280.0f);
    page.style.height = CF_PX(720.0f);

    for (uint32_t r = 0u; r < ROWS; ++r) {
        rows[r] = (cf_node){.style = cf_style_default()};
        rows[r].key = cf_intern_text(names, "row");
        rows[r].style.height = CF_PX(20.0f);
        rows[r].children = cells[r];
        rows[r].child_count = ROW_CHILDREN;

        for (uint32_t c = 0u; c < ROW_CHILDREN; ++c) {
            cells[r][c] = (cf_node){.style = cf_style_default()};
            cells[r][c].style.width = CF_PX(40.0f + (float)(c % 5u) * 10.0f);
            cells[r][c].style.height = CF_PX(20.0f);
        }
    }

    page.children = rows;
    page.child_count = ROWS;
}

static cf_node declare(cf_ctx *ctx) {
    (void)ctx;

    return page;
}

/* One round of a page whose records come and go: a thread that grows, a list
 * whose selection changes, a wizard with a step per record. Every key is one no
 * earlier round asked under. */
static size_t run_round(cf_ctx *ctx, uint32_t round) {
    cf_frame_begin(ctx);

    for (uint32_t i = 0u; i < ROUND_KEYS; ++i) {
        char name[16];
        snprintf(name, sizeof name, "r%u-%u", round, i);
        cf_state(ctx, cf_intern_text(&ctx->names, name), KEY_BYTES);
    }

    return cf_state_bytes(ctx);
}

int main(void) {
    printf("memory\n");

    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, FRAME_BYTES)) {
        CF_CHECK(false, "the context starts");
        return CF_REPORT();
    }

    build_page(&ctx.names);

    /* One whole frame through the real pipeline: the arena high-water is what
     * a page of this size costs, and it is charged once per frame rather than
     * accrued across them. */
    cf_command_list list = {0};
    cf_frame_run(&ctx, declare, (cf_size){1280.0f, 720.0f}, 0.016f, &list);

    /* The second frame, not the first, is the one a page at rest repeats: the
     * first builds the tree the second's diff has to index, so it reaches bytes
     * the steady state never asks for again. */
    cf_frame_run(&ctx, declare, (cf_size){1280.0f, 720.0f}, 0.016f, &list);
    const size_t steady_used = ctx.frame.used;
    const size_t steady_peak = ctx.frame.peak;

    /* A page at rest asks the arena for the same bytes every frame, so neither
     * what a frame ends holding nor the high-water it passed through moves with
     * the number of frames run. */
    bool flat = true;

    for (uint32_t i = 2u; i < IDLE_FRAMES; ++i) {
        cf_frame_run(&ctx, declare, (cf_size){1280.0f, 720.0f}, 0.016f, &list);

        if (ctx.frame.used != steady_used || ctx.frame.peak != steady_peak) {
            flat = false;
        }
    }

    printf("  one %uk-node frame, built and painted: %7.0f KiB of arena\n", NODE_COUNT / 1000u,
           (double)steady_peak / BYTES_PER_KIB);
    printf("  %uk idle frames of it:                 %7.0f KiB\n", IDLE_FRAMES / 1000u,
           (double)ctx.frame.peak / BYTES_PER_KIB);
    CF_CHECK(flat, "idle frames cost what the second frame costs, not more");

    /* The number a long session used to move: state asked for under names that
     * never come back. The peak is what the run held at its heaviest, the last
     * what it held once the page stopped asking. Interned names and retired
     * store slots do not shrink back, so the plateau claim has to say what
     * the interner and the two stores keep after the state GC finishes. */
    size_t held_peak = 0u;
    size_t interned_peak = 0u;
    size_t store_peak = 0u;

    for (uint32_t round = 0u; round < SESSION_ROUNDS; ++round) {
        const size_t held = run_round(&ctx, round);
        const size_t interned = cf_interner_bytes(&ctx.names);
        const size_t nodes = cf_store_bytes(&ctx.nodes) + cf_store_bytes(&ctx.states);

        if (held > held_peak) {
            held_peak = held;
        }

        if (interned > interned_peak) {
            interned_peak = interned;
        }

        if (nodes > store_peak) {
            store_peak = nodes;
        }
    }

    cf_frame_begin(&ctx);
    cf_frame_begin(&ctx);
    const size_t held_last = cf_state_bytes(&ctx);
    const size_t interned_last = cf_interner_bytes(&ctx.names);
    const size_t store_last = cf_store_bytes(&ctx.nodes) + cf_store_bytes(&ctx.states);

    printf("  %u rounds of %u fresh keys:           %7.0f KiB held at the peak\n", SESSION_ROUNDS,
           ROUND_KEYS, (double)held_peak / BYTES_PER_KIB);
    printf("  the page stops asking:                %7.0f KiB\n",
           (double)held_last / BYTES_PER_KIB);
    printf("  interned names retained:              %7.0f KiB (peak %7.0f KiB)\n",
           (double)interned_last / BYTES_PER_KIB, (double)interned_peak / BYTES_PER_KIB);
    printf("  node and state store capacity:        %7.0f KiB (peak %7.0f KiB)\n",
           (double)store_last / BYTES_PER_KIB, (double)store_peak / BYTES_PER_KIB);
    CF_CHECK(held_peak <= 2u * ROUND_KEYS * KEY_BYTES,
             "state accrues for the rounds still live, not the rounds that ran");
    CF_CHECK(held_last == 0u, "a page that stops asking holds no state");
    CF_CHECK(interned_last >= interned_peak, "interned bytes never grow once the run stops");
    CF_CHECK(store_last <= store_peak, "store capacity plateaus with the working set");

    cf_ctx_free(&ctx);
    return CF_REPORT();
}
