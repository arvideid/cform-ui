#include "cf/node.h"
#include "cform_test.h"

/*
 * Handlers record what they saw in a stack struct handed over as user data,
 * so dispatch runs the same path a widget drives: target, bubble, borrowed
 * data. Every case builds, diffs and lays out one context of its own.
 */
typedef struct recorder {
    int moves;
    int downs;
    int ups;
    int scrolls;
    int keys;
    bool stop;
    bool capture;
    uint32_t last_target;
} recorder;

static void on_pointer(cf_event *event, void *user_data) {
    recorder *seen = user_data;

    switch (event->type) {
    case CF_EVENT_POINTER_MOVE:
        seen->moves++;
        break;
    case CF_EVENT_POINTER_DOWN:
        seen->downs++;
        break;
    case CF_EVENT_POINTER_UP:
        seen->ups++;
        break;
    case CF_EVENT_SCROLL:
        seen->scrolls++;
        break;
    case CF_EVENT_KEY_DOWN:
    case CF_EVENT_KEY_UP:
    case CF_EVENT_TEXT:
    case CF_EVENT_QUIT:
        break;
    }

    seen->last_target = event->target;

    if (seen->stop) {
        cf_stop(event);
    }

    if (seen->capture) {
        cf_capture(event);
    }
}

static void on_key(cf_event *event, void *user_data) {
    recorder *seen = user_data;

    seen->keys++;
    seen->last_target = event->target;

    if (seen->stop) {
        cf_stop(event);
    }
}

static cf_node box(float width, float height, cf_event_fn pointer, void *pointer_data) {
    cf_node node = {.style = cf_style_default()};
    node.style.width = CF_PX(width);
    node.style.height = CF_PX(height);
    node.handlers.pointer = pointer;
    node.handlers.pointer_data = pointer_data;
    return node;
}

static bool context_of(cf_ctx *ctx) {
    if (!cf_ctx_init(ctx, 64u * 1024u, 4u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return false;
    }

    return true;
}

static void lay_out(cf_ctx *ctx, float width, float height) {
    cf_frame_layout(ctx, (cf_available){.width = CF_PX(width), .height = CF_PX(height)},
                    (cf_point){.x = 0.0f, .y = 0.0f});
}

static uint32_t child_of(const cf_ctx *ctx, uint32_t slot, uint32_t index) {
    const cf_tree *tree = cf_frame_tree(ctx);

    if (tree == NULL || slot == CF_NO_LINK) {
        return CF_NO_LINK;
    }

    return tree->child_slots[tree->child_start[slot] + index];
}

static uint32_t child_slot(const cf_ctx *ctx, uint32_t index) {
    return child_of(ctx, ctx->root_slot, index);
}

static void a_move_hovers_the_topmost_child_at_the_point(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    recorder first = {0};
    recorder second = {0};
    recorder root_log = {0};
    cf_node children[] = {
        box(50.0f, 50.0f, on_pointer, &first),
        box(50.0f, 50.0f, on_pointer, &second),
    };
    cf_node root = box(200.0f, 200.0f, on_pointer, &root_log);
    root.children = children;
    root.child_count = 2u;

    cf_frame_begin(&ctx);
    cf_diff(&ctx, &root);
    lay_out(&ctx, 200.0f, 200.0f);

    const uint32_t left = child_slot(&ctx, 0u);
    const uint32_t right = child_slot(&ctx, 1u);
    const cf_point at = {.x = 60.0f, .y = 10.0f};

    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_MOVE, .position = at});

    CF_CHECK(cf_hit_test(&ctx, at) == right, "the topmost node answers the point");
    CF_CHECK(cf_hovered(&ctx, right), "the move hovers the child under the pointer");
    CF_CHECK(!cf_hovered(&ctx, left), "the child beside it stays out");
    CF_CHECK(!cf_hovered(&ctx, ctx.root_slot), "the container behind it stays out");
    CF_CHECK(second.moves == 1 && first.moves == 0 && root_log.moves == 1,
             "the move runs the hovered node's handler and bubbles");
    CF_CHECK(second.last_target == right, "the event names the node whose handler ran");

    cf_ctx_free(&ctx);
}

