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

static void layout_align_center_offsets_fixed_child_on_cross_axis(void) {
    UI_Context *ctx = ui_create();

    UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){
        .direction = UI_HORIZONTAL, .align_items = UI_ALIGN_CENTER });
    UI_Frame *a = ui_frame(ctx, (UI_FrameConfig){
        .width = UI_FIXED(30), .height = UI_FIXED(40) });
    ui_add(root, a);

    ui_layout_frame(root, 0.0f, 0.0f, 100.0f, 100.0f);

    CF_NEAR(a->y, 30);
    CF_NEAR(a->height, 40);

    ui_destroy(ctx);
}

static void layout_align_end_positions_fixed_child_at_far_edge(void) {
    UI_Context *ctx = ui_create();

    UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){
        .direction = UI_HORIZONTAL, .align_items = UI_ALIGN_END });
    UI_Frame *a = ui_frame(ctx, (UI_FrameConfig){
        .width = UI_FIXED(30), .height = UI_FIXED(40) });
    ui_add(root, a);

    ui_layout_frame(root, 0.0f, 0.0f, 100.0f, 100.0f);

    CF_NEAR(a->y, 60);

    ui_destroy(ctx);
}

static void layout_align_stretch_grows_fixed_child_on_cross_axis(void) {
    UI_Context *ctx = ui_create();

    UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){
        .direction = UI_HORIZONTAL, .align_items = UI_ALIGN_STRETCH });
    UI_Frame *a = ui_frame(ctx, (UI_FrameConfig){
        .width = UI_FIXED(30), .height = UI_FIXED(40) });
    ui_add(root, a);

    ui_layout_frame(root, 0.0f, 0.0f, 100.0f, 100.0f);

    CF_NEAR(a->y, 0);
    CF_NEAR(a->height, 100);

    ui_destroy(ctx);
}

static void layout_align_respects_child_margin_on_cross_axis(void) {
    UI_Context *ctx = ui_create();

    UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){
        .direction = UI_HORIZONTAL, .align_items = UI_ALIGN_CENTER });
    UI_Frame *a = ui_frame(ctx, (UI_FrameConfig){
        .width = UI_FIXED(30), .height = UI_FIXED(40),
        .margin = UI_EDGES(6.0f, 0.0f, 2.0f, 0.0f) });
    ui_add(root, a);

    ui_layout_frame(root, 0.0f, 0.0f, 100.0f, 100.0f);

    /* Room = 100 - 6 - 2 = 92. Extra = 92 - 40 = 52. Half = 26, plus leading
     * margin 6 puts the child 32 pixels from the content-box top. */
    CF_NEAR(a->y, 32);

    ui_destroy(ctx);
}

/* Root is 210 wide; three 30-wide children consume 90 and leave 120 for
 * justify to distribute. Every expected position is an integer. */
static void make_justify_row(UI_Context *ctx, UI_Justify justify,
                             UI_Frame **out_a, UI_Frame **out_b,
                             UI_Frame **out_c) {
    UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){
        .direction = UI_HORIZONTAL, .justify_content = justify });
    *out_a = ui_frame(ctx, (UI_FrameConfig){ .width = UI_FIXED(30) });
    *out_b = ui_frame(ctx, (UI_FrameConfig){ .width = UI_FIXED(30) });
    *out_c = ui_frame(ctx, (UI_FrameConfig){ .width = UI_FIXED(30) });
    ui_add(root, *out_a);
    ui_add(root, *out_b);
    ui_add(root, *out_c);
    ui_layout_frame(root, 0.0f, 0.0f, 210.0f, 100.0f);
}

static void layout_justify_center_shifts_children_to_midpoint(void) {
    UI_Context *ctx = ui_create();
    UI_Frame *a; UI_Frame *b; UI_Frame *c;
    make_justify_row(ctx, UI_JUSTIFY_CENTER, &a, &b, &c);

    CF_NEAR(a->x, 60);
    CF_NEAR(b->x, 90);
    CF_NEAR(c->x, 120);

    ui_destroy(ctx);
}

static void layout_justify_end_shifts_children_to_far_edge(void) {
    UI_Context *ctx = ui_create();
    UI_Frame *a; UI_Frame *b; UI_Frame *c;
    make_justify_row(ctx, UI_JUSTIFY_END, &a, &b, &c);

    CF_NEAR(a->x, 120);
    CF_NEAR(b->x, 150);
    CF_NEAR(c->x, 180);

    ui_destroy(ctx);
}

static void layout_justify_space_between_packs_edges_tight(void) {
    UI_Context *ctx = ui_create();
    UI_Frame *a; UI_Frame *b; UI_Frame *c;
    make_justify_row(ctx, UI_JUSTIFY_SPACE_BETWEEN, &a, &b, &c);

    CF_NEAR(a->x, 0);
    CF_NEAR(b->x, 90);
    CF_NEAR(c->x, 180);

    ui_destroy(ctx);
}

static void layout_justify_space_around_adds_half_gaps_at_edges(void) {
    UI_Context *ctx = ui_create();
    UI_Frame *a; UI_Frame *b; UI_Frame *c;
    make_justify_row(ctx, UI_JUSTIFY_SPACE_AROUND, &a, &b, &c);

    CF_NEAR(a->x, 20);
    CF_NEAR(b->x, 90);
    CF_NEAR(c->x, 160);

    ui_destroy(ctx);
}

static void layout_justify_space_evenly_adds_full_gaps_at_edges(void) {
    UI_Context *ctx = ui_create();
    UI_Frame *a; UI_Frame *b; UI_Frame *c;
    make_justify_row(ctx, UI_JUSTIFY_SPACE_EVENLY, &a, &b, &c);

    CF_NEAR(a->x, 30);
    CF_NEAR(b->x, 90);
    CF_NEAR(c->x, 150);

    ui_destroy(ctx);
}

