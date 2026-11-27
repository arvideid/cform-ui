#include <string.h>

#include "cf/app.h"

#include "cform_test.h"

/*
 * The shell is the app layer's whole surface: these cases play a device whose
 * every move is scripted - the events it hands, the ticks it goes dry, the
 * frame it quits - and count the frames it got.
 */
static cf_arena store;
static uint32_t frames_built;
static uint32_t presents;

/* The fake font the inspector prints through: a fixed rhythm and a fixed
 * advance, the same table shape a real font backend answers. */
static cf_text_metrics fake_metrics(void *font) {
    (void)font;
    return (cf_text_metrics){.line_height = 10.0f};
}

static float fake_advance(void *font, uint32_t codepoint, uint32_t previous) {
    (void)font, (void)codepoint, (void)previous;
    return 6.0f;
}

static bool fake_has_glyph(void *font, uint32_t codepoint) {
    (void)font, (void)codepoint;
    return true;
}

static const cf_text_ops fake_ops = {fake_metrics, fake_advance, fake_has_glyph};
static int fake_font_handle;
static const cf_font fake_font = {.handle = &fake_font_handle, .ops = &fake_ops};
static const cf_text_stack fake_fonts = {.fonts = &fake_font, .count = 1u};

static cf_node counting_box(cf_ctx *ctx) {
    frames_built++;
    (void)ctx;

    cf_node root = {0};
    root.style = cf_style_default();
    root.style.width = CF_PX(40.0f);
    root.style.height = CF_PX(20.0f);
    root.style.background = (cf_opt_color){.present = true, .value = CF_COLOR(9, 9, 9)};

    return root;
}

/* A box that acts on the pointer is the one the node layer answers a hand for, and
 * the shell is what carries that answer to the device. */
static void noting_pointer(cf_event *event, void *user_data) {
    (void)event, (void)user_data;
}

static cf_node clickable_box(cf_ctx *ctx) {
    cf_node root = counting_box(ctx);
    root.handlers.pointer = noting_pointer;

    return root;
}

/* A control the build named: the box a bridge would announce, and the one the panel
 * has words to print once the keyboard stands on it. */
static cf_node named_button(cf_ctx *ctx) {
    cf_node root = counting_box(ctx);
    cf_node box = {0};

    box.style = cf_style_default();
    box.style.width = CF_PX(20.0f);
    box.style.height = CF_PX(20.0f);
    box.focusable = true;
    box.access = CF_ACCESS_BUTTON;
    box.name = (cf_string){"Send", 4u};

    const cf_node children[] = {box};
    root.children = children;
    root.child_count = 1u;

    return root;
}

/* The page the keyboard walks: a field that takes written text beside a box that
 * takes the keyboard without ever wanting a keyboard drawn for it. The pair is what
 * makes the shell's answer worth counting - a page of fields alone could be answered
 * by a port that never shuts the service off. */
static cf_node typed_page(cf_ctx *ctx) {
    cf_node root = counting_box(ctx);
    cf_node field = {0};
    cf_node button = {0};

    field.style = cf_style_default();
    field.style.width = CF_PX(20.0f);
    field.style.height = CF_PX(20.0f);
    field.role = CF_ROLE_FIELD;
    field.focusable = true;
    field.handlers.pointer = noting_pointer;

    button.style = cf_style_default();
    button.style.width = CF_PX(20.0f);
    button.style.height = CF_PX(20.0f);
    button.focusable = true;
    button.handlers.pointer = noting_pointer;

    const cf_node children[] = {field, button};
    root.children = children;
    root.child_count = 2u;

    return root;
}

/* The page an input method is deciding over: a field the keyboard stands on, beside a second field
 * the keyboard can move to without the written-text answer changing. The second one is the point -
 * a shell that only ever compared "does this box take text" would leave a candidate window standing
 * over the field that lost the keyboard, while the page had stopped painting the run already. Both
 * are real widgets rather than nodes wearing the role, because the box the shell hands over is the
 * one the field measured. The buffers are the page's, so they outlive the frame that declared them.
 */
static char composed_bytes[32];
static char other_bytes[32];
static cf_text_edit composed;
static cf_text_edit other;

static cf_node composing_page(cf_ctx *ctx) {
    cf_node root = counting_box(ctx);
    cf_node field = cf_field(ctx, "entry", &composed, &fake_fonts, CF_FIELD_SINGLE);
    cf_node next = cf_field(ctx, "other", &other, &fake_fonts, CF_FIELD_SINGLE);

    field.style.width = CF_PX(20.0f);
    field.style.height = CF_PX(20.0f);
    next.style.width = CF_PX(20.0f);
    next.style.height = CF_PX(20.0f);

    const cf_node children[] = {field, next};
    root.children = children;
    root.child_count = 2u;

    return root;
}

