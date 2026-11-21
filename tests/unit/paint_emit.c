#include <stdio.h>

#include "cf/paint.h"
#include "cform_test.h"

#define MAX_NODES 128u
#define MAX_CHILDREN 128u

/* The commands the list asks for before it grows for the first time. */
#define FIRST_CAPACITY 64u

typedef struct fixture {
    cf_style styles[MAX_NODES];
    uint32_t children[MAX_NODES][MAX_CHILDREN];
    uint32_t child_counts[MAX_NODES];
    uint32_t child_start[MAX_NODES + 1u];
    uint32_t child_slots[MAX_NODES * MAX_CHILDREN];
    cf_rect rects[MAX_NODES];
    uint32_t node_count;
} fixture;

static cf_arena scratch;

static void begin(fixture *f) {
    *f = (fixture){0};
}

static uint32_t append(fixture *f, uint32_t parent, cf_style style) {
    const uint32_t slot = f->node_count++;
    f->styles[slot] = style;
    f->children[parent][f->child_counts[parent]++] = slot;
    return slot;
}

static cf_tree tree_of(fixture *f) {
    uint32_t edge = 0u;
    for (uint32_t slot = 0u; slot < f->node_count; ++slot) {
        f->child_start[slot] = edge;
        for (uint32_t i = 0u; i < f->child_counts[slot]; ++i) {
            f->child_slots[edge++] = f->children[slot][i];
        }
    }
    f->child_start[f->node_count] = edge;

    return (cf_tree){.styles = f->styles,
                     .child_start = f->child_start,
                     .child_slots = f->child_slots,
                     .node_count = f->node_count};
}

static uint32_t paint_with(cf_arena *arena, fixture *f, cf_rect viewport, cf_command_list *list) {
    const cf_tree tree = tree_of(f);
    cf_paint(arena, &tree, f->rects, 0u, viewport, list);
    return list->count;
}

static void paint(fixture *f, cf_rect viewport, cf_command_list *list) {
    cf_arena_clear(&scratch);
    (void)paint_with(&scratch, f, viewport, list);
}

static cf_style painted(cf_color color) {
    cf_style style = cf_style_default();
    style.background = (cf_opt_color){.present = true, .value = color};
    return style;
}

static cf_rect box_at(float x, float y, float width, float height) {
    return (cf_rect){.x = x, .y = y, .width = width, .height = height};
}

static int log_count;

static void count_paint_logs(cf_log_channel channel, const char *message, void *user_data) {
    (void)message;
    (void)user_data;

    if (channel == CF_LOG_PAINT) {
        log_count++;
    }
}

static void a_box_with_nothing_stated_paints_nothing(void) {
    fixture f;
    begin(&f);
    f.styles[0] = cf_style_default();
    f.node_count = 1u;
    f.rects[0] = box_at(0.0f, 0.0f, 100.0f, 100.0f);

    cf_command_list list = {0};
    paint(&f, box_at(0.0f, 0.0f, 200.0f, 200.0f), &list);

    CF_CHECK(list.count == 0u, "a box that states no color paints no command");
}

static void a_stated_background_fills_the_border_box(void) {
    fixture f;
    begin(&f);
    f.styles[0] = cf_style_default();
    f.styles[0].background =
        (cf_opt_color){.present = true, .value = (cf_color){120u, 200u, 40u, 255u}};
    f.node_count = 1u;
    f.rects[0] = box_at(5.0f, 7.0f, 100.0f, 50.0f);

    cf_command_list list = {0};
    paint(&f, box_at(0.0f, 0.0f, 200.0f, 200.0f), &list);

    CF_CHECK(list.count == 1u, "one fill for one painted box");
    CF_CHECK(list.commands[0].kind == CF_COMMAND_FILL, "a background emits a fill");
    CF_NEAR(list.commands[0].rect.x, 5.0f);
    CF_NEAR(list.commands[0].rect.y, 7.0f);
    CF_NEAR(list.commands[0].rect.width, 100.0f);
    CF_NEAR(list.commands[0].rect.height, 50.0f);
    CF_CHECK(list.commands[0].color.green == 200u, "the stated color rides the command");
}

