#include "cf/node.h"
#include "cform_test.h"

static bool context_of(cf_ctx *ctx) {
    if (!cf_ctx_init(ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return false;
    }

    return true;
}

static void a_fresh_tween_answers_its_start(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    cf_frame_begin(&ctx);

    const cf_name width = cf_intern_text(&ctx.names, "width");
    cf_tween *tween = cf_tween_of(&ctx, width);

    CF_CHECK(tween != NULL, "the block is served");
    CF_CHECK(tween != NULL && cf_tween_step(tween, 0.5f) == 0.0f,
             "the zero state holds a value that goes nowhere");

    if (tween != NULL) {
        cf_tween_to(tween, 10.0f, 0.0f, CF_EASE_LINEAR);
        CF_CHECK(cf_tween_step(tween, 0.5f) == 10.0f,
                 "a duration the frame cannot divide snaps to the end");
    }

    CF_CHECK(cf_tween_of(&ctx, CF_NO_NAME) == NULL, "a nameless request is refused");

    cf_ctx_free(&ctx);
}

static void a_linear_tween_moves_by_the_share_of_time(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    cf_frame_begin(&ctx);

    cf_tween *tween = cf_tween_of(&ctx, cf_intern_text(&ctx.names, "width"));

    if (tween == NULL) {
        CF_CHECK(false, "the block is served");
        cf_ctx_free(&ctx);
        return;
    }

    cf_tween_to(tween, 100.0f, 10.0f, CF_EASE_LINEAR);

    CF_NEAR(cf_tween_step(tween, 5.0f), 50.0f);
    CF_NEAR(cf_tween_step(tween, 5.0f), 100.0f);

    cf_ctx_free(&ctx);
}

static void an_ease_out_settles_into_the_target(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    cf_frame_begin(&ctx);

    cf_tween *tween = cf_tween_of(&ctx, cf_intern_text(&ctx.names, "width"));

    if (tween == NULL) {
        CF_CHECK(false, "the block is served");
        cf_ctx_free(&ctx);
        return;
    }

    cf_tween_to(tween, 100.0f, 10.0f, CF_EASE_OUT);

    /* A quarter of the way in, a quadratic out has covered seven sixteenths. */
    CF_NEAR(cf_tween_step(tween, 2.5f), 43.75f);
    CF_NEAR(cf_tween_step(tween, 7.5f), 100.0f);

    cf_ctx_free(&ctx);
}

static void an_aim_while_running_restarts_from_where_it_sits(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    cf_frame_begin(&ctx);

    cf_tween *tween = cf_tween_of(&ctx, cf_intern_text(&ctx.names, "width"));

    if (tween == NULL) {
        CF_CHECK(false, "the block is served");
        cf_ctx_free(&ctx);
        return;
    }

    cf_tween_to(tween, 100.0f, 10.0f, CF_EASE_LINEAR);
    CF_NEAR(cf_tween_step(tween, 5.0f), 50.0f);

    cf_tween_to(tween, 200.0f, 10.0f, CF_EASE_LINEAR);
    CF_NEAR(cf_tween_step(tween, 5.0f), 125.0f);

    cf_ctx_free(&ctx);
}

static void a_finished_tween_keeps_answering_its_end(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    cf_frame_begin(&ctx);

    cf_tween *tween = cf_tween_of(&ctx, cf_intern_text(&ctx.names, "width"));

    if (tween == NULL) {
        CF_CHECK(false, "the block is served");
        cf_ctx_free(&ctx);
        return;
    }

    cf_tween_to(tween, 100.0f, 2.0f, CF_EASE_LINEAR);
    CF_NEAR(cf_tween_step(tween, 10.0f), 100.0f);
    CF_NEAR(cf_tween_step(tween, 50.0f), 100.0f);

    cf_ctx_free(&ctx);
}

static void a_block_survives_rebuilds_under_its_name(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    const cf_name width = cf_intern_text(&ctx.names, "width");
    const cf_node widget = {.key = width};
    cf_tween *tween = NULL;

    cf_frame_begin(&ctx);
    cf_diff(&ctx, &widget);
    tween = cf_tween_of(&ctx, width);

    if (tween != NULL) {
        cf_tween_to(tween, 100.0f, 10.0f, CF_EASE_LINEAR);
        cf_tween_step(tween, 5.0f);
    }

    cf_frame_begin(&ctx);
    cf_diff(&ctx, &widget);
    tween = cf_tween_of(&ctx, width);

    CF_CHECK(tween != NULL, "the block survives the rebuild");
    CF_NEAR(tween != NULL ? cf_tween_step(tween, 5.0f) : 0.0f, 100.0f);

    cf_ctx_free(&ctx);
}

int main(void) {
    printf("node anim\n");

    CF_RUN(a_fresh_tween_answers_its_start);
    CF_RUN(a_linear_tween_moves_by_the_share_of_time);
    CF_RUN(an_ease_out_settles_into_the_target);
    CF_RUN(an_aim_while_running_restarts_from_where_it_sits);
    CF_RUN(a_finished_tween_keeps_answering_its_end);
    CF_RUN(a_block_survives_rebuilds_under_its_name);

    return CF_REPORT();
}
