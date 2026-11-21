#include "cf/app.h"

#include "cform_test.h"

/*
 * The shell is the app layer's whole surface: these cases play a device that
 * hands an event, dries up, then quits, and count the frames it got.
 */
static cf_arena store;
static uint32_t series;
static uint32_t frames_built;

static const cf_node *one_box(cf_ctx *ctx) {
    frames_built++;
    cf_node *root = cf_alloc(&ctx->frame, sizeof *root);

    if (root == NULL) {
        return NULL;
    }

    *root = (cf_node){.key = CF_NO_NAME};
    root->style = cf_style_default();
    root->style.width = CF_PX(40.0f);
    root->style.height = CF_PX(20.0f);
    root->style.background = (cf_opt_color){.present = true, .value = CF_COLOR(9, 9, 9)};

    return root;
}

/*
 * The two-drain schedule: the first poll hands a move and reports the queue
 * dry, so the shell builds a frame; the next drain hands the quit. One poll
 * handing both would drain both before the first frame and the shell would
 * sit idle.
 */
static bool fake_poll(void *user_data, cf_event *out) {
    (void)user_data;

    if (series == 0u) {
        series = 1u;
        *out = (cf_event){.type = CF_EVENT_POINTER_MOVE, .position = {30.0f, 20.0f}};
        return true;
    }

    if (series == 1u) {
        series = 2u;
        return false;
    }

    *out = (cf_event){.type = CF_EVENT_QUIT};
    return true;
}

static cf_size fake_surface(void *user_data) {
    (void)user_data;
    return (cf_size){320.0f, 200.0f};
}

static void the_shell_runs_until_the_device_quits(void) {
    cf_recorder recorder = {.arena = &store};
    cf_backend backend = cf_backend_recorder(&recorder);
    backend.poll_event = fake_poll;
    backend.surface = fake_surface;

    series = 0u;
    frames_built = 0u;

    cf_app_run(&(cf_app_config){.build = one_box, .backend = backend, .theme = NULL});

    CF_CHECK(frames_built == 1u, "the shell built the frame between the drains");
    CF_CHECK(recorder.count == 1u, "the frame's stream is the one the backend holds");
    CF_CHECK(recorder.count == 1u && recorder.commands[0].kind == CF_COMMAND_FILL,
             "the box's fill arrived");
}

static void a_shell_that_cannot_run_sits_idle(void) {
    frames_built = 0u;

    cf_app_run(NULL);
    CF_CHECK(frames_built == 0u, "no config, no shell");

    const cf_app_config empty = {0};
    cf_app_run(&empty);
    CF_CHECK(frames_built == 0u, "a config without a build sits idle");

    const cf_app_config without_submit = {.build = one_box, .backend = {0}};
    cf_app_run(&without_submit);
    CF_CHECK(frames_built == 0u, "a backend that cannot submit is never asked to build");
}

int main(void) {
    printf("app run\n");
    CF_CHECK(cf_arena_init(&store, 65536u), "the recorder needs an arena");

    CF_RUN(the_shell_runs_until_the_device_quits);
    CF_RUN(a_shell_that_cannot_run_sits_idle);

    cf_arena_free(&store);
    return CF_REPORT();
}