static void the_cursor_names_the_pointer_only_over_a_handler(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    recorder handled = {0};
    cf_node children[] = {
        box(50.0f, 50.0f, NULL, NULL),
        box(50.0f, 50.0f, on_pointer, &handled),
    };
    cf_node root = box(200.0f, 200.0f, NULL, NULL);
    root.children = children;
    root.child_count = 2u;

    cf_frame_begin(&ctx);
    cf_diff(&ctx, &root);
    lay_out(&ctx, 200.0f, 200.0f);

    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_MOVE, .position = {.x = 10.0f, .y = 10.0f}});

    CF_CHECK(!cf_hovered(&ctx, child_slot(&ctx, 0u)), "a box that acts on nothing books no hover");
    CF_CHECK(cf_frame_cursor(&ctx) == CF_CURSOR_DEFAULT, "no handler, no pointer cursor");

    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_MOVE, .position = {.x = 60.0f, .y = 10.0f}});

    CF_CHECK(cf_frame_cursor(&ctx) == CF_CURSOR_POINTER, "the handler asks for the pointer");

    cf_ctx_free(&ctx);
}

static void a_press_marks_and_focuses_the_node_it_hit(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    recorder child_log = {0};
    recorder root_log = {0};
    cf_node children[] = {box(50.0f, 50.0f, on_pointer, &child_log)};
    cf_node root = box(200.0f, 200.0f, on_pointer, &root_log);
    root.children = children;
    root.child_count = 1u;

    cf_frame_begin(&ctx);
    cf_diff(&ctx, &root);
    lay_out(&ctx, 200.0f, 200.0f);

    const uint32_t child = child_slot(&ctx, 0u);

    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_DOWN, .position = {.x = 10.0f, .y = 10.0f}});

    CF_CHECK(cf_pressed(&ctx, child), "the press lands on the child");
    CF_CHECK(cf_focused(&ctx, child), "focus follows the press");
    CF_CHECK(!cf_pressed(&ctx, ctx.root_slot), "the container is not pressed");
    CF_CHECK(child_log.downs == 1 && root_log.downs == 1,
             "the press reaches the child and bubbles to the container");

    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_UP, .position = {.x = 10.0f, .y = 10.0f}});

    CF_CHECK(!cf_pressed(&ctx, child), "the release clears the press");
    CF_CHECK(cf_focused(&ctx, child), "focus outlives the press");
    CF_CHECK(child_log.ups == 1, "the release reaches the pressed node");

    cf_ctx_free(&ctx);
}

static void a_release_off_the_pressed_node_still_reaches_it(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    recorder first = {0};
    recorder second = {0};
    recorder root_log = {0};
    cf_node children[] = {
        box(50.0f, 50.0f, on_pointer, &first),
        box(50.0f, 50.0f, on_pointer, &second),
    };
    cf_node root = box(200.0f, 200.0f, on_pointer, &root_log);
    root.children = children;
    root.child_count = 2u;

    cf_frame_begin(&ctx);
    cf_diff(&ctx, &root);
    lay_out(&ctx, 200.0f, 200.0f);

    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_DOWN, .position = {.x = 10.0f, .y = 10.0f}});
    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_MOVE, .position = {.x = 60.0f, .y = 10.0f}});
    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_UP, .position = {.x = 60.0f, .y = 10.0f}});

    CF_CHECK(first.ups == 1, "the release reaches the node the press landed on");
    CF_CHECK(second.ups == 0, "the node under the release does not take it");
    CF_CHECK(cf_hovered(&ctx, child_slot(&ctx, 1u)), "the hover follows the pointer again");

    cf_ctx_free(&ctx);
}

static void a_stopping_handler_keeps_the_event_off_the_ancestors(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    recorder child_log = {0};
    recorder root_log = {0};
    cf_node children[] = {box(50.0f, 50.0f, on_pointer, &child_log)};
    cf_node root = box(200.0f, 200.0f, on_pointer, &root_log);
    root.children = children;
    root.child_count = 1u;

    cf_frame_begin(&ctx);
    cf_diff(&ctx, &root);
    lay_out(&ctx, 200.0f, 200.0f);

    child_log.stop = true;
    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_MOVE, .position = {.x = 10.0f, .y = 10.0f}});

    CF_CHECK(child_log.moves == 1 && root_log.moves == 0,
             "the stopped event ends at the node that stopped it");

    child_log.stop = false;
    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_MOVE, .position = {.x = 20.0f, .y = 10.0f}});

    CF_CHECK(child_log.moves == 2 && root_log.moves == 1,
             "an unstopped event climbs to the container");

    cf_ctx_free(&ctx);
}