/* The page's own way out: a press inside the box asks the shell to stop, and the
 * counter says how many presses the dispatch ever got. */
static uint32_t presses;

static void press_leaves(cf_event *event, void *user_data) {
    if (event->type == CF_EVENT_POINTER_DOWN) {
        presses++;
        cf_request_quit((cf_ctx *)user_data);
    }
}

static cf_node leaving_box(cf_ctx *ctx) {
    cf_node root = counting_box(ctx);
    root.handlers.pointer = press_leaves;
    root.handlers.pointer_data = ctx;

    return root;
}

/* A page mid-motion asks for the frame after this one, the only way a build
 * can keep running once the device stops handing events. */
static cf_node animating_box(cf_ctx *ctx) {
    cf_request_frame(ctx);

    return counting_box(ctx);
}

/*
 * A clipboard that serves nothing, to see the wiring with: what these cases ask is
 * whether the pair the host named in its config reaches the context a build reads,
 * and a build is the only place that is visible from outside the shell.
 */
static uint32_t boards_seen;

static cf_string silent_read(void *user_data, char *out, uint32_t capacity) {
    (void)user_data, (void)out, (void)capacity;
    return (cf_string){0};
}

static bool silent_write(void *user_data, cf_string utf8) {
    (void)user_data, (void)utf8;
    return false;
}

static cf_node noting_board(cf_ctx *ctx) {
    if (ctx->clipboard != NULL) {
        boards_seen++;
    }

    return counting_box(ctx);
}

typedef struct fake_hand {
    cf_event event;
    bool hands;
} fake_hand;

static const fake_hand *schedule;
static uint32_t schedule_at;
static uint32_t schedule_len;

static bool fake_poll(void *user_data, cf_event *out) {
    (void)user_data;

    if (schedule == NULL || schedule_at == schedule_len) {
        return false;
    }

    const fake_hand hand = schedule[schedule_at++];

    if (!hand.hands) {
        return false;
    }

    *out = hand.event;
    return true;
}

static cf_size fake_surface(void *user_data) {
    (void)user_data;
    return (cf_size){320.0f, 200.0f};
}

static void fake_present(void *user_data) {
    (void)user_data;
    presents++;
}

/* The four slots a host calls for itself, counted: a shell that uploaded,
 * decoded, retired or closed on its own initiative would leave the host holding
 * a backend it can no longer use or no longer trust. */
static uint32_t uploads;
static uint32_t loads;
static uint32_t releases;
static uint32_t destroys;

static uint32_t counted_create(void *user_data, uint32_t width, uint32_t height,
                               const void *pixels) {
    (void)user_data, (void)width, (void)height, (void)pixels;
    uploads++;
    return 1u;
}

static bool counted_load(void *user_data, const char *path, cf_image_content *out) {
    (void)user_data, (void)path, (void)out;
    loads++;
    return false;
}

static void counted_release(void *user_data, uint32_t texture) {
    (void)user_data, (void)texture;
    releases++;
}

static void counted_destroy(void *user_data) {
    (void)user_data;
    destroys++;
}

/* A submit that does nothing: the shell only asks whether one exists. */
static void fake_submit(void *user_data, const cf_command *commands, uint32_t count) {
    (void)user_data, (void)commands, (void)count;
}

/* The device a refusal is measured against: a move that earns a frame, a dry ask
 * that ends the drain, then the quit. Handing only a quit would prove nothing,
 * because the shell leaves on the first drain whether or not it ever compared the
 * backend it was given. */
static const fake_hand build_then_quit[] = {
    {{.type = CF_EVENT_POINTER_MOVE, .position = {30.0f, 20.0f}}, true},
    {{.type = CF_EVENT_POINTER_MOVE, .position = {0.0f, 0.0f}}, false},
    {{.type = CF_EVENT_QUIT}, true}};

/* A hand over a box that acts, a second frame with the hand still there, the pointer
 * moved off it, then a drain that hands nothing: the shape, the memo, the change back
 * and the idle turn that must cost the device no call at all. */
static const fake_hand cursor_walk[] = {
    {{.type = CF_EVENT_POINTER_MOVE, .position = {10.0f, 10.0f}}, true},
    {{.type = CF_EVENT_POINTER_MOVE, .position = {0.0f, 0.0f}}, false},
    {{.type = CF_EVENT_POINTER_MOVE, .position = {16.0f, 6.0f}}, true},
    {{.type = CF_EVENT_POINTER_MOVE, .position = {0.0f, 0.0f}}, false},
    {{.type = CF_EVENT_POINTER_MOVE, .position = {200.0f, 120.0f}}, true},
    {{.type = CF_EVENT_POINTER_MOVE, .position = {0.0f, 0.0f}}, false},
    {{.type = CF_EVENT_POINTER_MOVE, .position = {0.0f, 0.0f}}, false},
    {{.type = CF_EVENT_QUIT}, true}};