static void layout_justify_with_fill_child_leaves_no_extra_space(void) {
    UI_Context *ctx = ui_create();

    UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){
        .direction = UI_HORIZONTAL, .justify_content = UI_JUSTIFY_CENTER });
    UI_Frame *a = ui_frame(ctx, (UI_FrameConfig){ .width = UI_FILL });
    ui_add(root, a);

    ui_layout_frame(root, 0.0f, 0.0f, 200.0f, 100.0f);

    CF_NEAR(a->x, 0);
    CF_NEAR(a->width, 200);

    ui_destroy(ctx);
}

static void layout_justify_space_between_single_child_behaves_like_start(void) {
    UI_Context *ctx = ui_create();

    UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){
        .direction = UI_HORIZONTAL,
        .justify_content = UI_JUSTIFY_SPACE_BETWEEN });
    UI_Frame *a = ui_frame(ctx, (UI_FrameConfig){ .width = UI_FIXED(30) });
    ui_add(root, a);

    ui_layout_frame(root, 0.0f, 0.0f, 100.0f, 100.0f);

    CF_NEAR(a->x, 0);

    ui_destroy(ctx);
}

static void layout_overlay_stacks_children_at_parent_origin(void) {
    UI_Context *ctx = ui_create();

    UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){
        .direction = UI_OVERLAY });
    UI_Frame *base = ui_frame(ctx, (UI_FrameConfig){
        .width = UI_FIXED(100), .height = UI_FIXED(100) });
    UI_Frame *badge = ui_frame(ctx, (UI_FrameConfig){
        .width = UI_FIXED(20), .height = UI_FIXED(20) });
    ui_add(root, base);
    ui_add(root, badge);

    ui_layout_frame(root, 0.0f, 0.0f, 100.0f, 100.0f);

    CF_NEAR(base->x, 0);
    CF_NEAR(base->y, 0);
    CF_NEAR(badge->x, 0);
    CF_NEAR(badge->y, 0);
    CF_NEAR(badge->width, 20);
    CF_NEAR(badge->height, 20);

    ui_destroy(ctx);
}

static void layout_overlay_child_margin_offsets_from_origin(void) {
    UI_Context *ctx = ui_create();

    UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){
        .direction = UI_OVERLAY, .padding = UI_ALL(5.0f) });
    UI_Frame *badge = ui_frame(ctx, (UI_FrameConfig){
        .width = UI_FIXED(20), .height = UI_FIXED(20),
        .margin = UI_EDGES(3.0f, 0.0f, 0.0f, 8.0f) });
    ui_add(root, badge);

    ui_layout_frame(root, 0.0f, 0.0f, 100.0f, 100.0f);

    /* Content origin is (padding.left, padding.top) = (5, 5); child slides to
     * (5 + margin.left, 5 + margin.top) = (13, 8). */
    CF_NEAR(badge->x, 13);
    CF_NEAR(badge->y, 8);

    ui_destroy(ctx);
}

static void layout_overlay_fill_child_expands_to_parent_content(void) {
    UI_Context *ctx = ui_create();

    UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){
        .direction = UI_OVERLAY, .padding = UI_ALL(10.0f) });
    UI_Frame *sheet = ui_frame(ctx, (UI_FrameConfig){
        .width = UI_FILL, .height = UI_FILL });
    ui_add(root, sheet);

    ui_layout_frame(root, 0.0f, 0.0f, 200.0f, 100.0f);

    CF_NEAR(sheet->x, 10);
    CF_NEAR(sheet->y, 10);
    CF_NEAR(sheet->width, 180);
    CF_NEAR(sheet->height, 80);

    ui_destroy(ctx);
}

static void layout_overlay_clamps_child_size(void) {
    UI_Context *ctx = ui_create();

    UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){
        .direction = UI_OVERLAY });
    UI_Frame *a = ui_frame(ctx, (UI_FrameConfig){
        .width = UI_FIXED(100), .height = UI_FIXED(100),
        .max_width = 40.0f, .min_height = 60.0f });
    ui_add(root, a);

    ui_layout_frame(root, 0.0f, 0.0f, 200.0f, 200.0f);

    CF_NEAR(a->width, 40);
    CF_NEAR(a->height, 100);

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
    CF_RUN(layout_align_center_offsets_fixed_child_on_cross_axis);
    CF_RUN(layout_align_end_positions_fixed_child_at_far_edge);
    CF_RUN(layout_align_stretch_grows_fixed_child_on_cross_axis);
    CF_RUN(layout_align_respects_child_margin_on_cross_axis);
    CF_RUN(layout_justify_center_shifts_children_to_midpoint);
    CF_RUN(layout_justify_end_shifts_children_to_far_edge);
    CF_RUN(layout_justify_space_between_packs_edges_tight);
    CF_RUN(layout_justify_space_around_adds_half_gaps_at_edges);
    CF_RUN(layout_justify_space_evenly_adds_full_gaps_at_edges);
    CF_RUN(layout_justify_with_fill_child_leaves_no_extra_space);
    CF_RUN(layout_justify_space_between_single_child_behaves_like_start);
    CF_RUN(layout_overlay_stacks_children_at_parent_origin);
    CF_RUN(layout_overlay_child_margin_offsets_from_origin);
    CF_RUN(layout_overlay_fill_child_expands_to_parent_content);
    CF_RUN(layout_overlay_clamps_child_size);
    return CF_REPORT();
}