/* A plain box under the pointer reads through to whoever acts on it: the
 * climb books hover and press on the nearest handler or themed family, so
 * the widget a build composed owns the state its children sit in. */
static void a_press_on_a_plain_child_lands_on_the_button_around_it(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    recorder button_log = {0};
    cf_node label = box(40.0f, 40.0f, NULL, NULL);
    const cf_node children[] = {label};
    cf_node button = box(80.0f, 80.0f, on_pointer, &button_log);
    button.children = children;
    button.child_count = 1u;
    const cf_node roots[] = {button};
    cf_node root = box(200.0f, 200.0f, NULL, NULL);
    root.children = roots;
    root.child_count = 1u;

    cf_frame_begin(&ctx);
    cf_diff(&ctx, &root);
    lay_out(&ctx, 200.0f, 200.0f);

    const uint32_t button_slot = child_slot(&ctx, 0u);
    const uint32_t label_slot = child_of(&ctx, button_slot, 0u);

    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_DOWN, .position = {.x = 10.0f, .y = 10.0f}});

    CF_CHECK(cf_pressed(&ctx, button_slot), "the press lands on the button the label sits in");
    CF_CHECK(!cf_pressed(&ctx, label_slot), "the label itself holds none of it");
    CF_CHECK(button_log.downs == 1 && button_log.last_target == button_slot,
             "the button's handler still sees the press the label took");

    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_UP, .position = {.x = 10.0f, .y = 10.0f}});

    CF_CHECK(!cf_pressed(&ctx, button_slot), "the release clears the press");
    CF_CHECK(button_log.ups == 1, "the release reaches the button too");

    cf_ctx_free(&ctx);
}

static void a_disabled_button_takes_no_press_and_silences_its_handlers(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    recorder button_log = {0};
    cf_node label = box(40.0f, 40.0f, NULL, NULL);
    const cf_node children[] = {label};
    cf_node button = box(80.0f, 80.0f, on_pointer, &button_log);
    button.disabled = true;
    button.children = children;
    button.child_count = 1u;
    const cf_node roots[] = {button};
    cf_node root = box(200.0f, 200.0f, NULL, NULL);
    root.children = roots;
    root.child_count = 1u;

    cf_frame_begin(&ctx);
    cf_diff(&ctx, &root);
    lay_out(&ctx, 200.0f, 200.0f);

    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_DOWN, .position = {.x = 10.0f, .y = 10.0f}});

    CF_CHECK(button_log.downs == 0, "a switched-off node runs no handler");
    CF_CHECK(!cf_pressed(&ctx, child_slot(&ctx, 0u)), "the press books nothing on it");

    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_MOVE, .position = {.x = 10.0f, .y = 10.0f}});

    CF_CHECK(!cf_hovered(&ctx, child_slot(&ctx, 0u)), "the hover skips it as well");
    CF_CHECK(cf_frame_cursor(&ctx) == CF_CURSOR_DEFAULT, "and asks for no pointer cursor");

    cf_ctx_free(&ctx);
}

static void a_role_stops_the_climb_where_a_handler_would(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    cf_node leaf = box(40.0f, 40.0f, NULL, NULL);
    const cf_node children[] = {leaf};
    cf_node row = box(100.0f, 100.0f, NULL, NULL);
    row.role = CF_ROLE_LIST;
    row.children = children;
    row.child_count = 1u;
    const cf_node roots[] = {row};
    cf_node root = box(200.0f, 200.0f, NULL, NULL);
    root.children = roots;
    root.child_count = 1u;

    cf_frame_begin(&ctx);
    cf_diff(&ctx, &root);
    lay_out(&ctx, 200.0f, 200.0f);

    const uint32_t row_slot = child_slot(&ctx, 0u);
    const uint32_t leaf_slot = child_of(&ctx, row_slot, 0u);

    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_MOVE, .position = {.x = 10.0f, .y = 10.0f}});

    CF_CHECK(cf_hovered(&ctx, row_slot), "the themed family holds the hover for its states");
    CF_CHECK(!cf_hovered(&ctx, leaf_slot), "the leaf inside holds none of it");
    CF_CHECK(cf_frame_cursor(&ctx) == CF_CURSOR_DEFAULT, "no handler, no pointer cursor");

    cf_ctx_free(&ctx);
}

