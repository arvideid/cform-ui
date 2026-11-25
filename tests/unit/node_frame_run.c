#include "cf/node.h"

#include "cform_test.h"

/*
 * cf_frame_run is the pipeline the shell and the golden scenes play by hand:
 * these cases hand it a build and read the stream, the interval and the ask for
 * another frame that the whole pass leaves behind.
 */
static bool context_of(cf_ctx *ctx) {
    if (!cf_ctx_init(ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return false;
    }

    return true;
}

/* A leaf build: the answer is a value, so nothing here needs the arena. */
static cf_node one_box(cf_ctx *ctx) {
    (void)ctx;

    cf_node root = {0};
    root.style = cf_style_default();
    root.style.width = CF_PX(40.0f);
    root.style.height = CF_PX(20.0f);
    root.style.background = (cf_opt_color){.present = true, .value = CF_COLOR(9, 9, 9)};

    return root;
}

static cf_node nothing(cf_ctx *ctx) {
    (void)ctx;
    return (cf_node){0};
}

/* What one frame read of its own clock, so a case can state what the build was
 * handed rather than what it did with it. */
static float seconds_seen;

static cf_node ticking(cf_ctx *ctx) {
    seconds_seen = cf_frame_seconds(ctx);
    cf_request_frame(ctx);

    return one_box(ctx);
}

static void a_frame_runs_the_whole_pipeline_in_one_call(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    cf_command_list list = {0};
    cf_frame_run(&ctx, one_box, (cf_size){200.0f, 100.0f}, 0.0f, &list);

    CF_CHECK(cf_frame_tree(&ctx) != NULL, "the diff built a view");
    CF_CHECK(list.count == 1u, "the box's fill is the whole stream");
    CF_CHECK(list.count == 1u && list.commands[0].kind == CF_COMMAND_FILL,
             "the stream opens with the fill");
    CF_CHECK(list.count == 1u && list.commands[0].rect.width == 40.0f &&
                 list.commands[0].rect.height == 20.0f,
             "the fill sits where layout placed the box");

    cf_ctx_free(&ctx);
}

static void a_build_that_answers_nothing_paints_nothing(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    cf_command_list list = {0};
    cf_frame_run(&ctx, nothing, (cf_size){200.0f, 100.0f}, 0.0f, &list);

    CF_CHECK(list.count == 0u, "a page that states nothing paints nothing");
    CF_CHECK(cf_error_message(&ctx) == NULL, "declaring nothing is not a refusal");

    cf_ctx_free(&ctx);
}

static void a_run_without_a_build_or_a_list_is_a_no_op(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    cf_command_list list = {0};
    cf_frame_run(&ctx, NULL, (cf_size){200.0f, 100.0f}, 0.0f, &list);
    CF_CHECK(list.count == 0u && cf_frame_tree(&ctx) == NULL, "no build declares nothing");

    cf_frame_run(NULL, one_box, (cf_size){200.0f, 100.0f}, 0.0f, &list);
    cf_frame_run(&ctx, one_box, (cf_size){200.0f, 100.0f}, 0.0f, NULL);
    CF_CHECK(list.count == 0u && cf_frame_tree(&ctx) == NULL, "the guards sit idle");

    cf_ctx_free(&ctx);
}

static void a_frame_carries_its_seconds_and_may_ask_for_another(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    cf_command_list list = {0};
    seconds_seen = -1.0f;
    cf_frame_run(&ctx, ticking, (cf_size){200.0f, 100.0f}, 0.25f, &list);

    CF_NEAR(seconds_seen, 0.25f);
    CF_CHECK(cf_frame_requested(&ctx), "the ask outlives the build that made it");

    /* The ask is one frame's answer rather than a switch a build leaves on: the
     * next begin clears it, so a page that stops moving stops being drawn for. */
    cf_frame_run(&ctx, one_box, (cf_size){200.0f, 100.0f}, 0.0f, &list);
    CF_CHECK(!cf_frame_requested(&ctx), "a build that never asked leaves no ask standing");
    CF_CHECK(cf_frame_seconds(&ctx) == 0.0f, "a frame handed no interval covers none");

    CF_CHECK(cf_frame_seconds(NULL) == 0.0f, "no context covers no interval");
    CF_CHECK(!cf_frame_requested(NULL), "no context asked for nothing");

    cf_ctx_free(&ctx);
}

/* cf_frame_run replaces the stream rather than adding to it: one list handed
 * to two identical frames must answer with a single fill each time, and an
 * empty frame after a full one must leave nothing standing. bench_memory
 * reuses a list this way, so the run owns the reset the old code skipped. */
static void a_reused_list_is_replaced_not_appended(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    cf_command_list list = {0};
    cf_frame_run(&ctx, one_box, (cf_size){200.0f, 100.0f}, 0.0f, &list);
    CF_CHECK(list.count == 1u, "the first run paints one fill");

    cf_frame_run(&ctx, one_box, (cf_size){200.0f, 100.0f}, 0.0f, &list);
    CF_CHECK(list.count == 1u, "the second run on the same list still paints one fill");

    cf_frame_run(&ctx, nothing, (cf_size){200.0f, 100.0f}, 0.0f, &list);
    CF_CHECK(list.count == 0u, "a run that paints nothing empties the reused list");

    cf_ctx_free(&ctx);
}

/*
 * The store's extent is the historical peak, so a shrunk declaration leaves
 * slots behind that no claim fills. The view arrays cover the whole extent,
 * and measure_cache_init reads every style row it holds; a stale byte there
 * would show as a random width or an unexpected clip. Zeroing the rows at
 * tree_rebuild is what keeps a shrunk tree laying out like one that never
 * grew. */
#define SHRINK_LEAVES 12u

static uint32_t leaves_wanted;

static cf_node leaves_build(cf_ctx *ctx) {
    (void)ctx;

    static cf_node leaves[SHRINK_LEAVES];
    static cf_style leaf_styles[SHRINK_LEAVES];

    for (uint32_t i = 0u; i < leaves_wanted; ++i) {
        leaf_styles[i] = cf_style_default();
        leaf_styles[i].width = CF_PX(10.0f);
        leaf_styles[i].height = CF_PX(10.0f);
        leaf_styles[i].background = (cf_opt_color){.present = true, .value = CF_COLOR(1, 1, 1)};
        leaves[i] = (cf_node){.style = leaf_styles[i]};
    }

    cf_node root = {0};
    root.style = cf_style_default();
    root.style.width = CF_PX(200.0f);
    root.style.height = CF_PX(200.0f);
    root.style.background = (cf_opt_color){.present = true, .value = CF_COLOR(9, 9, 9)};
    root.children = leaves;
    root.child_count = leaves_wanted;

    return root;
}

static void a_shrunk_declaration_zeroes_the_view_rows_it_leaves_behind(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    leaves_wanted = SHRINK_LEAVES;
    cf_command_list big_list = {0};
    cf_frame_run(&ctx, leaves_build, (cf_size){200.0f, 200.0f}, 0.0f, &big_list);

    const uint32_t peak = (uint32_t)ctx.nodes.extent;

    leaves_wanted = 2u;
    cf_command_list small_list = {0};
    cf_frame_run(&ctx, leaves_build, (cf_size){200.0f, 200.0f}, 0.0f, &small_list);

    CF_CHECK(peak > 2u, "the big tree pushed the store past the small one");
    CF_CHECK(ctx.layout.node_count == peak, "the view covers the store, not just the live slots");

    /* Walk preorder from the root to name the live slots; every slot outside
     * the walk is one the current claim did not fill. */
    bool live[SHRINK_LEAVES + 2u] = {0};
    uint32_t live_count = 0u;

    for (uint32_t edge = 0u; edge < ctx.layout.child_start[ctx.layout.node_count]; ++edge) {
        const uint32_t child = ctx.layout.child_slots[edge];
        CF_CHECK(child < ctx.layout.node_count, "a child edge stays inside the view");

        if (!live[child]) {
            live[child] = true;
            ++live_count;
        }
    }

    if (!live[ctx.root_slot]) {
        live[ctx.root_slot] = true;
        ++live_count;
    }

    CF_CHECK(live_count == ctx.nodes.alive, "the CSR walk visits exactly the live slots");

    /* Every unclaimed row must read as a documented start, not as what the
     * big tree left behind. A zeroed length is CF_LENGTH_AUTO, and the two
     * callbacks become NULL; that is what keeps measure_cache_init safe. */
    for (uint32_t slot = 0u; slot < ctx.layout.node_count; ++slot) {
        if (live[slot]) {
            continue;
        }

        CF_CHECK(ctx.layout.styles[slot].width.kind == CF_LENGTH_AUTO &&
                     ctx.layout.styles[slot].height.kind == CF_LENGTH_AUTO,
                 "a retired style row reads as AUTO lengths");
        CF_CHECK(ctx.layout.measures[slot] == NULL, "a retired measure callback is NULL");
        CF_CHECK(ctx.layout.measure_data[slot] == NULL, "a retired measure datum is NULL");
    }

    /* Layout of the small tree after the big one must match the small tree
     * run alone: the holes the shrunk claim leaves behind must not shift
     * anything the walk from the root actually reads. */
    cf_ctx fresh;

    if (!context_of(&fresh)) {
        cf_ctx_free(&ctx);
        return;
    }

    leaves_wanted = 2u;
    cf_command_list fresh_list = {0};
    cf_frame_run(&fresh, leaves_build, (cf_size){200.0f, 200.0f}, 0.0f, &fresh_list);

    CF_CHECK(small_list.count == fresh_list.count, "a shrunk view paints the same stream");

    for (uint32_t i = 0u; i < small_list.count && i < fresh_list.count; ++i) {
        CF_CHECK(small_list.commands[i].kind == fresh_list.commands[i].kind &&
                     small_list.commands[i].rect.x == fresh_list.commands[i].rect.x &&
                     small_list.commands[i].rect.y == fresh_list.commands[i].rect.y &&
                     small_list.commands[i].rect.width == fresh_list.commands[i].rect.width &&
                     small_list.commands[i].rect.height == fresh_list.commands[i].rect.height,
                 "one command matches the fresh-run answer");
    }

    leaves_wanted = 0u;
    cf_ctx_free(&ctx);
    cf_ctx_free(&fresh);
}

int main(void) {
    printf("node frame run\n");
    CF_RUN(a_frame_runs_the_whole_pipeline_in_one_call);
    CF_RUN(a_build_that_answers_nothing_paints_nothing);
    CF_RUN(a_run_without_a_build_or_a_list_is_a_no_op);
    CF_RUN(a_frame_carries_its_seconds_and_may_ask_for_another);
    CF_RUN(a_reused_list_is_replaced_not_appended);
    CF_RUN(a_shrunk_declaration_zeroes_the_view_rows_it_leaves_behind);

    return CF_REPORT();
}
