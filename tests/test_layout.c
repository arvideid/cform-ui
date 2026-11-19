#include "cform_test.h"

#include <cform/cform.h>
#include "internal.h"

static void layout_fixed_children_pack_left_to_right(void) {
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

static void layout_gap_adds_space_between_children(void) {
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
    CF_NEAR(b->x, 40);

    ui_destroy(ctx);
}

static void layout_fill_splits_remaining_space(void) {
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
    CF_NEAR(b->width, 70);

    ui_destroy(ctx);
}

static void layout_padding_insets_content(void) {
    UI_Context *ctx = ui_create();

    UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){
        .direction = UI_HORIZONTAL, .padding = UI_ALL(20) });
    UI_Frame *a = ui_frame(ctx, (UI_FrameConfig){
        .width = UI_FILL, .height = UI_FILL });
    ui_add(root, a);

    ui_layout_frame(root, 0.0f, 0.0f, 100.0f, 100.0f);

    CF_NEAR(a->x, 20);
    CF_NEAR(a->y, 20);
    CF_NEAR(a->width, 60);
    CF_NEAR(a->height, 60);

    ui_destroy(ctx);
}

static void layout_margin_insets_child(void) {
    UI_Context *ctx = ui_create();

    UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){
        .direction = UI_HORIZONTAL });
    UI_Frame *a = ui_frame(ctx, (UI_FrameConfig){
        .width = UI_FILL, .height = UI_FILL, .margin = UI_ALL(10) });
    ui_add(root, a);

    ui_layout_frame(root, 0.0f, 0.0f, 100.0f, 100.0f);

    CF_NEAR(a->x, 10);
    CF_NEAR(a->y, 10);
    CF_NEAR(a->width, 80);
    CF_NEAR(a->height, 80);

    ui_destroy(ctx);
}

static void layout_vertical_children_stack_top_to_bottom(void) {
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
    CF_NEAR(b->y, 40);
    CF_NEAR(b->height, 40);

    ui_destroy(ctx);
}

static void layout_nested_tree_resolves_recursively(void) {
    UI_Context *ctx = ui_create();

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

    CF_NEAR(inner->x, 50);
    CF_NEAR(inner->y, 10);
    CF_NEAR(inner->width, 140);
    CF_NEAR(inner->height, 20);

    ui_destroy(ctx);
}

static void layout_fixed_child_clamps_to_max_width(void) {
    UI_Context *ctx = ui_create();

    UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){
        .direction = UI_HORIZONTAL });
    UI_Frame *a = ui_frame(ctx, (UI_FrameConfig){
        .width = UI_FIXED(200), .height = UI_FIXED(40), .max_width = 120 });
    ui_add(root, a);

    ui_layout_frame(root, 0.0f, 0.0f, 300.0f, 40.0f);

    CF_NEAR(a->width, 120);

    ui_destroy(ctx);
}

static void layout_fixed_child_clamps_to_min_width(void) {
    UI_Context *ctx = ui_create();

    UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){
        .direction = UI_HORIZONTAL });
    UI_Frame *a = ui_frame(ctx, (UI_FrameConfig){
        .width = UI_FIXED(30), .height = UI_FIXED(40), .min_width = 60 });
    ui_add(root, a);

    ui_layout_frame(root, 0.0f, 0.0f, 300.0f, 40.0f);

    CF_NEAR(a->width, 60);

    ui_destroy(ctx);
}

static void layout_fill_child_respects_max_width(void) {
    UI_Context *ctx = ui_create();

    UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){
        .direction = UI_HORIZONTAL });
    UI_Frame *a = ui_frame(ctx, (UI_FrameConfig){
        .width = UI_FILL, .height = UI_FIXED(40), .max_width = 40 });
    ui_add(root, a);

    ui_layout_frame(root, 0.0f, 0.0f, 200.0f, 40.0f);

    CF_NEAR(a->width, 40);

    ui_destroy(ctx);
}

static void layout_fill_child_respects_min_width(void) {
    UI_Context *ctx = ui_create();

    UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){
        .direction = UI_HORIZONTAL });
    UI_Frame *a = ui_frame(ctx, (UI_FrameConfig){
        .width = UI_FIXED(200), .height = UI_FIXED(40) });
    UI_Frame *b = ui_frame(ctx, (UI_FrameConfig){
        .width = UI_FILL, .height = UI_FIXED(40), .min_width = 60 });
    ui_add(root, a);
    ui_add(root, b);

    ui_layout_frame(root, 0.0f, 0.0f, 220.0f, 40.0f);

    /* Fill would be 20, but the child clamps up to its min. */
    CF_NEAR(b->width, 60);

    ui_destroy(ctx);
}

static void layout_cross_axis_clamps_fill_height(void) {
    UI_Context *ctx = ui_create();

    UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){
        .direction = UI_HORIZONTAL });
    UI_Frame *a = ui_frame(ctx, (UI_FrameConfig){
        .width = UI_FIXED(50), .height = UI_FILL, .max_height = 30 });
    ui_add(root, a);

    ui_layout_frame(root, 0.0f, 0.0f, 50.0f, 100.0f);

    CF_NEAR(a->height, 30);

    ui_destroy(ctx);
}

int main(void) {
    printf("test_layout\n");
    CF_RUN(layout_fixed_children_pack_left_to_right);
    CF_RUN(layout_gap_adds_space_between_children);
    CF_RUN(layout_fill_splits_remaining_space);
    CF_RUN(layout_padding_insets_content);
    CF_RUN(layout_margin_insets_child);
    CF_RUN(layout_vertical_children_stack_top_to_bottom);
    CF_RUN(layout_nested_tree_resolves_recursively);
    CF_RUN(layout_fixed_child_clamps_to_max_width);
    CF_RUN(layout_fixed_child_clamps_to_min_width);
    CF_RUN(layout_fill_child_respects_max_width);
    CF_RUN(layout_fill_child_respects_min_width);
    CF_RUN(layout_cross_axis_clamps_fill_height);
    return CF_REPORT();
}
