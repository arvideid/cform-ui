#include "cf/build.h"
#include "cf/node.h"
#include "cf/paint.h"
#include "cf/widget.h"

#include "cform_test.h"

/*
 * The switch is a box that answers the same asks a button does and states one
 * thing a button does not: whether it is on. Every case runs real frames through
 * cf_frame_run, so the theme's checked variant, the record a bridge walks and the
 * bit the page holds are all read the way a page reads them.
 */
typedef struct page {
    bool on;
    bool disabled;
    int toggles;
    cf_id last;
    bool saw_ctx;
} page;

static void told(cf_ctx *ctx, cf_id node, void *user_data) {
    page *seen = user_data;

    seen->toggles++;
    seen->last = node;
    seen->saw_ctx = ctx != NULL;
}

/* The page one case runs: a box with room for the square at its start. The child is
 * handed through cf_col because the diff reads the tree after this function returned,
 * and an array on a build's stack is gone by then. */
static cf_node page_of(cf_ctx *ctx, cf_node box) {
    cf_style full = cf_style_default();
    full.width = CF_PX(200.0f);
    full.height = CF_PX(200.0f);

    return cf_col(ctx, full, &box, 1u);
}

static cf_node build_notified(cf_ctx *ctx, void *user_data) {
    page *seen = user_data;
    cf_node box = cf_checkbox(ctx, "notify", &seen->on, told, seen);
    box.name = cf_string_of("Email me");
    box.disabled = seen->disabled;

    return page_of(ctx, box);
}

/* Silent is a legal answer: the page that reads its own bit on the next frame needs
 * nobody to tell it the bit moved, and the one that saves on the spot does. */
static cf_node build_silent(cf_ctx *ctx, void *user_data) {
    page *seen = user_data;

    return page_of(ctx, cf_checkbox(ctx, "notify", &seen->on, NULL, NULL));
}

