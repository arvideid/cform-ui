#include "cf/node.h"

#include "cform_test.h"

/*
 * cf_frame_run is the pipeline the shell and the golden scenes play by hand:
 * these cases hand it a build and read the stream the whole pass leaves.
 */
static bool context_of(cf_ctx *ctx) {
    if (!cf_ctx_init(ctx, 64u * 1024u, 4u * 1024u)) {
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

static void a_frame_runs_the_whole_pipeline_in_one_call(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    cf_command_list list = {0};
    cf_frame_run(&ctx, one_box, (cf_size){200.0f, 100.0f}, &list);

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
    cf_frame_run(&ctx, nothing, (cf_size){200.0f, 100.0f}, &list);

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
    cf_frame_run(&ctx, NULL, (cf_size){200.0f, 100.0f}, &list);
    CF_CHECK(list.count == 0u && cf_frame_tree(&ctx) == NULL, "no build declares nothing");

    cf_frame_run(NULL, one_box, (cf_size){200.0f, 100.0f}, &list);
    cf_frame_run(&ctx, one_box, (cf_size){200.0f, 100.0f}, NULL);
    CF_CHECK(list.count == 0u && cf_frame_tree(&ctx) == NULL, "the guards sit idle");

    cf_ctx_free(&ctx);
}

int main(void) {
    printf("node frame run\n");
    CF_RUN(a_frame_runs_the_whole_pipeline_in_one_call);
    CF_RUN(a_build_that_answers_nothing_paints_nothing);
    CF_RUN(a_run_without_a_build_or_a_list_is_a_no_op);

    return CF_REPORT();
}
