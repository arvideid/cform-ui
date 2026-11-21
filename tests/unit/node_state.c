#include "cf/node.h"
#include "cform_test.h"

/* Every case builds in its own context so a leak or a bad state in one does
 * not dress up as a state bug in another. */
static bool context_of(cf_ctx *ctx) {
    if (!cf_ctx_init(ctx, 64u * 1024u, 4u * 1024u)) {
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
    CF_CHECK(fresh != block, "the retired name answers new memory");
    CF_CHECK(fresh != NULL && *fresh == 0, "the new block starts zeroed");

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
    CF_RUN(a_nameless_request_is_refused);

    return CF_REPORT();
}
