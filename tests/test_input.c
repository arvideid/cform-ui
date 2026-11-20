#include "cform_test.h"

#include <cform/cform.h>
#include "internal.h"

/* Records click dispatch across tests. Reset in each test body. */
static int click_count;
static UI_Frame *click_frame;
static void *click_user_data;

static void record_click(UI_Frame *frame, void *user_data) {
    click_count++;
    click_frame = frame;
    click_user_data = user_data;
}

static void reset_clicks(void) {
    click_count = 0;
    click_frame = NULL;
    click_user_data = NULL;
}

/* Builds a root (100x100) with a single child (20x20) at offset (10,10)
 * inside a 10px padding on every side. Layout is run so rects are valid. */
static UI_Context *make_simple_tree(UI_Frame **out_root, UI_Frame **out_child) {
    UI_Context *ctx = ui_create();

    UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){
        .direction = UI_HORIZONTAL,
        .padding = UI_ALL(10) });
    UI_Frame *child = ui_frame(ctx, (UI_FrameConfig){
        .width = UI_FIXED(20), .height = UI_FIXED(20) });
    ui_add(root, child);

    ui_layout_frame(root, 0.0f, 0.0f, 100.0f, 100.0f);
    ctx->root = root;

    *out_root = root;
    *out_child = child;
    return ctx;
}

static void pointer_move_sets_hover_on_deepest_frame(void) {
    UI_Frame *root;
    UI_Frame *child;
    UI_Context *ctx = make_simple_tree(&root, &child);

    ui_pointer_move(ctx, 15.0f, 15.0f);

    CF_CHECK(ui_is_hovered(child), "child hovered at (15,15)");
    CF_CHECK(!ui_is_hovered(root), "root not hovered when child claims the point");

    ui_destroy(ctx);
}

static void pointer_move_to_padding_hovers_root_only(void) {
    UI_Frame *root;
    UI_Frame *child;
    UI_Context *ctx = make_simple_tree(&root, &child);

    ui_pointer_move(ctx, 5.0f, 5.0f);

    CF_CHECK(ui_is_hovered(root), "root hovered in its padding area");
    CF_CHECK(!ui_is_hovered(child), "child not hovered outside its rect");

    ui_destroy(ctx);
}

static void pointer_move_outside_root_clears_hover(void) {
    UI_Frame *root;
    UI_Frame *child;
    UI_Context *ctx = make_simple_tree(&root, &child);

    ui_pointer_move(ctx, 15.0f, 15.0f);
    ui_pointer_move(ctx, 500.0f, 500.0f);

    CF_CHECK(!ui_is_hovered(child), "child hover cleared when pointer leaves");
    CF_CHECK(!ui_is_hovered(root), "root hover cleared when pointer leaves");
    CF_CHECK(ctx->hovered == NULL, "context drops hover reference");

    ui_destroy(ctx);
}

static void later_sibling_wins_when_rects_overlap(void) {
    UI_Context *ctx = ui_create();

    UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){
        .direction = UI_HORIZONTAL });
    UI_Frame *first = ui_frame(ctx, (UI_FrameConfig){
        .width = UI_FIXED(50), .height = UI_FIXED(50) });
    UI_Frame *second = ui_frame(ctx, (UI_FrameConfig){
        .width = UI_FIXED(50), .height = UI_FIXED(50) });
    ui_add(root, first);
    ui_add(root, second);

    /* Force the second sibling to overlap the first at absolute (10, 0). */
    ui_layout_frame(root, 0.0f, 0.0f, 100.0f, 100.0f);
    first->x = 10.0f;
    second->x = 10.0f;
    ctx->root = root;

    ui_pointer_move(ctx, 20.0f, 20.0f);

    CF_CHECK(ui_is_hovered(second), "later sibling claims overlapping point");
    CF_CHECK(!ui_is_hovered(first), "earlier sibling loses the hit");

    ui_destroy(ctx);
}

static void click_fires_when_press_and_release_share_frame(void) {
    reset_clicks();

    UI_Frame *root;
    UI_Frame *child;
    UI_Context *ctx = make_simple_tree(&root, &child);

    child->config.on_click = record_click;
    child->config.user_data = (void *)0xABCD;

    ui_pointer_down(ctx, 15.0f, 15.0f);
    CF_CHECK(ui_is_pressed(child), "child enters pressed state on pointer down");

    ui_pointer_up(ctx, 15.0f, 15.0f);

    CF_CHECK(click_count == 1, "on_click fired exactly once");
    CF_CHECK(click_frame == child, "on_click received the child frame");
    CF_CHECK(click_user_data == (void *)0xABCD, "user_data round-trips");
    CF_CHECK(!ui_is_pressed(child), "pressed state cleared after release");

    ui_destroy(ctx);
}

