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

static uint32_t child_slot(const cf_ctx *ctx, uint32_t index) {
    const cf_tree *tree = cf_frame_tree(ctx);

    if (tree == NULL || ctx->root_slot == CF_NO_LINK) {
        return CF_NO_LINK;
    }

    return tree->child_slots[tree->child_start[ctx->root_slot] + index];
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

    CF_CHECK(cf_hovered(&ctx, child_slot(&ctx, 0u)), "the plain child still hovers");
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
    CF_RUN(a_stopping_handler_keeps_the_event_off_the_ancestors);
    CF_RUN(a_captured_pointer_keeps_its_events_until_the_release);
    CF_RUN(a_key_reaches_the_focused_node_and_bubbles);
    CF_RUN(focus_set_by_hand_reaches_a_node_no_press_touched);
    CF_RUN(a_recycled_slot_reads_as_nothing);
    CF_RUN(dispatch_before_a_layout_hits_nothing);

    return CF_REPORT();
}
