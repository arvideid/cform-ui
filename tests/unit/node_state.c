#include "cf/node.h"
#include "cform_test.h"

/* Every case builds in its own context so a leak or a bad state in one does
 * not dress up as a state bug in another. */
static bool context_of(cf_ctx *ctx) {
    if (!cf_ctx_init(ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return false;
    }

    return true;
}

static cf_node node_of(cf_name key) {
    return (cf_node){.key = key};
}

static void a_requested_block_starts_zeroed(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    const cf_name tab = cf_intern_text(&ctx.names, "tab");

    cf_frame_begin(&ctx);

    const int *block = cf_state(&ctx, tab, sizeof(int));
    CF_CHECK(block != NULL, "the block is served");
    CF_CHECK(block != NULL && *block == 0, "a fresh block reads as zero");

    cf_ctx_free(&ctx);
}

static void the_same_key_answers_the_same_memory(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    const cf_name tab = cf_intern_text(&ctx.names, "tab");

    cf_frame_begin(&ctx);

    const int *first = cf_state(&ctx, tab, sizeof(int));
    const int *second = cf_state(&ctx, tab, sizeof(int));
    CF_CHECK(first != NULL && first == second, "two requests under one name share one block");

    cf_ctx_free(&ctx);
}

static void a_block_survives_rebuilds(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    const cf_name tab = cf_intern_text(&ctx.names, "tab");

    cf_frame_begin(&ctx);
    int *block = cf_state(&ctx, tab, sizeof(int));

    if (block != NULL) {
        *block = 7;
    }

    cf_frame_begin(&ctx);
    int *again = cf_state(&ctx, tab, sizeof(int));
    CF_CHECK(again == block, "the block keeps its address across frames");
    CF_CHECK(again != NULL && *again == 7, "the block keeps its value");

    cf_frame_begin(&ctx);
    int *third = cf_state(&ctx, tab, sizeof(int));
    CF_CHECK(third == block && third != NULL && *third == 7, "the block outlives any single frame");

    cf_ctx_free(&ctx);
}

static void a_block_outlives_the_node_that_requested_it(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    const cf_name tab = cf_intern_text(&ctx.names, "tab");
    const cf_node with_it = node_of(tab);

    cf_frame_begin(&ctx);
    int *block = cf_state(&ctx, tab, sizeof(int));

    if (block != NULL) {
        *block = 7;
    }

    cf_diff(&ctx, &with_it);

    /* The node retires; the name is still being requested, so its state is
     * none of the diff's business. */
    cf_frame_begin(&ctx);
    int *kept = cf_state(&ctx, tab, sizeof(int));
    cf_diff(&ctx, NULL);

    CF_CHECK(kept == block && kept != NULL && *kept == 7,
             "a retired node does not retire the state requested under its name");

    cf_ctx_free(&ctx);
}

static void an_unrequested_block_retires(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    const cf_name tab = cf_intern_text(&ctx.names, "tab");

    cf_frame_begin(&ctx);
    int *block = cf_state(&ctx, tab, sizeof(int));

    if (block != NULL) {
        *block = 7;
    }

    /* One frame that never asks for it, then one more: the first begin
     * keeps the entry, the second drops it. */
    cf_frame_begin(&ctx);
    cf_frame_begin(&ctx);

    int *fresh = cf_state(&ctx, tab, sizeof(int));

    /* Which address a retired name answers with is the allocator's to decide,
     * and reusing the one it just took back is legal. What the engine owes is a
     * block with nothing left in it, which the seven above cannot survive. */
    CF_CHECK(fresh != NULL && *fresh == 0, "the retired name answers a block that starts empty");

    cf_ctx_free(&ctx);
}

static void a_grown_block_keeps_its_contents(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    const cf_name tab = cf_intern_text(&ctx.names, "tab");

    cf_frame_begin(&ctx);
    int *small = cf_state(&ctx, tab, sizeof(int));

    if (small != NULL) {
        *small = 7;
    }

    cf_frame_begin(&ctx);
    int *pair = cf_state(&ctx, tab, 2u * sizeof(int));
    CF_CHECK(pair != NULL, "the block grows");
    CF_CHECK(pair != NULL && pair[0] == 7, "the grown block keeps the old bytes");
    CF_CHECK(pair != NULL && pair[1] == 0, "the grown tail starts zeroed");

    cf_ctx_free(&ctx);
}

static void a_retired_block_returns_its_bytes(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    const cf_name first = cf_intern_text(&ctx.names, "first");
    const cf_name second = cf_intern_text(&ctx.names, "second");

    cf_frame_begin(&ctx);
    CF_CHECK(cf_state(&ctx, first, 256u) != NULL, "the first block is served");
    CF_CHECK(cf_state(&ctx, second, 256u) != NULL, "the second block is served");
    CF_CHECK(cf_state_bytes(&ctx) == 512u, "both blocks are counted");

    /* One frame that asks for neither keeps them, since a pointer into state
     * is good through the frame after the one that obtained it; the next
     * frame is the one that gives the bytes back. */
    cf_frame_begin(&ctx);
    CF_CHECK(cf_state_bytes(&ctx) == 512u, "a frame that did not ask yet keeps the block");

    cf_frame_begin(&ctx);
    CF_CHECK(cf_state_bytes(&ctx) == 0u, "a retired name hands its bytes back");

    cf_ctx_free(&ctx);
}

/* A page open for an hour asks for state under names it never asked under
 * before - a chat thread, a document list, a wizard with a step per record.
 * What matters is not the count of names it ever minted but the count it is
 * holding now, so the bytes stay inside two rounds no matter how many rounds
 * have run. */
static void state_does_not_accrue_as_a_page_runs(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    bool served = true;
    size_t held_peak = 0u;

    for (uint32_t round = 0u; round < 200u; ++round) {
        cf_frame_begin(&ctx);

        for (uint32_t i = 0u; i < 40u; ++i) {
            char name[16];
            snprintf(name, sizeof name, "r%u-%u", round, i);

            if (cf_state(&ctx, cf_intern_text(&ctx.names, name), 64u) == NULL) {
                served = false;
            }
        }

        const size_t held = cf_state_bytes(&ctx);

        if (held > held_peak) {
            held_peak = held;
        }
    }

    CF_CHECK(served, "every round's fresh names are served");
    CF_CHECK(held_peak <= 2u * 40u * 64u, "8 000 names minted never held past two rounds");

    cf_frame_begin(&ctx);
    cf_frame_begin(&ctx);
    CF_CHECK(cf_state_bytes(&ctx) == 0u, "a page that stops asking holds nothing");

    cf_ctx_free(&ctx);
}

/* A block wants the bytes it asked for, not a share of a budget settled when
 * the context began: the state a page keeps is bounded by what it uses. */
static void a_block_larger_than_the_context_starts_with_is_served(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    cf_frame_begin(&ctx);
    char *block = cf_state(&ctx, cf_intern_text(&ctx.names, "buffer"), 1024u * 1024u);
    CF_CHECK(block != NULL, "a state block is not capped by the arena a context began with");

    if (block != NULL) {
        block[0] = 'x';
        block[1024u * 1024u - 1u] = 'y';
        CF_CHECK(block[1024u * 1024u - 1u] == 'y', "the whole block is the caller's to write");
    }

    cf_ctx_free(&ctx);
}

static void a_nameless_request_is_refused(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    cf_frame_begin(&ctx);

    CF_CHECK(cf_state(&ctx, CF_NO_NAME, sizeof(int)) == NULL, "CF_NO_NAME names no block");
    CF_CHECK(cf_state(&ctx, cf_intern_text(&ctx.names, "zero"), 0u) == NULL,
             "a zero size asks for no block");

    cf_ctx_free(&ctx);
}

int main(void) {
    printf("node state\n");

    CF_RUN(a_requested_block_starts_zeroed);
    CF_RUN(the_same_key_answers_the_same_memory);
    CF_RUN(a_block_survives_rebuilds);
    CF_RUN(a_block_outlives_the_node_that_requested_it);
    CF_RUN(an_unrequested_block_retires);
    CF_RUN(a_grown_block_keeps_its_contents);
    CF_RUN(a_retired_block_returns_its_bytes);
    CF_RUN(state_does_not_accrue_as_a_page_runs);
    CF_RUN(a_block_larger_than_the_context_starts_with_is_served);
    CF_RUN(a_nameless_request_is_refused);

    return CF_REPORT();
}