/* A wheel arrives with the pointer's own position, so the notch that comes the
 * moment a window takes focus - with the hand already resting inside it and no
 * move ahead of it - still reaches the box the reader is over. */
static void a_wheel_finds_the_box_under_its_own_point(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    recorder left = {0};
    recorder right = {0};
    recorder container = {0};
    cf_node children[] = {
        box(50.0f, 50.0f, on_pointer, &left),
        box(50.0f, 50.0f, on_pointer, &right),
    };
    cf_node root = box(200.0f, 200.0f, on_pointer, &container);
    root.children = children;
    root.child_count = 2u;

    cf_frame_begin(&ctx);
    cf_diff(&ctx, &root);
    lay_out(&ctx, 200.0f, 200.0f);

    cf_input(&ctx, (cf_event){.type = CF_EVENT_SCROLL,
                              .position = {.x = 60.0f, .y = 10.0f},
                              .wheel = {.x = 0.0f, .y = 1.0f}});

    CF_CHECK(right.scrolls == 1 && left.scrolls == 0, "the notch runs where the pointer is");
    CF_CHECK(container.scrolls == 1, "and climbs from there as a pointer event does");

    /* A booked hover stays the authority: a device that reports the wheel at
     * some other point moves the column the hand was last seen on. */
    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_MOVE, .position = {.x = 10.0f, .y = 10.0f}});
    cf_input(&ctx, (cf_event){.type = CF_EVENT_SCROLL,
                              .position = {.x = 60.0f, .y = 10.0f},
                              .wheel = {.x = 0.0f, .y = 1.0f}});

    CF_CHECK(left.scrolls == 1 && right.scrolls == 1, "the hover keeps the wheel it is on");

    cf_ctx_free(&ctx);
}

static void a_captured_pointer_keeps_its_events_until_the_release(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    recorder first = {0};
    recorder second = {0};
    recorder root_log = {0};
    cf_node children[] = {
        box(50.0f, 50.0f, on_pointer, &first),
        box(50.0f, 50.0f, on_pointer, &second),
    };
    cf_node root = box(200.0f, 200.0f, on_pointer, &root_log);
    root.children = children;
    root.child_count = 2u;

    cf_frame_begin(&ctx);
    cf_diff(&ctx, &root);
    lay_out(&ctx, 200.0f, 200.0f);

    const uint32_t left = child_slot(&ctx, 0u);

    first.capture = true;
    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_DOWN, .position = {.x = 10.0f, .y = 10.0f}});
    first.capture = false;

    CF_CHECK(first.downs == 1 && root_log.downs == 1, "the capture begins with the press");

    /* Every rect is left behind; the hold keeps the events on the node that
     * asked for them. */
    cf_input(&ctx,
             (cf_event){.type = CF_EVENT_POINTER_MOVE, .position = {.x = 300.0f, .y = 300.0f}});

    CF_CHECK(first.moves == 1 && root_log.moves == 1,
             "the move outside every rect reaches the captured node");
    CF_CHECK(cf_hovered(&ctx, left), "the hold keeps the hover");

    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_UP, .position = {.x = 300.0f, .y = 300.0f}});

    CF_CHECK(first.ups == 1 && second.ups == 0, "the release ends on the captured node");
    CF_CHECK(!cf_hovered(&ctx, left), "the hold ended with the release");

    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_MOVE, .position = {.x = 60.0f, .y = 10.0f}});

    CF_CHECK(cf_hovered(&ctx, child_slot(&ctx, 1u)), "hit testing resumes once the hold ends");

    cf_ctx_free(&ctx);
}