static bool context_of(cf_ctx *ctx, bool themed) {
    if (!cf_ctx_init(ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return false;
    }

    if (themed) {
        cf_use_theme(ctx, cf_theme_default());
    }

    return true;
}

/* One frame of the loop a page runs, and the stream it handed the device: the
 * commands live in the frame arena, so they are read before the next frame. */
static cf_command_list step(cf_ctx *ctx, cf_build_fn build, page *seen) {
    cf_command_list stream = {0};
    cf_frame_run(ctx, build, seen, (cf_size){.width = 200.0f, .height = 200.0f}, 1.0f / 60.0f,
                 &stream);

    return stream;
}

static uint32_t box_slot(const cf_ctx *ctx) {
    const cf_tree *tree = cf_frame_tree(ctx);
    const uint32_t root = cf_frame_root(ctx);

    if (tree == NULL || root == CF_NO_LINK) {
        return CF_NO_LINK;
    }

    return tree->child_slots[tree->child_start[root]];
}

/* The point a hand would aim at: the middle of the square the frame laid. */
static cf_point box_centre(cf_ctx *ctx, uint32_t slot) {
    const cf_rect *rect = cf_frame_rect(ctx, slot);

    if (rect == NULL) {
        return (cf_point){0.0f, 0.0f};
    }

    return (cf_point){rect->x + rect->width / 2.0f, rect->y + rect->height / 2.0f};
}

static bool tone_is(cf_color color, uint8_t red, uint8_t green, uint8_t blue) {
    return color.red == red && color.green == green && color.blue == blue;
}

/* The square's own paint, picked out of the stream by the extent the theme stated
 * for the family: nothing else on this page is twenty points across. */
static cf_color fill_of(const cf_command_list *stream, uint32_t *found) {
    for (uint32_t i = 0u; i < stream->count; ++i) {
        const cf_command *command = &stream->commands[i];

        if (command->kind == CF_COMMAND_FILL && command->rect.width == 20.0f) {
            *found = *found + 1u;
            return command->color;
        }
    }

    *found = 0u;

    return (cf_color){0, 0, 0, 0};
}

static cf_color edge_of(const cf_command_list *stream, uint32_t *found) {
    for (uint32_t i = 0u; i < stream->count; ++i) {
        const cf_command *command = &stream->commands[i];

        if (command->kind == CF_COMMAND_BORDER && command->rect.width == 20.0f) {
            *found = *found + 1u;
            return command->color;
        }
    }

    *found = 0u;

    return (cf_color){0, 0, 0, 0};
}

/* The switch, as far as a bridge reads it. */
typedef struct spoken {
    bool found;
    cf_access access;
    cf_string name;
    uint32_t states;
    uint32_t actions;
} spoken;

static void hear(const cf_semantic *node, void *user_data) {
    spoken *said = user_data;

    if (said->found || node->access != CF_ACCESS_CHECKBOX) {
        return;
    }

    said->found = true;
    said->access = node->access;
    said->name = node->name;
    said->states = node->states;
    said->actions = node->actions;
}

/*
 * The claim the whole widget rests on: the box paints the difference the theme
 * states between on and off, and nothing else on the page takes the accent.
 */
static void the_bit_the_page_holds_is_the_bit_the_box_paints(void) {
    cf_ctx ctx;
    page seen = {0};

    if (!context_of(&ctx, true)) {
        return;
    }

    uint32_t painted = 0u;
    cf_command_list stream = step(&ctx, build_notified, &seen);
    const cf_color off = fill_of(&stream, &painted);

    CF_CHECK(painted != 0u, "an off switch still paints its box");
    CF_CHECK(tone_is(off, 250, 250, 250), "off rests on the card's own tone");

    uint32_t edged = 0u;
    const cf_color hairline = edge_of(&stream, &edged);

    CF_CHECK(edged != 0u, "and an off switch is edged");
    CF_CHECK(tone_is(hairline, 212, 212, 212), "on the page's hairline");

    seen.on = true;
    painted = 0u;
    stream = step(&ctx, build_notified, &seen);
    const cf_color on = fill_of(&stream, &painted);

    CF_CHECK(painted != 0u, "an on switch paints a box too");
    CF_CHECK(tone_is(on, 23, 23, 23), "and the accent fills it, the tone only a decision wears");

    cf_ctx_free(&ctx);
}

/* A hovered box that is on keeps the fill: the switch is applied under the pointer's
 * mark rather than over it, which is the order cf_style_varied was written for. */
static void a_hand_on_a_switch_that_is_on_leaves_it_on(void) {
    cf_ctx ctx;
    page seen = {.on = true};

    if (!context_of(&ctx, true)) {
        return;
    }

    step(&ctx, build_notified, &seen);
    const uint32_t slot = box_slot(&ctx);

    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_MOVE, .position = box_centre(&ctx, slot)});

    uint32_t painted = 0u;
    cf_command_list stream = step(&ctx, build_notified, &seen);
    const cf_color fill = fill_of(&stream, &painted);
    uint32_t edged = 0u;
    const cf_color edge = edge_of(&stream, &edged);

    CF_CHECK(painted != 0u, "hover leaves the box painted");
    CF_CHECK(tone_is(fill, 23, 23, 23), "and takes no fill away from a switch that is on");
    CF_CHECK(edged != 0u, "while the hover still draws an edge");
    CF_CHECK(tone_is(edge, 163, 163, 163),
             "the edge carries the hover's tone, laid over the checked one");

    cf_ctx_free(&ctx);
}

/* Disabled last: a switched-off box that is on reads disabled, which is what a person
 * needs to see before they need to see the state. */
static void a_switched_off_box_reads_switched_off_even_while_it_is_on(void) {
    cf_ctx ctx;
    page seen = {.on = true, .disabled = true};

    if (!context_of(&ctx, true)) {
        return;
    }

    uint32_t painted = 0u;
    const cf_command_list stream = step(&ctx, build_notified, &seen);

    CF_CHECK(tone_is(fill_of(&stream, &painted), 229, 229, 229),
             "the disabled tone wins over the accent the switch wears");

    cf_ctx_free(&ctx);
}

static void a_release_inside_flips_the_bit_the_page_keeps(void) {
    cf_ctx ctx;
    page seen = {0};

    if (!context_of(&ctx, true)) {
        return;
    }

    step(&ctx, build_notified, &seen);

    const uint32_t slot = box_slot(&ctx);
    const cf_id box = cf_node_id(&ctx, slot);
    const cf_point at = box_centre(&ctx, slot);

    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_DOWN, .position = at});

    CF_CHECK(cf_pressed(&ctx, box), "the hold books the box");
    CF_CHECK(!seen.on && seen.toggles == 0, "and the down alone flips nothing");

    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_UP, .position = at});

    CF_CHECK(seen.on, "the release inside moves the page's own bit");
    CF_CHECK(seen.toggles == 1, "and tells whoever asked, once");
    CF_CHECK(cf_id_equal(seen.last, box), "handing the box as a reference, not a place in arrays");
    CF_CHECK(seen.saw_ctx, "and the context beside it");

    cf_ctx_free(&ctx);
}

