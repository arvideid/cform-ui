#include "cform_test.h"

#include <cform/cform.h>
#include "internal.h"

static void context_create_returns_valid_defaults(void) {
    UI_Context *ctx = ui_create();

    CF_CHECK(ctx != NULL, "ui_create returns a context");
    CF_NEAR(ctx->window_width, 1280);
    CF_NEAR(ctx->frame_count, 0);

    ui_destroy(ctx);
}

static void frame_copies_config_by_value(void) {
    UI_Context *ctx = ui_create();

    UI_Frame *f = ui_frame(ctx, (UI_FrameConfig){
        .width = UI_FIXED(30), .height = UI_FILL });

    CF_CHECK(f != NULL, "ui_frame returns a frame");
    CF_NEAR(f->config.width.value, 30);
    CF_CHECK(f->config.width.type == UI_SIZE_FIXED, "width is fixed");
    CF_CHECK(f->config.height.type == UI_SIZE_FILL, "height is fill");
    CF_NEAR(f->child_count, 0);
    CF_CHECK(f->parent == NULL, "detached frame has no parent");

    ui_destroy(ctx);
}

static void add_links_child_to_parent(void) {
    UI_Context *ctx = ui_create();

    UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){0});
    UI_Frame *a = ui_frame(ctx, (UI_FrameConfig){0});
    UI_Frame *b = ui_frame(ctx, (UI_FrameConfig){0});

    ui_add(root, a);
    ui_add(root, b);

    CF_NEAR(root->child_count, 2);
    CF_CHECK(root->children[0] == a, "first child is a");
    CF_CHECK(root->children[1] == b, "second child is b");
    CF_CHECK(a->parent == root, "a's parent is root");
    CF_CHECK(b->parent == root, "b's parent is root");

    ui_destroy(ctx);
}

static void children_array_grows_by_doubling(void) {
    UI_Context *ctx = ui_create();

    UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){0});

    /* More children than the initial capacity of 4, to exercise the growth. */
    for (int i = 0; i < 10; i++) {
        ui_add(root, ui_frame(ctx, (UI_FrameConfig){0}));
    }

    CF_NEAR(root->child_count, 10);

    ui_destroy(ctx);
}

static void context_registers_every_frame(void) {
    UI_Context *ctx = ui_create();

    for (int i = 0; i < 20; i++) {
        ui_frame(ctx, (UI_FrameConfig){0});
    }

    CF_NEAR(ctx->frame_count, 20);

    ui_destroy(ctx);
}

static void null_inputs_are_silent_no_ops(void) {
    CF_CHECK(ui_frame(NULL, (UI_FrameConfig){0}) == NULL,
             "ui_frame with NULL context returns NULL");

    UI_Context *ctx = ui_create();
    UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){0});

    ui_add(NULL, NULL);          /* no crash */
    ui_add(root, NULL);
    CF_NEAR(root->child_count, 0);

    ui_destroy(ctx);
    ui_destroy(NULL);            /* no crash */
}

int main(void) {
    printf("test_frame\n");
    CF_RUN(context_create_returns_valid_defaults);
    CF_RUN(frame_copies_config_by_value);
    CF_RUN(add_links_child_to_parent);
    CF_RUN(children_array_grows_by_doubling);
    CF_RUN(context_registers_every_frame);
    CF_RUN(null_inputs_are_silent_no_ops);
    return CF_REPORT();
}
