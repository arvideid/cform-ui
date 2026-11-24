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

int main(void) {
    printf("node frame run\n");
    CF_RUN(a_frame_runs_the_whole_pipeline_in_one_call);
    CF_RUN(a_build_that_answers_nothing_paints_nothing);
    CF_RUN(a_run_without_a_build_or_a_list_is_a_no_op);
    CF_RUN(a_frame_carries_its_seconds_and_may_ask_for_another);

    return CF_REPORT();
}