/* Every shape the shell asked this device to show, in the order it asked: a recorder
 * keeps the last one, and the slot's whole agreement is that it is asked when the
 * frame's answer moves rather than once per frame. */
static uint32_t cursor_asks;
static cf_cursor asked_shapes[4u];

static void counted_cursor(void *user_data, cf_cursor cursor) {
    (void)user_data;

    if (cursor_asks < (uint32_t)(sizeof asked_shapes / sizeof asked_shapes[0])) {
        asked_shapes[cursor_asks] = cursor;
    }

    cursor_asks++;
}

/* The same count for the other ask: what the shell told the device about written
 * text, in the order it told it. */
static uint32_t text_asks;
static bool asked_text[4u];

static void counted_text(void *user_data, bool enabled) {
    (void)user_data;

    if (text_asks < (uint32_t)(sizeof asked_text / sizeof asked_text[0])) {
        asked_text[text_asks] = enabled;
    }

    text_asks++;
}

/* The same count for the box an input method would stand its answers beside: every rect the
 * shell told the device, in the order it told it. A recorder only keeps the last one, and what
 * these cases are after is whether the shell told it once per move rather than once per frame. */
static uint32_t candidate_asks;
static cf_rect asked_boxes[6u];

static void counted_candidate(void *user_data, cf_rect box) {
    (void)user_data;

    if (candidate_asks < (uint32_t)(sizeof asked_boxes / sizeof asked_boxes[0])) {
        asked_boxes[candidate_asks] = box;
    }

    candidate_asks++;
}

/* The device the keyboard walk runs on: a hand, a dry ask to end the drain, the same
 * press again now the page has a rect to land on, a press on the box that takes no
 * text, a move that changes nothing, then the quit. */
static const fake_hand keyboard_walk[] = {
    {{.type = CF_EVENT_POINTER_DOWN, .position = {10.0f, 10.0f}, .button = 1u}, true},
    {{.type = CF_EVENT_POINTER_MOVE, .position = {0.0f, 0.0f}}, false},
    {{.type = CF_EVENT_POINTER_DOWN, .position = {10.0f, 10.0f}, .button = 1u}, true},
    {{.type = CF_EVENT_POINTER_MOVE, .position = {10.0f, 10.0f}}, false},
    {{.type = CF_EVENT_POINTER_DOWN, .position = {30.0f, 10.0f}, .button = 1u}, true},
    {{.type = CF_EVENT_POINTER_MOVE, .position = {30.0f, 10.0f}}, false},
    {{.type = CF_EVENT_POINTER_MOVE, .position = {30.0f, 10.0f}}, true},
    {{.type = CF_EVENT_POINTER_MOVE, .position = {30.0f, 10.0f}}, false},
    {{.type = CF_EVENT_QUIT}, true}};

/* The same walk with a run being decided in the middle of it: the field is focused by the second
 * press, an input method says what it is holding, a frame passes with nothing moved, and the
 * keyboard then goes to the box beside it - which is the move the run cannot survive. */
static const fake_hand composition_walk[] = {
    {{.type = CF_EVENT_POINTER_DOWN, .position = {10.0f, 10.0f}, .button = 1u}, true},
    {{.type = CF_EVENT_POINTER_MOVE, .position = {0.0f, 0.0f}}, false},
    {{.type = CF_EVENT_POINTER_DOWN, .position = {10.0f, 10.0f}, .button = 1u}, true},
    {{.type = CF_EVENT_POINTER_MOVE, .position = {10.0f, 10.0f}}, false},
    {{.type = CF_EVENT_COMPOSITION, .text = {.chars = "ni", .length = 2u}}, true},
    {{.type = CF_EVENT_POINTER_MOVE, .position = {10.0f, 10.0f}}, false},
    {{.type = CF_EVENT_POINTER_MOVE, .position = {30.0f, 10.0f}}, true},
    {{.type = CF_EVENT_POINTER_MOVE, .position = {30.0f, 10.0f}}, false},
    {{.type = CF_EVENT_POINTER_DOWN, .position = {30.0f, 10.0f}, .button = 1u}, true},
    {{.type = CF_EVENT_POINTER_MOVE, .position = {30.0f, 10.0f}}, false},
    {{.type = CF_EVENT_QUIT}, true}};