static void a_border_needs_both_a_width_and_a_color(void) {
    fixture f;
    begin(&f);
    f.styles[0] = cf_style_default();
    f.styles[0].border = (cf_box){
        .left = CF_PX(2.0f), .top = CF_PX(2.0f), .right = CF_PX(2.0f), .bottom = CF_PX(2.0f)};
    f.node_count = 1u;
    f.rects[0] = box_at(0.0f, 0.0f, 40.0f, 40.0f);

    cf_command_list list = {0};
    paint(&f, box_at(0.0f, 0.0f, 100.0f, 100.0f), &list);
    CF_CHECK(list.count == 0u, "widths without a color paint nothing");

    f.styles[0] = cf_style_default();
    f.styles[0].border_color =
        (cf_opt_color){.present = true, .value = (cf_color){10u, 10u, 10u, 255u}};
    paint(&f, box_at(0.0f, 0.0f, 100.0f, 100.0f), &list);
    CF_CHECK(list.count == 0u, "a color without widths paints nothing");

    f.styles[0].border = (cf_box){
        .left = CF_PX(1.0f), .top = CF_PX(2.0f), .right = CF_PX(3.0f), .bottom = CF_PX(4.0f)};
    paint(&f, box_at(0.0f, 0.0f, 100.0f, 100.0f), &list);

    CF_CHECK(list.count == 1u, "widths with a color stroke the box");
    CF_CHECK(list.commands[0].kind == CF_COMMAND_BORDER, "a border emits a border");
    CF_NEAR(list.commands[0].widths.left, 1.0f);
    CF_NEAR(list.commands[0].widths.top, 2.0f);
    CF_NEAR(list.commands[0].widths.right, 3.0f);
    CF_NEAR(list.commands[0].widths.bottom, 4.0f);
}

static void a_radius_is_clamped_to_half_the_shorter_side(void) {
    fixture f;
    begin(&f);
    f.styles[0] = painted((cf_color){0u, 0u, 0u, 255u});
    f.styles[0].radius = 24.0f;
    f.node_count = 1u;
    f.rects[0] = box_at(0.0f, 0.0f, 40.0f, 20.0f);

    cf_command_list list = {0};
    paint(&f, box_at(0.0f, 0.0f, 100.0f, 100.0f), &list);

    CF_NEAR(list.commands[0].radius, 10.0f);

    f.styles[0].radius = -6.0f;
    list = (cf_command_list){0};
    paint(&f, box_at(0.0f, 0.0f, 100.0f, 100.0f), &list);

    CF_NEAR(list.commands[0].radius, 0.0f);
}

static void a_box_outside_the_viewport_paints_nothing(void) {
    fixture f;
    begin(&f);
    f.styles[0] = painted((cf_color){0u, 0u, 0u, 255u});
    f.node_count = 1u;
    f.rects[0] = box_at(150.0f, 150.0f, 40.0f, 40.0f);

    cf_command_list list = {0};
    paint(&f, box_at(0.0f, 0.0f, 100.0f, 100.0f), &list);
    CF_CHECK(list.count == 0u, "a box past every edge of the viewport paints nothing");

    f.rects[0] = box_at(80.0f, 80.0f, 40.0f, 40.0f);
    paint(&f, box_at(0.0f, 0.0f, 100.0f, 100.0f), &list);

    CF_CHECK(list.count == 1u, "a box that straddles the edge still paints");
    CF_NEAR(list.commands[0].rect.width, 40.0f);
    CF_NEAR(list.commands[0].rect.height, 40.0f);
}

static void children_paint_after_their_parent(void) {
    fixture f;
    begin(&f);
    f.styles[0] = painted((cf_color){1u, 1u, 1u, 255u});
    f.node_count = 1u;
    f.rects[0] = box_at(0.0f, 0.0f, 100.0f, 100.0f);
    (void)append(&f, 0u, painted((cf_color){2u, 2u, 2u, 255u}));
    f.rects[1] = box_at(10.0f, 10.0f, 20.0f, 20.0f);

    cf_command_list list = {0};
    paint(&f, box_at(0.0f, 0.0f, 200.0f, 200.0f), &list);

    CF_CHECK(list.count == 2u, "one command per painted box");
    CF_CHECK(list.commands[0].color.red == 1u, "the parent paints first");
    CF_CHECK(list.commands[1].color.red == 2u, "the child paints over it");
}