static void a_release_that_strayed_out_leaves_the_bit_alone(void) {
    cf_ctx ctx;
    page seen = {0};

    if (!context_of(&ctx, true)) {
        return;
    }

    step(&ctx, build_notified, &seen);

    const uint32_t slot = box_slot(&ctx);
    const cf_id box = cf_node_id(&ctx, slot);
    const cf_point at = box_centre(&ctx, slot);

    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_DOWN, .position = at});
    cf_input(&ctx,
             (cf_event){.type = CF_EVENT_POINTER_MOVE, .position = (cf_point){180.0f, 180.0f}});
    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_UP, .position = (cf_point){180.0f, 180.0f}});

    CF_CHECK(!seen.on, "a drag that left the box decides nothing");
    CF_CHECK(seen.toggles == 0, "and nobody is told");
    CF_CHECK(!cf_pressed(&ctx, box), "while the press still ends with the release");

    cf_ctx_free(&ctx);
}

/* A page that never asked to be told still gets its bit moved: the callback is a
 * door, not the mechanism. */
static void a_switch_nobody_asks_about_still_moves(void) {
    cf_ctx ctx;
    page seen = {0};

    if (!context_of(&ctx, true)) {
        return;
    }

    step(&ctx, build_silent, &seen);

    const uint32_t slot = box_slot(&ctx);
    const cf_point at = box_centre(&ctx, slot);

    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_DOWN, .position = at});
    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_UP, .position = at});

    CF_CHECK(seen.on, "the bit moves with no handler to tell");

    /* The frame after the release is the one that states the switch: a click moves the
     * bit during the event, and a declaration made before it said otherwise. */
    step(&ctx, build_silent, &seen);

    spoken said = {0};
    cf_semantic_visit(&ctx, hear, &said);

    CF_CHECK(said.found, "and the box that moved reaches a bridge");
    CF_CHECK((said.states & (uint32_t)CF_SEMANTIC_CHECKED) != 0u,
             "the frame after says so to whoever is listening");
    CF_CHECK(said.name.length == 0u,
             "a box the build never named is announced by no words rather than by a guess");

    cf_ctx_free(&ctx);
}

static void the_focused_box_answers_the_keys_that_ask(void) {
    cf_ctx ctx;
    page seen = {0};

    if (!context_of(&ctx, true)) {
        return;
    }

    step(&ctx, build_notified, &seen);

    const cf_id box = cf_node_id(&ctx, box_slot(&ctx));

    cf_input(&ctx, (cf_event){.type = CF_EVENT_KEY_DOWN, .key = CF_KEY_ENTER});

    CF_CHECK(!seen.on && seen.toggles == 0, "a box nobody focused flips on no key");

    cf_focus(&ctx, box);
    cf_input(&ctx, (cf_event){.type = CF_EVENT_KEY_DOWN, .key = CF_KEY_ENTER});

    CF_CHECK(seen.on, "Enter on the focused box is the same ask as a release");
    CF_CHECK(seen.toggles == 1, "and it is told once");

    cf_input(&ctx, (cf_event){.type = CF_EVENT_KEY_DOWN, .key = CF_KEY_SPACE});

    CF_CHECK(!seen.on && seen.toggles == 2, "the space bar asks for the same thing");

    cf_input(&ctx, (cf_event){.type = CF_EVENT_KEY_DOWN, .key = CF_KEY_END});

    CF_CHECK(seen.toggles == 2, "a key with no ask in it flips nothing");

    cf_ctx_free(&ctx);
}

static void a_switched_off_box_moves_no_bit_for_anyone(void) {
    cf_ctx ctx;
    page seen = {.disabled = true};

    if (!context_of(&ctx, true)) {
        return;
    }

    step(&ctx, build_notified, &seen);

    const uint32_t slot = box_slot(&ctx);
    const cf_id box = cf_node_id(&ctx, slot);
    const cf_point at = box_centre(&ctx, slot);

    cf_focus(&ctx, box);

    CF_CHECK(!cf_focused(&ctx, box), "a switched-off box takes no focus");

    cf_input(&ctx, (cf_event){.type = CF_EVENT_KEY_DOWN, .key = CF_KEY_ENTER});
    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_DOWN, .position = at});
    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_UP, .position = at});

    CF_CHECK(!seen.on && seen.toggles == 0, "and neither door moves the bit it refuses");

    cf_ctx_free(&ctx);
}

/* The frame a bridge walks sees the switch, the state it is in and the asks it
 * answers - and no value a person could type into it. */