static void a_key_reaches_the_focused_node_and_bubbles(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    recorder child_log = {0};
    recorder root_log = {0};
    cf_node children[] = {box(50.0f, 50.0f, on_pointer, &child_log)};
    children[0].handlers.key = on_key;
    children[0].handlers.key_data = &child_log;
    cf_node root = box(200.0f, 200.0f, on_pointer, &root_log);
    root.handlers.key = on_key;
    root.handlers.key_data = &root_log;
    root.children = children;
    root.child_count = 1u;

    cf_frame_begin(&ctx);
    cf_diff(&ctx, &root);
    lay_out(&ctx, 200.0f, 200.0f);

    const uint32_t child = child_slot(&ctx, 0u);

    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_DOWN, .position = {.x = 10.0f, .y = 10.0f}});
    cf_input(&ctx, (cf_event){.type = CF_EVENT_KEY_DOWN, .key = 41u});

    CF_CHECK(child_log.keys == 1 && root_log.keys == 1,
             "the key runs the focused node's handler and bubbles");
    CF_CHECK(child_log.last_target == child, "the event names the focused node");

    child_log.stop = true;
    cf_input(&ctx, (cf_event){.type = CF_EVENT_KEY_UP, .key = 41u});

    CF_CHECK(child_log.keys == 2 && root_log.keys == 1,
             "a stopped key ends at the node that stopped it");

    cf_ctx_free(&ctx);
}

static void what_the_keyboard_wrote_goes_to_whoever_holds_focus(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    recorder child_log = {0};
    recorder root_log = {0};
    cf_node children[] = {box(50.0f, 50.0f, on_pointer, &child_log)};
    children[0].handlers.key = on_key;
    children[0].handlers.key_data = &child_log;
    cf_node root = box(200.0f, 200.0f, on_pointer, &root_log);
    root.handlers.key = on_key;
    root.handlers.key_data = &root_log;
    root.children = children;
    root.child_count = 1u;

    cf_frame_begin(&ctx);
    cf_diff(&ctx, &root);
    lay_out(&ctx, 200.0f, 200.0f);

    const uint32_t child = child_slot(&ctx, 0u);

    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_DOWN, .position = {.x = 10.0f, .y = 10.0f}});
    cf_input(&ctx, (cf_event){.type = CF_EVENT_TEXT, .text = {.chars = "hi", .length = 2u}});

    CF_CHECK(child_log.keys == 1 && root_log.keys == 1,
             "a written run reaches the focused node and bubbles like a key does");
    CF_CHECK(child_log.last_target == child, "the run names the focused node");

    cf_ctx_free(&ctx);
}

static void focus_set_by_hand_reaches_a_node_no_press_touched(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    recorder first = {0};
    recorder second = {0};
    cf_node children[] = {
        box(50.0f, 50.0f, NULL, NULL),
        box(50.0f, 50.0f, NULL, NULL),
    };
    children[0].handlers.key = on_key;
    children[0].handlers.key_data = &first;
    children[1].handlers.key = on_key;
    children[1].handlers.key_data = &second;
    cf_node root = box(200.0f, 200.0f, NULL, NULL);
    root.children = children;
    root.child_count = 2u;

    cf_frame_begin(&ctx);
    cf_diff(&ctx, &root);
    lay_out(&ctx, 200.0f, 200.0f);

    const uint32_t right = child_slot(&ctx, 1u);

    cf_focus(&ctx, right);

    CF_CHECK(cf_focused(&ctx, right), "the focus lands where it was set");
    CF_CHECK(!cf_focused(&ctx, child_slot(&ctx, 0u)), "the sibling stays out");
    CF_CHECK(!cf_focused(&ctx, ctx.root_slot), "the container stays out");

    cf_focus(&ctx, CF_NO_LINK);

    CF_CHECK(cf_focused(&ctx, right), "a slot that names no node is refused");

    cf_input(&ctx, (cf_event){.type = CF_EVENT_KEY_DOWN, .key = 41u});

    CF_CHECK(second.keys == 1 && first.keys == 0, "the key goes to the node focused by hand");

    cf_ctx_free(&ctx);
}

