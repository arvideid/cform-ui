/*
 * Layout tests - the heart of the library, exercised headlessly.
 *
 * ui_layout_frame() resolves a frame tree against a given rect, so each test
 * builds a small tree, lays it out at a known size and asserts on the computed
 * x / y / width / height of the children.
 */
#include "cform_test.h"

#include <cform/cform.h>
#include "internal.h"

static void test_fixed_horizontal(void) {
    UI_Context *ctx = ui_create();

    UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){
        .direction = UI_HORIZONTAL, .gap = 0.0f });
    UI_Frame *a = ui_frame(ctx, (UI_FrameConfig){
        .width = UI_FIXED(30), .height = UI_FIXED(50) });
    UI_Frame *b = ui_frame(ctx, (UI_FrameConfig){
        .width = UI_FIXED(40), .height = UI_FIXED(50) });
    ui_add(root, a);
    ui_add(root, b);

    ui_layout_frame(root, 0.0f, 0.0f, 100.0f, 50.0f);

    CF_NEAR(a->x, 0);
    CF_NEAR(a->width, 30);
    CF_NEAR(b->x, 30);
    CF_NEAR(b->width, 40);
    CF_NEAR(b->x + b->width, 70);

    ui_destroy(ctx);
}

static void test_gap_adds_space(void) {
    UI_Context *ctx = ui_create();

    UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){
        .direction = UI_HORIZONTAL, .gap = 10.0f });
    UI_Frame *a = ui_frame(ctx, (UI_FrameConfig){
        .width = UI_FIXED(30), .height = UI_FIXED(50) });
    UI_Frame *b = ui_frame(ctx, (UI_FrameConfig){
        .width = UI_FIXED(40), .height = UI_FIXED(50) });
    ui_add(root, a);
    ui_add(root, b);

    ui_layout_frame(root, 0.0f, 0.0f, 100.0f, 50.0f);

    CF_NEAR(a->x, 0);
    CF_NEAR(b->x, 40); /* 30 + gap */

    ui_destroy(ctx);
}

static void test_fill_shares_remaining_space(void) {
    UI_Context *ctx = ui_create();

    UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){
        .direction = UI_HORIZONTAL, .gap = 0.0f });
    UI_Frame *a = ui_frame(ctx, (UI_FrameConfig){
        .width = UI_FIXED(30), .height = UI_FIXED(50) });
    UI_Frame *b = ui_frame(ctx, (UI_FrameConfig){
        .width = UI_FILL, .height = UI_FIXED(50) });
    ui_add(root, a);
    ui_add(root, b);

    ui_layout_frame(root, 0.0f, 0.0f, 100.0f, 50.0f);

    CF_NEAR(b->x, 30);
    CF_NEAR(b->width, 70); /* the whole remainder */

    ui_destroy(ctx);
}

static void test_padding_insets_content(void) {
    UI_Context *ctx = ui_create();

    UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){
        .direction = UI_HORIZONTAL, .padding = UI_ALL(20) });
    UI_Frame *a = ui_frame(ctx, (UI_FrameConfig){
        .width = UI_FILL, .height = UI_FILL });
    ui_add(root, a);

    ui_layout_frame(root, 0.0f, 0.0f, 100.0f, 100.0f);

    CF_NEAR(a->x, 20);
    CF_NEAR(a->y, 20);
    CF_NEAR(a->width, 60);  /* 100 - 20 - 20 */
    CF_NEAR(a->height, 60);

    ui_destroy(ctx);
}

static void test_margin_insets_child(void) {
    UI_Context *ctx = ui_create();

    UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){
        .direction = UI_HORIZONTAL });
    UI_Frame *a = ui_frame(ctx, (UI_FrameConfig){
        .width = UI_FILL, .height = UI_FILL, .margin = UI_ALL(10) });
    ui_add(root, a);

    ui_layout_frame(root, 0.0f, 0.0f, 100.0f, 100.0f);

    CF_NEAR(a->x, 10);
    CF_NEAR(a->y, 10);
    CF_NEAR(a->width, 80);  /* 100 - 10 - 10 */
    CF_NEAR(a->height, 80);

    ui_destroy(ctx);
}

static void test_vertical_stacking(void) {
    UI_Context *ctx = ui_create();

    UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){
        .direction = UI_VERTICAL, .gap = 10.0f });
    UI_Frame *a = ui_frame(ctx, (UI_FrameConfig){
        .width = UI_FIXED(50), .height = UI_FIXED(30) });
    UI_Frame *b = ui_frame(ctx, (UI_FrameConfig){
        .width = UI_FIXED(50), .height = UI_FIXED(40) });
    ui_add(root, a);
    ui_add(root, b);

    ui_layout_frame(root, 0.0f, 0.0f, 50.0f, 100.0f);

    CF_NEAR(a->y, 0);
    CF_NEAR(b->y, 40); /* 30 + gap */
    CF_NEAR(b->height, 40);

    ui_destroy(ctx);
}

static void test_nested_tree(void) {
    UI_Context *ctx = ui_create();

    /* root(H) -> [ fixed 40 sidebar , fill main ]  at 200x100 */
    UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){
        .direction = UI_HORIZONTAL, .gap = 0.0f });
    UI_Frame *side = ui_frame(ctx, (UI_FrameConfig){
        .width = UI_FIXED(40), .height = UI_FILL });
    UI_Frame *main = ui_frame(ctx, (UI_FrameConfig){
        .width = UI_FILL, .height = UI_FILL, .padding = UI_ALL(10),
        .direction = UI_VERTICAL });
    UI_Frame *inner = ui_frame(ctx, (UI_FrameConfig){
        .width = UI_FILL, .height = UI_FIXED(20) });
    ui_add(root, side);
    ui_add(root, main);
    ui_add(main, inner);

    ui_layout_frame(root, 0.0f, 0.0f, 200.0f, 100.0f);

    CF_NEAR(side->x, 0);
    CF_NEAR(side->width, 40);
    CF_NEAR(side->height, 100);

    CF_NEAR(main->x, 40);
    CF_NEAR(main->width, 160);

    /* inner lives inside main's 10px padding */
    CF_NEAR(inner->x, 50);
    CF_NEAR(inner->y, 10);
    CF_NEAR(inner->width, 140); /* 160 - 10 - 10 */
    CF_NEAR(inner->height, 20);

    ui_destroy(ctx);
}

int main(void) {
    printf("test_layout\n");
    CF_RUN(test_fixed_horizontal);
    CF_RUN(test_gap_adds_space);
    CF_RUN(test_fill_shares_remaining_space);
    CF_RUN(test_padding_insets_content);
    CF_RUN(test_margin_insets_child);
    CF_RUN(test_vertical_stacking);
    CF_RUN(test_nested_tree);
    return CF_REPORT();
}