static cf_backend driven_backend(cf_recorder *recorder) {
    cf_backend backend = cf_backend_recorder(recorder);
    backend.poll_event = fake_poll;
    backend.surface = fake_surface;
    backend.present = fake_present;
    return backend;
}

/* Every case starts the shell from the same place: the device's queue, the counters,
 * and the arena the recorder carves its store out of. That arena is one allocation
 * for the whole binary, so rewinding it is what keeps a case that filled it from
 * leaving the next one holding an empty stream - which reads as a shell that never
 * submitted, not as a heap that ran out. */
static void reset(const fake_hand *hands, uint32_t count) {
    cf_arena_clear(&store);
    schedule = hands;
    schedule_len = count;
    schedule_at = 0u;
    frames_built = 0u;
    presents = 0u;
    boards_seen = 0u;
    cursor_asks = 0u;
    presses = 0u;
    text_asks = 0u;
    candidate_asks = 0u;

    for (uint32_t at = 0u; at < (uint32_t)(sizeof asked_boxes / sizeof asked_boxes[0]); ++at) {
        asked_boxes[at] = (cf_rect){-1.0f, -1.0f, -1.0f, -1.0f};
    }

    composed = (cf_text_edit){.chars = composed_bytes, .capacity = sizeof composed_bytes};
    other = (cf_text_edit){.chars = other_bytes, .capacity = sizeof other_bytes};

    for (uint32_t at = 0u; at < (uint32_t)(sizeof asked_shapes / sizeof asked_shapes[0]); ++at) {
        asked_shapes[at] = CF_CURSOR_DEFAULT;
    }

    for (uint32_t at = 0u; at < (uint32_t)(sizeof asked_text / sizeof asked_text[0]); ++at) {
        asked_text[at] = false;
    }
}

static void the_shell_runs_until_the_device_quits(void) {
    cf_recorder recorder = {.arena = &store};
    const cf_backend backend = driven_backend(&recorder);

    /* The two-drain schedule: the first poll hands a move and goes dry, so
     * the shell builds a frame; the next drain hands the quit. One poll
     * handing both would drain both before the first frame and the shell
     * would sit idle. */
    static const fake_hand hands[] = {
        {{.type = CF_EVENT_POINTER_MOVE, .position = {30.0f, 20.0f}}, true},
        {{.type = CF_EVENT_POINTER_MOVE, .position = {0.0f, 0.0f}}, false},
        {{.type = CF_EVENT_QUIT}, true}};
    reset(hands, 3u);

    cf_app_run(&(cf_app_config){.build = counting_box, .backend = backend, .theme = NULL});

    CF_CHECK(frames_built == 1u, "the shell built the frame between the drains");
    CF_CHECK(recorder.count == 1u, "the frame's stream is the one the backend holds");
    CF_CHECK(recorder.count == 1u && recorder.commands[0].kind == CF_COMMAND_FILL,
             "the box's fill arrived");
}

static void the_shell_skips_undamaged_frames(void) {
    cf_recorder recorder = {.arena = &store};
    const cf_backend backend = driven_backend(&recorder);

    static const fake_hand hands[] = {
        {{.type = CF_EVENT_POINTER_MOVE, .position = {30.0f, 20.0f}}, true},
        {{.type = CF_EVENT_POINTER_MOVE, .position = {0.0f, 0.0f}}, false},
        {{.type = CF_EVENT_POINTER_MOVE, .position = {0.0f, 0.0f}}, false},
        {{.type = CF_EVENT_POINTER_MOVE, .position = {0.0f, 0.0f}}, false},
        {{.type = CF_EVENT_QUIT}, true}};
    reset(hands, 5u);

    cf_app_run(&(cf_app_config){.build = counting_box, .backend = backend, .theme = NULL});

    CF_CHECK(frames_built == 1u, "a frame with nothing new does not build again");
    CF_CHECK(presents == 3u, "the window keeps being served while nothing changes");
}

static void the_shell_keeps_frames_a_moving_page_asks_for(void) {
    cf_recorder recorder = {.arena = &store};
    const cf_backend backend = driven_backend(&recorder);

    /* Two drains the device hands nothing on, past the one that ended the
     * frame's own drain: without an ask the shell would build once and then
     * only present, so the count is the proof the ask was honored rather than
     * the tree having moved. */
    static const fake_hand hands[] = {
        {{.type = CF_EVENT_POINTER_MOVE, .position = {30.0f, 20.0f}}, true},
        {{.type = CF_EVENT_POINTER_MOVE, .position = {0.0f, 0.0f}}, false},
        {{.type = CF_EVENT_POINTER_MOVE, .position = {0.0f, 0.0f}}, false},
        {{.type = CF_EVENT_POINTER_MOVE, .position = {0.0f, 0.0f}}, false},
        {{.type = CF_EVENT_QUIT}, true}};
    reset(hands, 5u);

    cf_app_run(&(cf_app_config){.build = animating_box, .backend = backend, .theme = NULL});

    CF_CHECK(frames_built == 3u, "an ask builds the frame it asks for, event or no event");
}