static void an_exhausted_scratch_paints_nothing_and_says_so(void) {
    fixture f;
    begin(&f);
    f.styles[0] = painted((cf_color){0u, 0u, 0u, 255u});
    f.node_count = 1u;
    f.rects[0] = box_at(0.0f, 0.0f, 40.0f, 40.0f);

    cf_arena tiny;
    CF_CHECK(cf_arena_init(&tiny, 16u), "the test needs an arena too small for one command");

    log_count = 0;
    cf_log_install(count_paint_logs, NULL);

    cf_command_list list = {0};
    (void)paint_with(&tiny, &f, box_at(0.0f, 0.0f, 100.0f, 100.0f), &list);

    CF_CHECK(list.count == 0u, "an arena too small for one command paints nothing");
    CF_CHECK(log_count == 1, "the exhaustion is reported once on the paint channel");

    cf_arena_free(&tiny);
    cf_log_install(NULL, NULL);
}

static void a_truncated_list_keeps_the_commands_that_fit(void) {
    fixture f;
    begin(&f);
    f.styles[0] = painted((cf_color){0u, 0u, 0u, 255u});
    f.node_count = 1u;
    f.rects[0] = box_at(0.0f, 0.0f, 1.0f, 1.0f);

    for (uint32_t i = 0u; i < 99u; ++i) {
        (void)append(&f, 0u, painted((cf_color){0u, 0u, 0u, 255u}));
        f.rects[i + 1u] = box_at(0.0f, 0.0f, 1.0f, 1.0f);
    }

    /* The first doubling fits exactly; the second ask has no room left. */
    cf_arena tiny;
    CF_CHECK(cf_arena_init(&tiny, (size_t)FIRST_CAPACITY * sizeof(cf_command)),
             "the test needs an arena sized for one doubling");

    log_count = 0;
    cf_log_install(count_paint_logs, NULL);

    cf_command_list list = {0};
    (void)paint_with(&tiny, &f, box_at(0.0f, 0.0f, 100.0f, 100.0f), &list);

    CF_CHECK(list.count == FIRST_CAPACITY, "the commands that fit stay in the list");
    CF_CHECK(log_count == 1, "the exhaustion is reported once, not per box");

    cf_arena_free(&tiny);
    cf_log_install(NULL, NULL);
}

static void a_tree_or_list_that_is_not_there_paints_nothing(void) {
    fixture f;
    begin(&f);
    f.styles[0] = painted((cf_color){0u, 0u, 0u, 255u});
    f.node_count = 1u;
    f.rects[0] = box_at(0.0f, 0.0f, 40.0f, 40.0f);

    const cf_tree tree = tree_of(&f);
    cf_command_list list = {0};

    cf_paint(&scratch, NULL, f.rects, 0u, box_at(0.0f, 0.0f, 100.0f, 100.0f), &list);
    cf_paint(&scratch, &tree, NULL, 0u, box_at(0.0f, 0.0f, 100.0f, 100.0f), &list);
    cf_paint(&scratch, &tree, f.rects, 0u, box_at(0.0f, 0.0f, 100.0f, 100.0f), NULL);
    cf_paint(&scratch, &tree, f.rects, tree.node_count, box_at(0.0f, 0.0f, 100.0f, 100.0f), &list);

    CF_CHECK(list.count == 0u, "an input that is not there paints nothing");
}

int main(void) {
    printf("paint emit\n");
    CF_CHECK(cf_arena_init(&scratch, 65536u), "paint needs scratch");

    CF_RUN(a_box_with_nothing_stated_paints_nothing);
    CF_RUN(a_stated_background_fills_the_border_box);
    CF_RUN(a_border_needs_both_a_width_and_a_color);
    CF_RUN(a_radius_is_clamped_to_half_the_shorter_side);
    CF_RUN(a_box_outside_the_viewport_paints_nothing);
    CF_RUN(children_paint_after_their_parent);
    CF_RUN(an_exhausted_scratch_paints_nothing_and_says_so);
    CF_RUN(a_truncated_list_keeps_the_commands_that_fit);
    CF_RUN(a_tree_or_list_that_is_not_there_paints_nothing);

    cf_arena_free(&scratch);
    return CF_REPORT();
}
