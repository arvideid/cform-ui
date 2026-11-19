/*
 * Frame and context tests - object lifecycle, config copying and tree wiring.
 * No layout math here; just that frames are created, linked and registered.
 */
#include "cform_test.h"

#include <cform/cform.h>
#include "internal.h"

static void test_create_has_defaults(void) {
    UI_Context *ctx = ui_create();

    CF_CHECK(ctx != NULL, "ui_create returns a context");
    CF_NEAR(ctx->window_width, 1280);
    CF_NEAR(ctx->frame_count, 0);

    ui_destroy(ctx);
}

static void test_frame_copies_config(void) {
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

static void test_add_links_tree(void) {
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

static void test_children_array_grows(void) {
    UI_Context *ctx = ui_create();

    UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){0});

    /* More children than the initial capacity of 4, to exercise the growth. */
    for (int i = 0; i < 10; i++) {
        ui_add(root, ui_frame(ctx, (UI_FrameConfig){0}));
    }

    CF_NEAR(root->child_count, 10);

    ui_destroy(ctx);
}

static void test_frames_registered_in_context(void) {
    UI_Context *ctx = ui_create();

    for (int i = 0; i < 20; i++) {
        ui_frame(ctx, (UI_FrameConfig){0});
    }

    /* Context keeps every frame so ui_destroy can free them in one pass. */
    CF_NEAR(ctx->frame_count, 20);

    ui_destroy(ctx);
}

static void test_null_inputs_are_safe(void) {
    CF_CHECK(ui_frame(NULL, (UI_FrameConfig){0}) == NULL,
             "ui_frame with NULL context returns NULL");

    UI_Context *ctx = ui_create();
    UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){0});

    ui_add(NULL, NULL);          /* no crash */
    ui_add(root, NULL);          /* ignored */
    CF_NEAR(root->child_count, 0);

    ui_destroy(ctx);
    ui_destroy(NULL);            /* no crash */
}

int main(void) {
    printf("test_frame\n");
    CF_RUN(test_create_has_defaults);
    CF_RUN(test_frame_copies_config);
    CF_RUN(test_add_links_tree);
    CF_RUN(test_children_array_grows);
    CF_RUN(test_frames_registered_in_context);
    CF_RUN(test_null_inputs_are_safe);
    return CF_REPORT();
}