static void the_inspector_toggles_on_f12(void) {
    cf_recorder recorder = {.arena = &store};
    const cf_backend backend = driven_backend(&recorder);

    static const fake_hand hands[] = {
        {{.type = CF_EVENT_KEY_DOWN, .key = CF_KEY_F12}, true},
        {{.type = CF_EVENT_POINTER_MOVE, .position = {0.0f, 0.0f}}, false},
        {{.type = CF_EVENT_QUIT}, true}};
    reset(hands, 3u);

    cf_app_run(&(cf_app_config){
        .build = counting_box, .backend = backend, .theme = NULL, .fonts = &fake_fonts});

    CF_CHECK(frames_built == 1u, "the toggle builds the frame it lands in");
    CF_CHECK(recorder.count == 9u, "the page's fill and the panel's eight commands arrived");

    bool text_seen = false;

    for (uint32_t i = 0u; i < recorder.count; ++i) {
        if (recorder.commands[i].kind == CF_COMMAND_TEXT) {
            text_seen = true;
        }
    }

    CF_CHECK(text_seen, "the panel's numbers ride the stream as text");
}

/*
 * The panel's last line is the bridge walk read back rather than a second, weaker rule: the box
 * the keyboard stands on, named by the build, printed as the words a person would hear. A developer
 * catches a control nobody named by seeing the difference between the dash and the name, so both
 * ends are worth a case. The key that opens the panel is handed after a frame has laid the page
 * out, because Tab walks the tree the last frame built and the first press has no rect to answer
 * from.
 */
static void the_inspector_prints_the_words_a_bridge_would_say(void) {
    cf_recorder recorder = {.arena = &store};
    const cf_backend backend = driven_backend(&recorder);

    static const fake_hand hands[] = {
        {{.type = CF_EVENT_POINTER_MOVE, .position = {30.0f, 20.0f}}, true},
        {{.type = CF_EVENT_POINTER_MOVE, .position = {0.0f, 0.0f}}, false},
        {{.type = CF_EVENT_KEY_DOWN, .key = CF_KEY_TAB}, true},
        {{.type = CF_EVENT_POINTER_MOVE, .position = {0.0f, 0.0f}}, false},
        {{.type = CF_EVENT_KEY_DOWN, .key = CF_KEY_F12}, true},
        {{.type = CF_EVENT_POINTER_MOVE, .position = {0.0f, 0.0f}}, false},
        {{.type = CF_EVENT_QUIT}, true}};
    reset(hands, 7u);

    cf_app_run(&(cf_app_config){
        .build = named_button, .backend = backend, .theme = NULL, .fonts = &fake_fonts});

    CF_CHECK(frames_built == 3u, "the frame, the key and the toggle each earned one");

    const char *said = NULL;
    uint32_t spoken = 0u;

    for (uint32_t i = 0u; i < recorder.count; ++i) {
        const cf_command *command = &recorder.commands[i];

        if (command->kind == CF_COMMAND_TEXT && command->text.length >= 5u &&
            memcmp(command->text.chars, "says ", 5u) == 0) {
            said = command->text.chars + 5u;
            spoken = command->text.length - 5u;
        }
    }

    CF_CHECK(said != NULL, "the open panel prints an announced line");
    CF_CHECK(spoken == 4u && said != NULL && memcmp(said, "Send", 4u) == 0,
             "and it prints the name the walk gave the box holding the keyboard");
}

static void the_shell_leaves_the_backend_to_its_host(void) {
    cf_recorder recorder = {.arena = &store};
    cf_backend backend = driven_backend(&recorder);

    backend.create_texture = counted_create;
    backend.load_image = counted_load;
    backend.release_texture = counted_release;
    backend.destroy = counted_destroy;

    static const fake_hand hands[] = {
        {{.type = CF_EVENT_POINTER_MOVE, .position = {30.0f, 20.0f}}, true},
        {{.type = CF_EVENT_POINTER_MOVE, .position = {0.0f, 0.0f}}, false},
        {{.type = CF_EVENT_QUIT}, true}};
    reset(hands, 3u);
    uploads = 0u;
    loads = 0u;
    releases = 0u;
    destroys = 0u;

    cf_app_run(&(cf_app_config){.build = counting_box, .backend = backend, .theme = NULL});

    CF_CHECK(frames_built == 1u, "the shell ran the frame it was asked to run");
    CF_CHECK(uploads == 0u && loads == 0u, "the shell asks for no image of its own");
    CF_CHECK(releases == 0u, "the shell retires no upload on the way out");
    CF_CHECK(destroys == 0u, "the shell does not close a window it was only handed");
}

