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

/* A submit that does nothing: the shell only asks whether one exists. */
static void fake_submit(void *user_data, const cf_command *commands, uint32_t count) {
    (void)user_data, (void)commands, (void)count;
}

static cf_backend driven_backend(cf_recorder *recorder) {
    cf_backend backend = cf_backend_recorder(recorder);
    backend.poll_event = fake_poll;
    backend.surface = fake_surface;
    backend.present = fake_present;
    return backend;
}

static void reset(const fake_hand *hands, uint32_t count) {
    schedule = hands;
    schedule_len = count;
    schedule_at = 0u;
    frames_built = 0u;
    presents = 0u;
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
    CF_CHECK(recorder.count == 8u, "the page's fill and the panel's seven commands arrived");

    bool text_seen = false;

    for (uint32_t i = 0u; i < recorder.count; ++i) {
        if (recorder.commands[i].kind == CF_COMMAND_TEXT) {
            text_seen = true;
        }
    }

    CF_CHECK(text_seen, "the panel's numbers ride the stream as text");
}

static void a_shell_that_cannot_run_sits_idle(void) {
    frames_built = 0u;

    cf_app_run(NULL);
    CF_CHECK(frames_built == 0u, "no config, no shell");

    const cf_app_config empty = {0};
    cf_app_run(&empty);
    CF_CHECK(frames_built == 0u, "a config without a build sits idle");

    const cf_app_config without_submit = {.build = counting_box,
                                          .backend = {.abi_version = CF_BACKEND_ABI_VERSION}};
    cf_app_run(&without_submit);
    CF_CHECK(frames_built == 0u, "a backend that cannot submit is never asked to build");

    cf_app_run(&(cf_app_config){
        .build = counting_box,
        .backend = {.abi_version = CF_BACKEND_ABI_VERSION - 1u, .submit = fake_submit}});
    CF_CHECK(frames_built == 0u, "a backend reading an older stream is not drawn for");
}

int main(void) {
    printf("app run\n");
    CF_CHECK(cf_arena_init(&store, 65536u), "the recorder needs an arena");

    CF_RUN(the_shell_runs_until_the_device_quits);
    CF_RUN(the_shell_skips_undamaged_frames);
    CF_RUN(the_inspector_toggles_on_f12);
    CF_RUN(a_shell_that_cannot_run_sits_idle);

    cf_arena_free(&store);
    return CF_REPORT();
}