static void a_recycled_slot_reads_as_nothing(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    recorder child_log = {0};
    recorder root_log = {0};
    cf_node children[] = {box(50.0f, 50.0f, on_pointer, &child_log)};
    cf_node root = box(200.0f, 200.0f, on_pointer, &root_log);
    root.children = children;
    root.child_count = 1u;

    cf_frame_begin(&ctx);
    cf_diff(&ctx, &root);
    lay_out(&ctx, 200.0f, 200.0f);

    const uint32_t child = child_slot(&ctx, 0u);
    const uint32_t child_generation = cf_store_id(&ctx.nodes, child).generation;

    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_MOVE, .position = {.x = 10.0f, .y = 10.0f}});
    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_DOWN, .position = {.x = 10.0f, .y = 10.0f}});

    CF_CHECK(cf_hovered(&ctx, child) && cf_pressed(&ctx, child) && cf_focused(&ctx, child),
             "the press sets every state on the child");

    /* The declaration empties and the child retires. */
    cf_frame_begin(&ctx);
    cf_diff(&ctx, NULL);

    CF_CHECK(!cf_hovered(&ctx, child) && !cf_pressed(&ctx, child) && !cf_focused(&ctx, child),
             "a retired node keeps none of it");
    CF_CHECK(cf_store_id(&ctx.nodes, child).generation != child_generation,
             "retirement turns the slot's identity over");

    /* A fresh build reclaims the store's slots; whatever lands where, the
     * old slot answers as nothing until a new event names it. */
    cf_frame_begin(&ctx);
    cf_diff(&ctx, &root);
    lay_out(&ctx, 200.0f, 200.0f);

    CF_CHECK(!cf_hovered(&ctx, child) && !cf_pressed(&ctx, child) && !cf_focused(&ctx, child),
             "a recycled slot inherits nothing");

    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_MOVE, .position = {.x = 10.0f, .y = 10.0f}});
    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_DOWN, .position = {.x = 10.0f, .y = 10.0f}});

    const uint32_t fresh = child_slot(&ctx, 0u);

    CF_CHECK(cf_hovered(&ctx, fresh) && cf_pressed(&ctx, fresh) && cf_focused(&ctx, fresh),
             "the new node answers for itself");

    cf_ctx_free(&ctx);
}

static void dispatch_before_a_layout_hits_nothing(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    recorder child_log = {0};
    recorder root_log = {0};
    cf_node children[] = {box(50.0f, 50.0f, on_pointer, &child_log)};
    cf_node root = box(200.0f, 200.0f, on_pointer, &root_log);
    root.children = children;
    root.child_count = 1u;

    cf_frame_begin(&ctx);
    cf_diff(&ctx, &root);

    const uint32_t child = child_slot(&ctx, 0u);

    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_MOVE, .position = {.x = 10.0f, .y = 10.0f}});
    cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_DOWN, .position = {.x = 10.0f, .y = 10.0f}});
    cf_input(&ctx, (cf_event){.type = CF_EVENT_SCROLL, .wheel = {.x = 0.0f, .y = 1.0f}});
    cf_input(&ctx, (cf_event){.type = CF_EVENT_KEY_DOWN, .key = 41u});

    CF_CHECK(cf_hit_test(&ctx, (cf_point){.x = 10.0f, .y = 10.0f}) == CF_NO_LINK,
             "nothing answers a point before the rects exist");
    CF_CHECK(!cf_hovered(&ctx, child) && !cf_pressed(&ctx, child) && !cf_focused(&ctx, child),
             "no state settles before the rects exist");
    CF_CHECK(child_log.moves == 0 && child_log.downs == 0 && child_log.scrolls == 0 &&
                 child_log.keys == 0 && root_log.moves == 0 && root_log.downs == 0 &&
                 root_log.scrolls == 0 && root_log.keys == 0,
             "no handler runs before the rects exist");

    cf_ctx_free(&ctx);
}

int main(void) {
    printf("node input\n");

    CF_RUN(a_move_hovers_the_topmost_child_at_the_point);
    CF_RUN(the_cursor_names_the_pointer_only_over_a_handler);
    CF_RUN(a_press_marks_and_focuses_the_node_it_hit);
    CF_RUN(a_release_off_the_pressed_node_still_reaches_it);
    CF_RUN(a_press_on_a_plain_child_lands_on_the_button_around_it);
    CF_RUN(a_disabled_button_takes_no_press_and_silences_its_handlers);
    CF_RUN(a_role_stops_the_climb_where_a_handler_would);
    CF_RUN(a_stopping_handler_keeps_the_event_off_the_ancestors);
    CF_RUN(a_wheel_finds_the_box_under_its_own_point);
    CF_RUN(a_captured_pointer_keeps_its_events_until_the_release);
    CF_RUN(a_key_reaches_the_focused_node_and_bubbles);
    CF_RUN(what_the_keyboard_wrote_goes_to_whoever_holds_focus);
    CF_RUN(focus_set_by_hand_reaches_a_node_no_press_touched);
    CF_RUN(a_recycled_slot_reads_as_nothing);
    CF_RUN(dispatch_before_a_layout_hits_nothing);

    return CF_REPORT();
}