static void a_shell_that_cannot_run_sits_idle(void) {
    frames_built = 0u;

    cf_app_run(NULL);
    CF_CHECK(frames_built == 0u, "no config, no shell");

    const cf_app_config empty = {0};
    cf_app_run(&empty);
    CF_CHECK(frames_built == 0u, "a config without a build sits idle");

    const cf_app_config without_submit = {
        .build = counting_box,
        .backend = {.abi_version = CF_BACKEND_ABI_VERSION, .size = sizeof(cf_backend)}};
    cf_app_run(&without_submit);
    CF_CHECK(frames_built == 0u, "a backend that cannot submit is never asked to build");

    /* Both comparisons run against a device that would otherwise get a frame
     * built: a count of zero only means the refusal happened if entering the
     * loop would have produced a non-zero one. */
    reset(build_then_quit, 3u);

    cf_app_run(&(cf_app_config){.build = counting_box,
                                .backend = {.abi_version = CF_BACKEND_ABI_VERSION - 1u,
                                            .size = sizeof(cf_backend),
                                            .poll_event = fake_poll,
                                            .submit = fake_submit,
                                            .present = fake_present}});
    CF_CHECK(frames_built == 0u, "a backend reading an older stream is not drawn for");

    /* submit present but present absent is the incomplete shell the loop must
     * refuse before it ever paints: the old code built a frame and then called
     * a NULL present, crashing on the first one. */
    reset(build_then_quit, 3u);

    cf_app_run(&(cf_app_config){.build = counting_box,
                                .backend = {.abi_version = CF_BACKEND_ABI_VERSION,
                                            .size = sizeof(cf_backend),
                                            .poll_event = fake_poll,
                                            .submit = fake_submit}});
    CF_CHECK(frames_built == 0u, "a backend that cannot present is never entered");

    /* The current number on a struct of another size is a library built against a
     * different revision of the header: its slots sit at other offsets, so reading
     * the stream through them would draw wrong rather than fail. */
    reset(build_then_quit, 3u);

    cf_app_run(&(cf_app_config){.build = counting_box,
                                .backend = {.abi_version = CF_BACKEND_ABI_VERSION,
                                            .size = (uint32_t)sizeof(cf_backend) - 8u,
                                            .poll_event = fake_poll,
                                            .submit = fake_submit,
                                            .present = fake_present}});
    CF_CHECK(frames_built == 0u, "a backend whose struct is not this size is refused");

    /* And the same device with both numbers right is drawn for: the refusals
     * above are the checks speaking, not a shell that cannot run at all. */
    reset(build_then_quit, 3u);

    cf_app_run(&(cf_app_config){.build = counting_box,
                                .backend = {.abi_version = CF_BACKEND_ABI_VERSION,
                                            .size = sizeof(cf_backend),
                                            .poll_event = fake_poll,
                                            .submit = fake_submit,
                                            .present = fake_present}});
    CF_CHECK(frames_built == 1u, "a backend that states both numbers correctly is entered");
}

/* The host's pair arrives at the page it was named for, and its absence arrives
 * as an absence rather than as something a widget has to guess about. */
static void the_shell_hands_the_page_the_clipboard_the_host_named(void) {
    const cf_clipboard board = {.read = silent_read, .write = silent_write, .user_data = NULL};
    cf_recorder recorder = {.arena = &store};
    const cf_backend backend = driven_backend(&recorder);

    reset(build_then_quit, 3u);
    cf_app_run(&(cf_app_config){
        .build = noting_board, .backend = backend, .theme = NULL, .clipboard = &board});

    CF_CHECK(frames_built == 1u, "the scripted move earned a frame");
    CF_CHECK(boards_seen == 1u, "and the build could read the pair the config named");

    reset(build_then_quit, 3u);
    cf_app_run(&(cf_app_config){.build = noting_board, .backend = backend, .theme = NULL});

    CF_CHECK(frames_built == 1u, "the same move earns a frame without one");
    CF_CHECK(boards_seen == 0u, "and the page is handed no clipboard it did not ask for");
}

/* The shape the frame answers for the pointer is the shell's to hand over, and only
 * when it moves: a page the hand is resting on costs the device no call per frame,
 * which is the same invariant the idle shortcut keeps for layout and paint. */
