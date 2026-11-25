#include <stdio.h>

#include "cf/paint.h"
#include "cform_test.h"

#define MAX_NODES 128u
#define MAX_CHILDREN 128u

/* The commands the list asks for before it grows for the first time. */
#define FIRST_CAPACITY 64u

typedef struct fixture {
    cf_style styles[MAX_NODES];
    cf_measure_fn measures[MAX_NODES];
    void *measure_data[MAX_NODES];
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
                     .measures = f->measures,
                     .measure_data = f->measure_data,
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

/* The font the text cases read: ten points per codepoint, twenty per line,
 * so the lines a frame pins are arithmetic. */
static cf_text_metrics mono_metrics(void *font) {
    (void)font;
    return (cf_text_metrics){.line_height = 20.0f};
}

static float mono_advance(void *font, uint32_t codepoint, uint32_t previous) {
    (void)font;
    (void)codepoint;
    (void)previous;
    return 10.0f;
}

static bool mono_has_glyph(void *font, uint32_t codepoint) {
    (void)font;
    (void)codepoint;
    return true;
}

static const cf_text_ops mono_ops = {
    .metrics = mono_metrics, .advance = mono_advance, .has_glyph = mono_has_glyph};
static const cf_font mono_stack[] = {{.handle = NULL, .ops = &mono_ops}};

static const char label_bytes[] = "aa bb cc";
static const cf_text_content label_text = {
    .stack = {.fonts = mono_stack, .count = 1u},
    .text = {label_bytes, (uint32_t)(sizeof label_bytes - 1u)}};

static const cf_image_content image_content = {.texture = 7u, .width = 32.0f, .height = 16.0f};

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

static void a_text_node_paints_one_command_per_line(void) {
    fixture f;
    begin(&f);
    f.styles[0] = cf_style_default();
    f.styles[0].color = (cf_opt_color){.present = true, .value = (cf_color){10u, 10u, 10u, 255u}};
    f.measures[0] = cf_text_measure;
    f.measure_data[0] = (void *)&label_text;
    f.node_count = 1u;
    f.rects[0] = box_at(10.0f, 10.0f, 60.0f, 60.0f);

    cf_command_list list = {0};
    paint(&f, box_at(0.0f, 0.0f, 200.0f, 200.0f), &list);

    CF_CHECK(list.count == 2u, "one command per laid line");
    CF_CHECK(list.commands[0].kind == CF_COMMAND_TEXT, "the lines are text commands");
    CF_NEAR(list.commands[0].rect.x, 10.0f);
    CF_NEAR(list.commands[0].rect.y, 10.0f);
    CF_NEAR(list.commands[0].rect.width, 50.0f); /* aa bb */
    CF_NEAR(list.commands[0].rect.height, 20.0f);
    CF_NEAR(list.commands[1].rect.y, 30.0f);     /* one line_height below */
    CF_NEAR(list.commands[1].rect.width, 20.0f); /* cc */
    CF_CHECK(list.commands[1].text.length == 2u && list.commands[1].text.chars[0] == 'c',
             "each command carries its own line's bytes");
    CF_CHECK(list.commands[0].color.red == 10u, "the stated tone rides the command");
}

static void text_wraps_inside_the_content_box_the_style_insets(void) {
    fixture f;
    begin(&f);
    f.styles[0] = cf_style_default();
    f.styles[0].color = (cf_opt_color){.present = true, .value = (cf_color){10u, 10u, 10u, 255u}};
    f.styles[0].padding = (cf_box){CF_PX(5.0f), CF_PX(6.0f), CF_PX(5.0f), CF_PX(6.0f)};
    f.styles[0].border = (cf_box){CF_PX(1.0f), CF_PX(1.0f), CF_PX(1.0f), CF_PX(1.0f)};
    f.styles[0].border_color =
        (cf_opt_color){.present = true, .value = (cf_color){120u, 120u, 120u, 255u}};
    f.measures[0] = cf_text_measure;
    f.measure_data[0] = (void *)&label_text;
    f.node_count = 1u;
    f.rects[0] = box_at(0.0f, 0.0f, 60.0f, 60.0f);

    cf_command_list list = {0};
    paint(&f, box_at(0.0f, 0.0f, 200.0f, 200.0f), &list);

    CF_CHECK(list.count == 4u, "the box strokes first, its lines follow");
    CF_CHECK(list.commands[0].kind == CF_COMMAND_BORDER, "the box's own paint comes first");
    CF_CHECK(list.commands[1].kind == CF_COMMAND_TEXT, "the lines paint over it");
    CF_NEAR(list.commands[1].rect.x, 6.0f); /* padding 5 + border 1 */
    CF_NEAR(list.commands[1].rect.y, 7.0f);
    CF_NEAR(list.commands[1].rect.width, 20.0f); /* aa, wrapped at 48 */
    CF_NEAR(list.commands[2].rect.y, 27.0f);
    CF_NEAR(list.commands[3].rect.y, 47.0f);
}

static void a_text_node_without_a_color_paints_nothing(void) {
    fixture f;
    begin(&f);
    f.styles[0] = cf_style_default();
    f.measures[0] = cf_text_measure;
    f.measure_data[0] = (void *)&label_text;
    f.node_count = 1u;
    f.rects[0] = box_at(0.0f, 0.0f, 60.0f, 60.0f);

    cf_command_list list = {0};
    paint(&f, box_at(0.0f, 0.0f, 200.0f, 200.0f), &list);

    CF_CHECK(list.count == 0u, "the stated color is the text's on-switch");
}

static void a_capped_text_node_flags_the_line_the_ellipsis_closes(void) {
    fixture f;
    begin(&f);
    cf_text_content capped = label_text;
    capped.max_lines = (cf_opt_u32){.present = true, .value = 1u};
    capped.ellipsis = true;

    f.styles[0] = cf_style_default();
    f.styles[0].color = (cf_opt_color){.present = true, .value = (cf_color){10u, 10u, 10u, 255u}};
    f.measures[0] = cf_text_measure;
    f.measure_data[0] = (void *)&capped;
    f.node_count = 1u;
    f.rects[0] = box_at(0.0f, 0.0f, 30.0f, 30.0f);

    cf_command_list list = {0};
    paint(&f, box_at(0.0f, 0.0f, 200.0f, 200.0f), &list);

    CF_CHECK(list.count == 1u, "the cap holds one line");
    CF_CHECK(list.commands[0].ellipsis, "the line the walk closed carries the flag");
    CF_NEAR(list.commands[0].rect.width, 30.0f); /* aa beside the ellipsis */
    CF_CHECK(list.commands[0].text.length == 2u, "the bytes end where the ellipsis takes over");
}

static void text_lines_cull_like_their_box_does(void) {
    fixture f;
    begin(&f);
    f.styles[0] = cf_style_default();
    f.styles[0].color = (cf_opt_color){.present = true, .value = (cf_color){10u, 10u, 10u, 255u}};
    f.measures[0] = cf_text_measure;
    f.measure_data[0] = (void *)&label_text;
    f.node_count = 1u;
    f.rects[0] = box_at(0.0f, 150.0f, 60.0f, 60.0f);

    cf_command_list list = {0};
    paint(&f, box_at(0.0f, 0.0f, 200.0f, 100.0f), &list);
    CF_CHECK(list.count == 0u, "a text box past every edge paints nothing");

    f.rects[0] = box_at(0.0f, 90.0f, 60.0f, 60.0f);
    paint(&f, box_at(0.0f, 0.0f, 200.0f, 100.0f), &list);
    CF_CHECK(list.count == 1u, "the line inside the viewport paints, the one below it does not");
    CF_NEAR(list.commands[0].rect.y, 90.0f);
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

/*
 * The fourth scratch-exhaustion class the roadmap names: the lines a text box
 * counted have no room, even though the command array did. The box's own fill
 * stays because the command list was allocated first, the refusal is reported
 * once, and the next frame with a real arena paints the whole box - the walk
 * does not remember the refusal past the arena clear.
 */
static void refused_text_lines_keep_their_box_and_say_so(void) {
    fixture f;
    begin(&f);
    f.styles[0] = cf_style_default();
    f.styles[0].background = (cf_opt_color){.present = true, .value = (cf_color){0u, 0u, 0u, 255u}};
    f.styles[0].color = (cf_opt_color){.present = true, .value = (cf_color){10u, 10u, 10u, 255u}};
    f.measures[0] = cf_text_measure;
    f.measure_data[0] = (void *)&label_text;
    f.node_count = 1u;
    /* "aa bb cc" at ten points a glyph and a box thirty wide lays three lines. */
    f.rects[0] = box_at(0.0f, 0.0f, 30.0f, 60.0f);

    /* The command-array allocation fills the arena exactly, so the walk has
     * room for the box's own fill but none for the lines it counted. */
    cf_arena tiny;
    CF_CHECK(cf_arena_init(&tiny, (size_t)FIRST_CAPACITY * sizeof(cf_command)),
             "the test needs scratch that fits one command array and nothing else");

    log_count = 0;
    cf_log_install(count_paint_logs, NULL);

    cf_command_list refused = {0};
    (void)paint_with(&tiny, &f, box_at(0.0f, 0.0f, 100.0f, 100.0f), &refused);

    CF_CHECK(refused.count == 1u,
             "the box's own fill stays; the lines it cannot lay paint nothing");
    CF_CHECK(log_count == 1, "the refusal is reported once, on the paint channel");

    cf_arena_free(&tiny);
    cf_log_install(NULL, NULL);

    /* Recovery: the same tree against the real arena paints the fill and the
     * three lines; nothing about the last frame's refusal carries over. */
    cf_command_list next = {0};
    paint(&f, box_at(0.0f, 0.0f, 100.0f, 100.0f), &next);
    CF_CHECK(next.count == 4u, "the next valid frame paints the fill and every line");
}

/* A square fill is cut rather than scissored: rewriting its rect costs the
 * picture nothing, because the rect is the whole of what the command draws. */
static void a_box_that_clips_cuts_its_children_square_fills(void) {
    fixture f;
    begin(&f);
    f.styles[0] = painted((cf_color){0u, 0u, 0u, 255u});
    f.styles[0].clips_content = true;
    f.node_count = 1u;
    (void)append(&f, 0, painted((cf_color){0u, 0u, 0u, 255u}));
    f.rects[0] = box_at(0.0f, 0.0f, 100.0f, 100.0f);
    f.rects[1] = box_at(50.0f, 50.0f, 100.0f, 100.0f);

    cf_command_list list = {0};
    paint(&f, box_at(0.0f, 0.0f, 300.0f, 300.0f), &list);

    CF_CHECK(list.count == 2u, "the scroller fills whole and the child fills clipped");
    CF_NEAR(list.commands[0].rect.width, 100.0f);
    CF_NEAR(list.commands[1].rect.x, 50.0f);
    CF_NEAR(list.commands[1].rect.y, 50.0f);
    CF_NEAR(list.commands[1].rect.width, 50.0f);
    CF_NEAR(list.commands[1].rect.height, 50.0f);
}

static void a_child_outside_a_clipping_box_paints_nothing(void) {
    fixture f;
    begin(&f);
    f.styles[0] = painted((cf_color){0u, 0u, 0u, 255u});
    f.styles[0].clips_content = true;
    f.node_count = 1u;
    (void)append(&f, 0, painted((cf_color){0u, 0u, 0u, 255u}));
    f.rects[0] = box_at(0.0f, 0.0f, 100.0f, 100.0f);
    f.rects[1] = box_at(150.0f, 150.0f, 40.0f, 40.0f);

    cf_command_list list = {0};
    paint(&f, box_at(0.0f, 0.0f, 300.0f, 300.0f), &list);

    CF_CHECK(list.count == 1u, "the child past the clip leaves only the scroller's fill");
    CF_CHECK(list.commands[0].kind == CF_COMMAND_FILL, "and that fill is the scroller's own");
}

/* overflow_x and overflow_y are the same rule as the legacy clips_content flag,
 * spelled per axis the way CSS writes it. A box that names HIDDEN on either
 * axis cuts its descendants at its own rect; a box that names VISIBLE on both
 * does not. The paint walk reads the shared helper, so the two spellings agree
 * without either being a special case. */
static void an_overflow_axis_clips_the_way_the_legacy_flag_does(void) {
    fixture f;
    begin(&f);
    f.styles[0] = painted((cf_color){0u, 0u, 0u, 255u});
    f.styles[0].overflow_y = CF_OVERFLOW_HIDDEN;
    f.node_count = 1u;
    (void)append(&f, 0, painted((cf_color){0u, 0u, 0u, 255u}));
    f.rects[0] = box_at(0.0f, 0.0f, 100.0f, 100.0f);
    f.rects[1] = box_at(50.0f, 50.0f, 100.0f, 100.0f);

    cf_command_list list = {0};
    paint(&f, box_at(0.0f, 0.0f, 300.0f, 300.0f), &list);

    CF_CHECK(list.count == 2u, "the axis name cuts, not the flag");
    CF_NEAR(list.commands[1].rect.width, 50.0f);
    CF_NEAR(list.commands[1].rect.height, 50.0f);
}

/* SCROLL means the same clip at the paint walk: the scrollbar is the widget's
 * own concern, so the descendant is cut here and the offset arrives from a
 * different style field. CLIP is HIDDEN for the same reason until a border is
 * drawn into the clip rectangle. */
static void an_overflow_scroll_axis_clips_until_the_widget_draws_a_bar(void) {
    fixture f;
    begin(&f);
    f.styles[0] = painted((cf_color){0u, 0u, 0u, 255u});
    f.styles[0].overflow_x = CF_OVERFLOW_SCROLL;
    f.node_count = 1u;
    (void)append(&f, 0, painted((cf_color){0u, 0u, 0u, 255u}));
    f.rects[0] = box_at(0.0f, 0.0f, 100.0f, 100.0f);
    f.rects[1] = box_at(150.0f, 150.0f, 40.0f, 40.0f);

    cf_command_list list = {0};
    paint(&f, box_at(0.0f, 0.0f, 300.0f, 300.0f), &list);

    CF_CHECK(list.count == 1u, "the scroller fill survives, the child past it does not");
}

/* A box that clips nothing lets its descendants overflow past its own box.
 * An offscreen non-clipping parent with a visible child must still paint the
 * child - the hit walk already descends into it; only a clipping box culls its
 * subtree when its own box cannot reach the clip. */
static void a_child_overflowing_a_non_clipping_parent_paints(void) {
    fixture f;
    begin(&f);
    /* Root does not clip: its 50-wide box sits entirely off the viewport. */
    f.styles[0] = cf_style_default();
    f.node_count = 1u;
    (void)append(&f, 0, painted((cf_color){0u, 0u, 0u, 255u}));
    f.rects[0] = box_at(-100.0f, 0.0f, 50.0f, 50.0f);
    /* Child is fully inside the viewport [0,0,300,300]. */
    f.rects[1] = box_at(50.0f, 50.0f, 50.0f, 50.0f);

    cf_command_list list = {0};
    paint(&f, box_at(0.0f, 0.0f, 300.0f, 300.0f), &list);

    CF_CHECK(list.count == 1u, "a child overflowing an offscreen parent paints");
    CF_CHECK(list.commands[0].kind == CF_COMMAND_FILL, "and the command is the child's fill");
}

/* The complement: when a parent does clip its content and its box lies wholly
 * outside the clip, the empty region culls the whole subtree. */
static void a_clipping_box_wholly_offscreen_culls_its_subtree(void) {
    fixture f;
    begin(&f);
    f.styles[0] = cf_style_default();
    f.styles[0].clips_content = true;
    f.node_count = 1u;
    (void)append(&f, 0, painted((cf_color){0u, 0u, 0u, 255u}));
    f.rects[0] = box_at(400.0f, 0.0f, 100.0f, 100.0f);
    /* Child at [100,50] is inside the viewport, but the clipping parent at
     * [400,...] does not reach it, so nothing of the subtree draws. */
    f.rects[1] = box_at(100.0f, 50.0f, 50.0f, 50.0f);

    cf_command_list list = {0};
    paint(&f, box_at(0.0f, 0.0f, 300.0f, 300.0f), &list);

    CF_CHECK(list.count == 0u, "a clipping box outside the clip culls all descendants");
}

static void a_clip_narrows_as_it_descends(void) {
    fixture f;
    begin(&f);
    f.styles[0] = cf_style_default();
    f.styles[0].clips_content = true;
    f.node_count = 1u;
    f.rects[0] = box_at(0.0f, 0.0f, 100.0f, 100.0f);
    const uint32_t middle = append(&f, 0, cf_style_default());
    f.styles[middle].clips_content = true;
    f.rects[middle] = box_at(50.0f, 0.0f, 100.0f, 100.0f);
    const uint32_t leaf = append(&f, middle, painted((cf_color){0u, 0u, 0u, 255u}));
    f.rects[leaf] = box_at(60.0f, 10.0f, 200.0f, 200.0f);

    cf_command_list list = {0};
    paint(&f, box_at(0.0f, 0.0f, 300.0f, 300.0f), &list);

    CF_CHECK(list.count == 1u, "the leaf fills against both clips");
    CF_NEAR(list.commands[0].rect.x, 60.0f);
    CF_NEAR(list.commands[0].rect.y, 10.0f);
    CF_NEAR(list.commands[0].rect.width, 40.0f);
    CF_NEAR(list.commands[0].rect.height, 90.0f);
}

/* A stroke cannot be cut without moving the corners it rounds, so a border
 * keeps its own rect and names the region the drawing must stop at instead. */
static void a_border_at_a_clip_names_the_clip_it_is_cut_by(void) {
    fixture f;
    begin(&f);
    f.styles[0] = cf_style_default();
    f.styles[0].clips_content = true;
    f.node_count = 1u;

    cf_style edge = cf_style_default();
    edge.border_color = (cf_opt_color){.present = true, .value = CF_COLOR(0, 0, 0)};
    edge.border = (cf_box){CF_PX(1.0f), CF_PX(1.0f), CF_PX(1.0f), CF_PX(1.0f)};

    (void)append(&f, 0, edge);
    f.rects[0] = box_at(0.0f, 0.0f, 100.0f, 100.0f);
    f.rects[1] = box_at(0.0f, 60.0f, 100.0f, 80.0f);

    cf_command_list list = {0};
    paint(&f, box_at(0.0f, 0.0f, 300.0f, 300.0f), &list);

    CF_CHECK(list.count == 1u, "the stroked child paints its border");
    CF_CHECK(list.commands[0].kind == CF_COMMAND_BORDER, "and the command is a border");
    CF_NEAR(list.commands[0].rect.height, 80.0f);
    CF_NEAR(list.commands[0].clip.y, 0.0f);
    CF_NEAR(list.commands[0].clip.height, 100.0f);
}

/* A corner is arithmetic on the rect, so a rounded fill cut to the clip rounds
 * the cut box and stops matching the border stroked at the true one. It keeps
 * its rect and names the clip the way a border does. */
static void a_rounded_fill_at_a_clip_keeps_the_box_it_rounds(void) {
    fixture f;
    begin(&f);
    f.styles[0] = cf_style_default();
    f.styles[0].clips_content = true;
    f.node_count = 1u;

    cf_style round = painted((cf_color){0u, 0u, 0u, 255u});
    round.radius = 10.0f;
    (void)append(&f, 0, round);
    f.rects[0] = box_at(0.0f, 0.0f, 100.0f, 100.0f);
    f.rects[1] = box_at(60.0f, 10.0f, 80.0f, 80.0f);

    cf_command_list list = {0};
    paint(&f, box_at(0.0f, 0.0f, 300.0f, 300.0f), &list);

    CF_CHECK(list.count == 1u, "the rounded child paints its fill");
    CF_NEAR(list.commands[0].radius, 10.0f);
    CF_NEAR(list.commands[0].rect.width, 80.0f);
    CF_NEAR(list.commands[0].clip.width, 100.0f);
    CF_NEAR(list.commands[0].clip.height, 100.0f);
}

/* The clip a page outside every clipping box carries is no clip at all: the
 * screen bounds itself, and a stream that said otherwise would make every
 * backend pay a state change for the whole frame. */
static void a_page_with_no_clipping_box_names_no_clip(void) {
    fixture f;
    begin(&f);
    f.styles[0] = cf_style_default();
    f.node_count = 1u;

    cf_style edge = cf_style_default();
    edge.border_color = (cf_opt_color){.present = true, .value = CF_COLOR(0, 0, 0)};
    edge.border = (cf_box){CF_PX(1.0f), CF_PX(1.0f), CF_PX(1.0f), CF_PX(1.0f)};

    (void)append(&f, 0, edge);
    f.rects[0] = box_at(0.0f, 0.0f, 100.0f, 100.0f);
    f.rects[1] = box_at(0.0f, 60.0f, 100.0f, 80.0f);

    cf_command_list list = {0};
    paint(&f, box_at(0.0f, 0.0f, 100.0f, 100.0f), &list);

    CF_CHECK(list.count == 1u, "the border paints");
    CF_NEAR(list.commands[0].clip.width, 0.0f);
    CF_NEAR(list.commands[0].clip.height, 0.0f);
}

static void text_culls_per_line_inside_a_clip(void) {
    fixture f;
    begin(&f);
    f.styles[0] = cf_style_default();
    f.styles[0].clips_content = true;
    f.node_count = 1u;
    const uint32_t label = append(&f, 0, cf_style_default());
    f.styles[label].color = (cf_opt_color){.present = true, .value = CF_COLOR(0, 0, 0)};
    f.measures[label] = cf_text_measure;
    f.measure_data[label] = (void *)&label_text;
    f.rects[0] = box_at(0.0f, 0.0f, 100.0f, 50.0f);
    f.rects[label] = box_at(0.0f, 100.0f, 100.0f, 20.0f);

    cf_command_list list = {0};
    paint(&f, box_at(0.0f, 0.0f, 300.0f, 300.0f), &list);
    CF_CHECK(list.count == 0u, "the line below the clip is culled with its box");

    f.rects[label] = box_at(0.0f, 10.0f, 100.0f, 20.0f);
    paint(&f, box_at(0.0f, 0.0f, 300.0f, 300.0f), &list);

    CF_CHECK(list.count == 1u, "the line inside it paints");
    CF_CHECK(list.commands[0].kind == CF_COMMAND_TEXT, "and the command is text");
}

static void an_image_paints_its_texture_into_its_box(void) {
    fixture f;
    begin(&f);
    f.styles[0] = cf_style_default();
    f.node_count = 1u;
    f.measures[0] = cf_image_measure;
    f.measure_data[0] = (void *)&image_content;
    f.rects[0] = box_at(10.0f, 20.0f, 32.0f, 16.0f);

    cf_command_list list = {0};
    paint(&f, box_at(0.0f, 0.0f, 100.0f, 100.0f), &list);

    CF_CHECK(list.count == 1u, "an image paints one command");
    CF_CHECK(list.commands[0].kind == CF_COMMAND_IMAGE, "and it is an image");
    CF_CHECK(list.commands[0].texture == 7u, "the texture the content names rides the command");
    CF_NEAR(list.commands[0].rect.x, 10.0f);
    CF_NEAR(list.commands[0].rect.y, 20.0f);
    CF_NEAR(list.commands[0].rect.width, 32.0f);
    CF_NEAR(list.commands[0].rect.height, 16.0f);
}

/* A device maps the whole of a texture into the rect it is handed, so a cut
 * rect squashes what is left into the window instead of showing the part that
 * fits it. The image keeps its box and names the clip that stops it. */
static void an_image_at_a_clip_names_the_clip_it_is_cut_by(void) {
    fixture f;
    begin(&f);
    f.styles[0] = cf_style_default();
    f.styles[0].clips_content = true;
    f.node_count = 1u;
    const uint32_t image = append(&f, 0, cf_style_default());
    f.measures[image] = cf_image_measure;
    f.measure_data[image] = (void *)&image_content;
    f.rects[0] = box_at(0.0f, 0.0f, 100.0f, 100.0f);
    f.rects[image] = box_at(80.0f, 10.0f, 32.0f, 16.0f);

    cf_command_list list = {0};
    paint(&f, box_at(0.0f, 0.0f, 300.0f, 300.0f), &list);

    CF_CHECK(list.count == 1u, "the clipped image is the only paint");
    CF_CHECK(list.commands[0].kind == CF_COMMAND_IMAGE, "and it is an image");
    CF_NEAR(list.commands[0].rect.x, 80.0f);
    CF_NEAR(list.commands[0].rect.width, 32.0f);
    CF_NEAR(list.commands[0].clip.width, 100.0f);
}

/*
 * The two marks an editor states. "aa bb cc" at ten points a glyph and a box
 * thirty wide lays three lines - "aa", "bb", "cc" over bytes 0..2, 3..5 and
 * 6..8 - so the pair 3 and 5 is the whole of the middle one.
 */
static void a_stated_caret_and_selection_paint_with_the_lines(void) {
    fixture f;
    begin(&f);
    cf_text_content edited = label_text;
    edited.caret = (cf_opt_u32){.present = true, .value = 5u};
    edited.mark = (cf_opt_u32){.present = true, .value = 3u};

    f.styles[0] = cf_style_default();
    f.styles[0].color = (cf_opt_color){.present = true, .value = (cf_color){10u, 10u, 10u, 255u}};
    f.styles[0].caret = (cf_opt_color){.present = true, .value = (cf_color){23u, 23u, 23u, 255u}};
    f.styles[0].selection =
        (cf_opt_color){.present = true, .value = (cf_color){191u, 219u, 254u, 255u}};
    f.measures[0] = cf_text_measure;
    f.measure_data[0] = (void *)&edited;
    f.node_count = 1u;
    f.rects[0] = box_at(0.0f, 0.0f, 30.0f, 60.0f);

    cf_command_list list = {0};
    paint(&f, box_at(0.0f, 0.0f, 200.0f, 200.0f), &list);

    CF_CHECK(list.count == 5u, "a band, three lines and a caret");
    CF_CHECK(list.commands[0].kind == CF_COMMAND_FILL, "the band is laid before the glyphs");
    CF_NEAR(list.commands[0].rect.x, 0.0f);
    CF_NEAR(list.commands[0].rect.y, 20.0f);
    CF_NEAR(list.commands[0].rect.width, 20.0f);
    CF_NEAR(list.commands[0].rect.height, 20.0f);
    CF_CHECK(list.commands[1].kind == CF_COMMAND_TEXT, "and the lines ride over it");
    CF_CHECK(list.commands[4].kind == CF_COMMAND_FILL, "the caret closes the box's paint");
    CF_NEAR(list.commands[4].rect.x, 20.0f);
    CF_NEAR(list.commands[4].rect.y, 20.0f);
    CF_NEAR(list.commands[4].rect.width, 1.0f);
    CF_NEAR(list.commands[4].rect.height, 20.0f);

    /* A mark the style hands out no tone for is a mark nothing paints, which is
     * how a label that carries an offset stays a label. */
    f.styles[0].caret.present = false;
    f.styles[0].selection.present = false;
    list.count = 0u; /* the arena rewind hands the array back; clearing the count
                      * is the caller's */
    paint(&f, box_at(0.0f, 0.0f, 200.0f, 200.0f), &list);
    CF_CHECK(list.count == 3u, "without the tones the bytes stand alone");
}

/* A range that crosses a line needs no case for the lines it only passes
 * through: each end clamps into the line it is read against, so a middle line
 * answers both its own edges. */
static void a_range_across_lines_bands_every_one_it_touches(void) {
    fixture f;
    begin(&f);
    cf_text_content edited = label_text;
    edited.caret = (cf_opt_u32){.present = true, .value = 7u};
    edited.mark = (cf_opt_u32){.present = true, .value = 1u};

    f.styles[0] = cf_style_default();
    f.styles[0].color = (cf_opt_color){.present = true, .value = (cf_color){10u, 10u, 10u, 255u}};
    f.styles[0].selection =
        (cf_opt_color){.present = true, .value = (cf_color){191u, 219u, 254u, 255u}};
    f.measures[0] = cf_text_measure;
    f.measure_data[0] = (void *)&edited;
    f.node_count = 1u;
    f.rects[0] = box_at(0.0f, 0.0f, 30.0f, 60.0f);

    cf_command_list list = {0};
    paint(&f, box_at(0.0f, 0.0f, 200.0f, 200.0f), &list);

    CF_CHECK(list.count == 6u, "three bands over three lines, and no caret without its tone");
    CF_NEAR(list.commands[0].rect.x, 10.0f);
    CF_NEAR(list.commands[0].rect.y, 0.0f);
    CF_NEAR(list.commands[0].rect.width, 10.0f);
    CF_NEAR(list.commands[2].rect.x, 0.0f);
    CF_NEAR(list.commands[2].rect.y, 40.0f);
    CF_NEAR(list.commands[2].rect.width, 10.0f);
}

/* A value wider than the box that shows it: the walk keeps the line's true rect
 * and names the clip that cuts it, and the box slides along the line to hold the
 * caret the content states. */
static void a_caret_past_the_edge_slides_the_line_and_is_cut_at_it(void) {
    fixture f;
    begin(&f);
    cf_text_content long_value = label_text;
    long_value.nowrap = true;
    long_value.caret = (cf_opt_u32){.present = true, .value = 8u};

    f.styles[0] = cf_style_default();
    f.styles[0].color = (cf_opt_color){.present = true, .value = (cf_color){10u, 10u, 10u, 255u}};
    f.styles[0].caret = (cf_opt_color){.present = true, .value = (cf_color){23u, 23u, 23u, 255u}};
    f.styles[0].clips_content = true;
    f.measures[0] = cf_text_measure;
    f.measure_data[0] = (void *)&long_value;
    f.node_count = 1u;
    f.rects[0] = box_at(0.0f, 0.0f, 50.0f, 20.0f);

    cf_command_list list = {0};
    paint(&f, box_at(0.0f, 0.0f, 200.0f, 200.0f), &list);

    CF_CHECK(list.count == 2u, "one line that refuses to wrap, and the caret in it");
    CF_NEAR(list.commands[0].rect.x, -30.0f); /* the eight bytes slid to the caret */
    CF_NEAR(list.commands[0].clip.x, 0.0f);
    CF_NEAR(list.commands[0].clip.width, 50.0f);
    CF_NEAR(list.commands[0].clip.height, 20.0f);
    /* The offset the text ends at stands on the edge itself, where a cut would
     * erase it: the column is pulled inside instead. */
    CF_NEAR(list.commands[1].rect.x, 49.0f);
    CF_NEAR(list.commands[1].rect.width, 1.0f);
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
    CF_RUN(a_text_node_paints_one_command_per_line);
    CF_RUN(text_wraps_inside_the_content_box_the_style_insets);
    CF_RUN(a_text_node_without_a_color_paints_nothing);
    CF_RUN(a_capped_text_node_flags_the_line_the_ellipsis_closes);
    CF_RUN(text_lines_cull_like_their_box_does);
    CF_RUN(a_box_that_clips_cuts_its_children_square_fills);
    CF_RUN(a_child_outside_a_clipping_box_paints_nothing);
    CF_RUN(an_overflow_axis_clips_the_way_the_legacy_flag_does);
    CF_RUN(an_overflow_scroll_axis_clips_until_the_widget_draws_a_bar);
    CF_RUN(a_child_overflowing_a_non_clipping_parent_paints);
    CF_RUN(a_clipping_box_wholly_offscreen_culls_its_subtree);
    CF_RUN(a_clip_narrows_as_it_descends);
    CF_RUN(a_border_at_a_clip_names_the_clip_it_is_cut_by);
    CF_RUN(a_rounded_fill_at_a_clip_keeps_the_box_it_rounds);
    CF_RUN(a_page_with_no_clipping_box_names_no_clip);
    CF_RUN(text_culls_per_line_inside_a_clip);
    CF_RUN(an_exhausted_scratch_paints_nothing_and_says_so);
    CF_RUN(a_truncated_list_keeps_the_commands_that_fit);
    CF_RUN(refused_text_lines_keep_their_box_and_say_so);
    CF_RUN(a_tree_or_list_that_is_not_there_paints_nothing);
    CF_RUN(an_image_paints_its_texture_into_its_box);
    CF_RUN(an_image_at_a_clip_names_the_clip_it_is_cut_by);
    CF_RUN(a_stated_caret_and_selection_paint_with_the_lines);
    CF_RUN(a_range_across_lines_bands_every_one_it_touches);
    CF_RUN(a_caret_past_the_edge_slides_the_line_and_is_cut_at_it);

    cf_arena_free(&scratch);
    return CF_REPORT();
}