static void a_bridge_hears_the_switch_and_what_it_answers(void) {
    cf_ctx ctx;
    page seen = {.on = true};

    if (!context_of(&ctx, true)) {
        return;
    }

    step(&ctx, build_notified, &seen);

    spoken said = {0};
    cf_semantic_visit(&ctx, hear, &said);

    CF_CHECK(said.found, "the box reaches a bridge that walks the page");
    CF_CHECK(said.access == CF_ACCESS_CHECKBOX, "and is called a switch, not a themed family");
    CF_CHECK(said.name.length == sizeof("Email me") - 1u, "by the words the build gave it");
    CF_CHECK((said.states & (uint32_t)CF_SEMANTIC_CHECKED) != 0u, "and says it is on");
    CF_CHECK((said.states & (uint32_t)CF_SEMANTIC_FOCUSABLE) != 0u,
             "and that the keyboard reaches it");
    CF_CHECK((said.actions & (uint32_t)CF_SEMANTIC_ACTIVATE) != 0u,
             "offering a flip as the ask a person can make");
    CF_CHECK((said.actions & (uint32_t)CF_SEMANTIC_SET_FOCUS) != 0u, "and a focus ask");
    CF_CHECK((said.actions & (uint32_t)CF_SEMANTIC_SET_VALUE) == 0u,
             "but no value to write: a switch takes a flip, not a typed word");

    seen.on = false;
    step(&ctx, build_notified, &seen);
    said = (spoken){0};
    cf_semantic_visit(&ctx, hear, &said);

    CF_CHECK((said.states & (uint32_t)CF_SEMANTIC_CHECKED) == 0u,
             "and a frame later the same box says it is off");

    cf_ctx_free(&ctx);
}

/* A theme paints a control; it is not the control. With no theme at all the box has
 * no colour and no size, and the keyboard still answers for the bit it holds. */
static void a_box_no_theme_painted_is_still_a_switch(void) {
    cf_ctx ctx;
    page seen = {0};

    if (!context_of(&ctx, false)) {
        return;
    }

    step(&ctx, build_notified, &seen);

    const cf_id box = cf_node_id(&ctx, box_slot(&ctx));

    cf_focus(&ctx, box);
    cf_input(&ctx, (cf_event){.type = CF_EVENT_KEY_DOWN, .key = CF_KEY_ENTER});

    CF_CHECK(seen.on, "the ask reaches a box the theme never sized");
    CF_CHECK(seen.toggles == 1, "and it is told once, colour or no colour");

    cf_ctx_free(&ctx);
}

/* Every guard the API states has a caller that reaches it: a declaration made from
 * nothing draws nothing, fires nothing and hands back a box with no switch in it. */
static void a_box_handed_nothing_answers_for_the_missing_page(void) {
    cf_ctx ctx;

    if (!context_of(&ctx, true)) {
        return;
    }

    bool bit = false;

    CF_CHECK(cf_checkbox(NULL, "notify", &bit, NULL, NULL).role == CF_ROLE_NONE,
             "no context answers an empty declaration");
    CF_CHECK(cf_checkbox(&ctx, NULL, &bit, NULL, NULL).role == CF_ROLE_NONE,
             "no name does the same");
    cf_node unhanded = cf_checkbox(&ctx, "notify", NULL, NULL, NULL);

    CF_CHECK(unhanded.role == CF_ROLE_NONE, "and a bit nobody holds leaves no switch role");
    CF_CHECK(unhanded.handlers.activate == NULL, "no ask to answer");
    CF_CHECK(unhanded.handlers.pointer == NULL, "and no press to follow");

    cf_ctx_free(&ctx);
}

int main(void) {
    printf("widget checkbox\n");

    CF_RUN(the_bit_the_page_holds_is_the_bit_the_box_paints);
    CF_RUN(a_hand_on_a_switch_that_is_on_leaves_it_on);
    CF_RUN(a_switched_off_box_reads_switched_off_even_while_it_is_on);
    CF_RUN(a_release_inside_flips_the_bit_the_page_keeps);
    CF_RUN(a_release_that_strayed_out_leaves_the_bit_alone);
    CF_RUN(a_switch_nobody_asks_about_still_moves);
    CF_RUN(the_focused_box_answers_the_keys_that_ask);
    CF_RUN(a_switched_off_box_moves_no_bit_for_anyone);
    CF_RUN(a_bridge_hears_the_switch_and_what_it_answers);
    CF_RUN(a_box_no_theme_painted_is_still_a_switch);
    CF_RUN(a_box_handed_nothing_answers_for_the_missing_page);

    return CF_REPORT();
}