static void the_shell_asks_for_the_shape_only_when_it_moves(void) {
    cf_recorder recorder = {.arena = &store};
    cf_backend backend = driven_backend(&recorder);
    backend.set_cursor = counted_cursor;

    reset(cursor_walk, 8u);
    cf_app_run(&(cf_app_config){.build = clickable_box, .backend = backend, .theme = NULL});

    CF_CHECK(frames_built == 3u, "three drains earned three frames");
    CF_CHECK(cursor_asks == 2u, "and the device was asked twice, not once per frame");
    CF_CHECK(asked_shapes[0] == CF_CURSOR_POINTER, "the hand, over a box that acts");
    CF_CHECK(asked_shapes[1] == CF_CURSOR_DEFAULT, "the arrow, once the pointer left it");
}

/* A device that has no shape to change is a legal device: the walk runs the same
 * frames, and the recorder is left holding the shape it started with because nothing
 * reached it. */
static void a_device_with_no_cursor_slot_still_runs(void) {
    cf_recorder recorder = {.arena = &store};
    cf_backend backend = driven_backend(&recorder);
    backend.set_cursor = NULL;
    recorder.cursor = CF_CURSOR_TEXT;

    reset(cursor_walk, 8u);
    cf_app_run(&(cf_app_config){.build = clickable_box, .backend = backend, .theme = NULL});

    CF_CHECK(frames_built == 3u, "the frames ran without anywhere to put the shape");
    CF_CHECK(recorder.cursor == CF_CURSOR_TEXT, "and the device was handed no shape at all");
}

/*
 * The keyboard's service is the shell's to hand over, on the same agreement the
 * cursor's is: told once before the page has anywhere for text, again when a press
 * reaches a field, again when the keyboard moves where text cannot go, and not at all
 * by the frame that changed nothing. The press that focuses the field is the second
 * one - the first is absorbed for want of a layout, the way the cursor walk finds it.
 */
static void the_shell_opens_the_keyboard_only_for_a_field(void) {
    cf_recorder recorder = {.arena = &store};
    cf_backend backend = driven_backend(&recorder);
    backend.set_text_input = counted_text;

    reset(keyboard_walk, 9u);
    cf_app_run(&(cf_app_config){.build = typed_page, .backend = backend, .theme = NULL});

    CF_CHECK(frames_built == 4u, "four waking drains earned four frames");
    CF_CHECK(text_asks == 3u, "and the device was told three times, not once per frame");
    CF_CHECK(!asked_text[0u], "the frame that laid the page out shut the service no field needed");
    CF_CHECK(asked_text[1u], "the press that reached the field opened it again");
    CF_CHECK(!asked_text[2u], "and the press onto a box that takes no text shut it");
}

/* A device with no keyboard to show - an offscreen run, a headless render, a platform
 * that types whatever it is told - is a legal device: the frames it runs are the frames
 * it would have run, and its own answer is left untouched. */
static void a_device_with_no_keyboard_slot_still_runs(void) {
    cf_recorder recorder = {.arena = &store};
    cf_backend backend = driven_backend(&recorder);
    backend.set_text_input = NULL;
    recorder.text_input = true;

    reset(keyboard_walk, 9u);
    cf_app_run(&(cf_app_config){.build = typed_page, .backend = backend, .theme = NULL});

    CF_CHECK(frames_built == 4u, "the frames ran with nowhere to send the ask");
    CF_CHECK(recorder.text_input, "and the device was handed no answer at all");
}

/*
 * The box the device would stand its candidates against is the frame's third answer, and it
 * travels the same door as the other two: told once before anything is being decided, told again
 * when a run appears, told a third time when the keyboard moves and takes the run with it. The
 * frame between the last two is what the case is for - the pointer left the field and the page
 * still cost the device no call, because the box had not moved - and the third answer has to be
 * empty rather than the field beside it's own box, since a keyboard that changed hands is a method
 * that lost the run it was deciding.
 *
 * The numbers are the font's own: a ten-point row and a six-point advance make the two bytes the
 * method is holding twelve points across, sitting at the start of a field that begins at the
 * page's origin.
 */
static void the_shell_tells_the_device_where_the_run_sits(void) {
    cf_recorder recorder = {.arena = &store};
    cf_backend backend = driven_backend(&recorder);
    backend.set_candidate_rect = counted_candidate;

    reset(composition_walk, 11u);
    cf_app_run(&(cf_app_config){.build = composing_page, .backend = backend, .theme = NULL});

    CF_CHECK(frames_built == 5u, "five waking drains earned five frames");
    CF_CHECK(candidate_asks == 3u, "and the device was told three times, not once per frame");
    CF_CHECK(asked_boxes[0u].width == 0.0f && asked_boxes[0u].height == 0.0f,
             "the page opened with nothing being decided");
    CF_CHECK(asked_boxes[1u].width == 12.0f && asked_boxes[1u].height == 10.0f,
             "the run the method handed over is boxed at the row the page lays it on");
    CF_CHECK(asked_boxes[2u].width == 0.0f && asked_boxes[2u].height == 0.0f,
             "and the box goes empty the frame the keyboard changes hands");
    CF_CHECK(other.length == 0u,
             "the field the keyboard moved onto takes the run as nothing at all");
    CF_CHECK(composed.length == 0u, "and the field it left never got the bytes either");
}