static void click_suppressed_when_release_outside(void) {
    reset_clicks();

    UI_Frame *root;
    UI_Frame *child;
    UI_Context *ctx = make_simple_tree(&root, &child);

    child->config.on_click = record_click;

    ui_pointer_down(ctx, 15.0f, 15.0f);
    ui_pointer_up(ctx, 999.0f, 999.0f);

    CF_CHECK(click_count == 0, "release outside the frame suppresses the click");
    CF_CHECK(!ui_is_pressed(child), "pressed state cleared even on cancelled click");

    ui_destroy(ctx);
}

static void press_on_non_clickable_frame_is_ignored(void) {
    reset_clicks();

    UI_Frame *root;
    UI_Frame *child;
    UI_Context *ctx = make_simple_tree(&root, &child);

    /* child has no on_click handler. */
    ui_pointer_down(ctx, 15.0f, 15.0f);
    ui_pointer_up(ctx, 15.0f, 15.0f);

    CF_CHECK(click_count == 0, "no click on non-clickable frame");
    CF_CHECK(!ui_is_pressed(child), "non-clickable frame never enters pressed state");

    ui_destroy(ctx);
}

static void pointer_leave_cancels_pending_press(void) {
    reset_clicks();

    UI_Frame *root;
    UI_Frame *child;
    UI_Context *ctx = make_simple_tree(&root, &child);

    child->config.on_click = record_click;

    ui_pointer_down(ctx, 15.0f, 15.0f);
    ui_pointer_leave(ctx);
    ui_pointer_up(ctx, 15.0f, 15.0f);

    CF_CHECK(click_count == 0, "leaving the window cancels the pending press");
    CF_CHECK(!ui_is_pressed(child), "leave clears pressed state");

    ui_destroy(ctx);
}

static void hover_transition_clears_previous_frame(void) {
    UI_Frame *root;
    UI_Frame *child;
    UI_Context *ctx = make_simple_tree(&root, &child);

    ui_pointer_move(ctx, 15.0f, 15.0f);
    CF_CHECK(ui_is_hovered(child), "child hovered first");

    ui_pointer_move(ctx, 90.0f, 90.0f);
    CF_CHECK(!ui_is_hovered(child), "child unhovered after transition");
    CF_CHECK(ui_is_hovered(root), "root picked up hover");

    ui_destroy(ctx);
}

static void getters_accept_null_safely(void) {
    CF_CHECK(!ui_is_hovered(NULL), "ui_is_hovered(NULL) returns false");
    CF_CHECK(!ui_is_pressed(NULL), "ui_is_pressed(NULL) returns false");
}

static void hit_test_returns_deepest_frame_containing_point(void) {
    UI_Frame *root;
    UI_Frame *child;
    UI_Context *ctx = make_simple_tree(&root, &child);

    CF_CHECK(ui_hit_test(ctx, 15.0f, 15.0f) == child, "inside child");
    CF_CHECK(ui_hit_test(ctx, 90.0f, 90.0f) == root, "padding lands on root");
    CF_CHECK(ui_hit_test(ctx, 500.0f, 500.0f) == NULL, "outside root misses");

    ui_destroy(ctx);
}

static void hit_test_topmost_sibling_wins(void) {
    UI_Context *ctx = ui_create();

    UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){
        .direction = UI_OVERLAY });
    UI_Frame *bottom = ui_frame(ctx, (UI_FrameConfig){
        .width = UI_FIXED(100), .height = UI_FIXED(100) });
    UI_Frame *top = ui_frame(ctx, (UI_FrameConfig){
        .width = UI_FIXED(50), .height = UI_FIXED(50) });
    ui_add(root, bottom);
    ui_add(root, top);

    ui_layout_frame(root, 0.0f, 0.0f, 100.0f, 100.0f);
    ctx->root = root;

    CF_CHECK(ui_hit_test(ctx, 20.0f, 20.0f) == top,
             "later sibling overlaps earlier one");
    CF_CHECK(ui_hit_test(ctx, 80.0f, 80.0f) == bottom,
             "point outside top falls through to bottom");

    ui_destroy(ctx);
}

static void hit_test_null_before_layout(void) {
    UI_Context *ctx = ui_create();

    CF_CHECK(ui_hit_test(ctx, 0.0f, 0.0f) == NULL,
             "no root registered yet returns NULL");
    CF_CHECK(ui_hit_test(NULL, 0.0f, 0.0f) == NULL,
             "NULL context is a safe no-op");

    ui_destroy(ctx);
}

/* root -> a(clickable), b(plain), c(link). Focusables in pre-order: a, c. */
static UI_Context *make_focus_tree(UI_Frame **out_root,
                                   UI_Frame **out_a,
                                   UI_Frame **out_c) {
    UI_Context *ctx = ui_create();

    UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){ .direction = UI_VERTICAL });
    UI_Frame *a = ui_frame(ctx, (UI_FrameConfig){
        .width = UI_FIXED(10), .height = UI_FIXED(10), .on_click = record_click });
    UI_Frame *b = ui_frame(ctx, (UI_FrameConfig){
        .width = UI_FIXED(10), .height = UI_FIXED(10) });
    UI_Frame *c = ui_frame(ctx, (UI_FrameConfig){
        .width = UI_FIXED(10), .height = UI_FIXED(10), .link_url = "https://example.test" });
    ui_add(root, a);
    ui_add(root, b);
    ui_add(root, c);

    ui_layout_frame(root, 0.0f, 0.0f, 100.0f, 100.0f);
    ctx->root = root;

    *out_root = root;
    *out_a = a;
    *out_c = c;
    return ctx;
}