/* A device that places its own window, or has no input method at all, is a legal device: the
 * page is being decided either way and the preedit is drawn out of the stream, so a shell with
 * nowhere to send the box runs the frames it would have run and tells the device nothing. */
static void a_device_with_no_candidate_slot_still_runs(void) {
    cf_recorder recorder = {.arena = &store};
    cf_backend backend = driven_backend(&recorder);
    backend.set_candidate_rect = NULL;
    recorder.candidate = (cf_rect){7.0f, 7.0f, 7.0f, 7.0f};

    reset(composition_walk, 11u);
    cf_app_run(&(cf_app_config){.build = composing_page, .backend = backend, .theme = NULL});

    CF_CHECK(frames_built == 5u, "the frames ran with nowhere to send the box");
    CF_CHECK(recorder.candidate.width == 7.0f, "and the device was handed no rect at all");
}

/* The two ends of a run differ only in who names them: the window's close button
 * arrives as an event, and a control inside the page has no event to send, so it
 * asks through its context. The frame that answered the press is shown, and the
 * events queued behind it are never read - the shell left, it did not pause.
 *
 * The first press is absorbed: dispatch walks the rects the last frame laid out, and
 * before any frame there is none to hit, so the box gets its rect from that frame and
 * the press after it is the one the handler sees. The third is what the check is for -
 * a shell that only paused would dispatch it and count it. */
static void a_page_that_asks_leaves_after_the_frame_that_answers_it(void) {
    cf_recorder recorder = {.arena = &store};
    const cf_backend backend = driven_backend(&recorder);

    static const fake_hand hands[] = {
        {{.type = CF_EVENT_POINTER_DOWN, .position = {10.0f, 10.0f}, .button = 1u}, true},
        {{.type = CF_EVENT_POINTER_MOVE, .position = {0.0f, 0.0f}}, false},
        {{.type = CF_EVENT_POINTER_DOWN, .position = {10.0f, 10.0f}, .button = 1u}, true},
        {{.type = CF_EVENT_POINTER_MOVE, .position = {0.0f, 0.0f}}, false},
        {{.type = CF_EVENT_POINTER_DOWN, .position = {10.0f, 10.0f}, .button = 1u}, true},
        {{.type = CF_EVENT_POINTER_MOVE, .position = {0.0f, 0.0f}}, false},
        {{.type = CF_EVENT_QUIT}, true}};
    reset(hands, 7u);

    cf_app_run(&(cf_app_config){.build = leaving_box, .backend = backend, .theme = NULL});

    CF_CHECK(frames_built == 2u, "the ask earned the frame that answered it, no more");
    CF_CHECK(presents == 2u, "and that frame reached the screen before the loop went");
    CF_CHECK(presses == 1u, "and the press queued behind it was never dispatched");
    CF_CHECK(recorder.count == 1u, "the stream it left behind is the page's own fill");
}

int main(void) {
    printf("app run\n");
    CF_CHECK(cf_arena_init(&store, 65536u), "the recorder needs an arena");

    CF_RUN(the_shell_runs_until_the_device_quits);
    CF_RUN(the_shell_skips_undamaged_frames);
    CF_RUN(the_shell_keeps_frames_a_moving_page_asks_for);
    CF_RUN(the_inspector_toggles_on_f12);
    CF_RUN(the_inspector_prints_the_words_a_bridge_would_say);
    CF_RUN(the_shell_leaves_the_backend_to_its_host);
    CF_RUN(the_shell_hands_the_page_the_clipboard_the_host_named);
    CF_RUN(the_shell_asks_for_the_shape_only_when_it_moves);
    CF_RUN(a_device_with_no_cursor_slot_still_runs);
    CF_RUN(the_shell_opens_the_keyboard_only_for_a_field);
    CF_RUN(a_device_with_no_keyboard_slot_still_runs);
    CF_RUN(the_shell_tells_the_device_where_the_run_sits);
    CF_RUN(a_device_with_no_candidate_slot_still_runs);
    CF_RUN(a_page_that_asks_leaves_after_the_frame_that_answers_it);
    CF_RUN(a_shell_that_cannot_run_sits_idle);

    cf_arena_free(&store);
    return CF_REPORT();
}