static void focus_next_walks_focusables_and_wraps(void) {
    UI_Frame *root;
    UI_Frame *a;
    UI_Frame *c;
    UI_Context *ctx = make_focus_tree(&root, &a, &c);

    CF_CHECK(ui_focus_next(ctx, false) == a, "first Tab focuses first clickable");
    CF_CHECK(ui_focus_next(ctx, false) == c, "second Tab skips the plain frame");
    CF_CHECK(ui_focus_next(ctx, false) == a, "third Tab wraps back to the first");
    CF_CHECK(a->focused && !c->focused, "focus flag follows the current frame");

    ui_destroy(ctx);
}

static void shift_tab_wraps_to_last_focusable(void) {
    UI_Frame *root;
    UI_Frame *a;
    UI_Frame *c;
    UI_Context *ctx = make_focus_tree(&root, &a, &c);

    ui_focus_next(ctx, false);
    CF_CHECK(ui_focus_next(ctx, true) == c, "Shift+Tab from first wraps to last");
    CF_CHECK(ui_focus_next(ctx, true) == a, "Shift+Tab steps back to first");

    ui_destroy(ctx);
}

static void activate_focused_runs_click_handler(void) {
    reset_clicks();

    UI_Frame *root;
    UI_Frame *a;
    UI_Frame *c;
    UI_Context *ctx = make_focus_tree(&root, &a, &c);

    ui_focus_next(ctx, false);
    UI_Frame *activated = ui_activate_focused(ctx);

    CF_CHECK(activated == a, "activate returns the focused frame");
    CF_CHECK(click_count == 1, "focused frame's handler ran once");

    ui_clear_focus(ctx);
    CF_CHECK(ui_activate_focused(ctx) == NULL, "activate with no focus is a no-op");

    ui_destroy(ctx);
}

/* root scrolls vertically with a 500px child inside a 100px viewport. */
static UI_Context *make_scroll_tree(UI_Frame **out_root) {
    UI_Context *ctx = ui_create();

    UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){
        .direction = UI_VERTICAL, .scrollable = true });
    UI_Frame *tall = ui_frame(ctx, (UI_FrameConfig){
        .width = UI_FIXED(100), .height = UI_FIXED(500) });
    ui_add(root, tall);

    ui_layout_frame(root, 0.0f, 0.0f, 100.0f, 100.0f);
    ctx->root = root;

    *out_root = root;
    return ctx;
}

static void wheel_notch_scrolls_by_pixel_step(void) {
    UI_Frame *root;
    UI_Context *ctx = make_scroll_tree(&root);

    ui_pointer_wheel(ctx, 50.0f, 50.0f, 0.0f, -1.0f);

    CF_CHECK(root->scroll_y == 60.0f,
             "one notch moves 60px, not the raw ±1 delta");

    ui_destroy(ctx);
}

static void keyboard_scroll_targets_first_scrollable(void) {
    UI_Frame *root;
    UI_Context *ctx = make_scroll_tree(&root);

    ui_scroll_by(ctx, 0.0f, 100.0f);
    CF_CHECK(root->scroll_y == 100.0f, "scroll_by moves the scrollable region");

    ui_scroll_extreme(ctx, true);
    CF_CHECK(root->scroll_y == root->content_height, "End jumps to the content end");

    ui_scroll_extreme(ctx, false);
    CF_CHECK(root->scroll_y == 0.0f, "Home returns to the start");

    ui_destroy(ctx);
}

int main(void) {
    printf("test_input\n");
    CF_RUN(pointer_move_sets_hover_on_deepest_frame);
    CF_RUN(pointer_move_to_padding_hovers_root_only);
    CF_RUN(pointer_move_outside_root_clears_hover);
    CF_RUN(later_sibling_wins_when_rects_overlap);
    CF_RUN(click_fires_when_press_and_release_share_frame);
    CF_RUN(click_suppressed_when_release_outside);
    CF_RUN(press_on_non_clickable_frame_is_ignored);
    CF_RUN(pointer_leave_cancels_pending_press);
    CF_RUN(hover_transition_clears_previous_frame);
    CF_RUN(getters_accept_null_safely);
    CF_RUN(hit_test_returns_deepest_frame_containing_point);
    CF_RUN(hit_test_topmost_sibling_wins);
    CF_RUN(hit_test_null_before_layout);
    CF_RUN(focus_next_walks_focusables_and_wraps);
    CF_RUN(shift_tab_wraps_to_last_focusable);
    CF_RUN(activate_focused_runs_click_handler);
    CF_RUN(wheel_notch_scrolls_by_pixel_step);
    CF_RUN(keyboard_scroll_targets_first_scrollable);
    return CF_REPORT();
}
