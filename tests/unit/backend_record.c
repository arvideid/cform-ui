#include <stdio.h>

#include "cf/backend.h"
#include "cform_test.h"

/* The commands the store asks for before it grows for the first time. */
#define FIRST_CAPACITY 64u

static cf_arena store;

static cf_command fill_at(float x, float y, float width, float height, uint8_t red) {
    return (cf_command){.kind = CF_COMMAND_FILL,
                        .rect = (cf_rect){.x = x, .y = y, .width = width, .height = height},
                        .color = (cf_color){red, 0u, 0u, 255u}};
}

static int log_count;

static void count_backend_logs(cf_log_channel channel, const char *message, void *user_data) {
    (void)message;
    (void)user_data;

    if (channel == CF_LOG_BACKEND) {
        log_count++;
    }
}

static void a_recorder_holds_the_last_submitted_stream(void) {
    cf_recorder recorder = {.arena = &store};
    const cf_backend backend = cf_backend_recorder(&recorder);

    CF_CHECK(backend.abi_version == CF_BACKEND_ABI_VERSION, "the backend speaks the current ABI");

    const cf_command stream[] = {fill_at(0.0f, 0.0f, 10.0f, 10.0f, 1u),
                                 fill_at(20.0f, 0.0f, 10.0f, 10.0f, 2u)};
    backend.submit(backend.user_data, stream, 2u);

    CF_CHECK(recorder.count == 2u, "the recorder holds what was submitted");
    CF_CHECK(recorder.commands[0].rect.width == 10.0f, "the command is copied, not borrowed");
    CF_CHECK(recorder.commands[0].color.red == 1u, "the first command is first");
    CF_CHECK(recorder.commands[1].color.red == 2u, "the second follows it");

    const cf_command next[] = {fill_at(0.0f, 0.0f, 40.0f, 10.0f, 3u)};
    backend.submit(backend.user_data, next, 1u);

    CF_CHECK(recorder.count == 1u, "a submit replaces the frame, it does not append");
    CF_CHECK(recorder.commands[0].color.red == 3u, "the new frame is the one held");
}

static void an_idle_frame_leaves_the_recorder_empty(void) {
    cf_recorder recorder = {.arena = &store};
    const cf_backend backend = cf_backend_recorder(&recorder);

    backend.submit(backend.user_data, NULL, 0u);
    CF_CHECK(recorder.count == 0u, "an empty stream submits nothing");

    const cf_command stream[] = {fill_at(0.0f, 0.0f, 10.0f, 10.0f, 1u)};
    backend.submit(backend.user_data, stream, 1u);
    CF_CHECK(recorder.count == 1u, "a frame was held, to see what an idle frame does to it");

    backend.submit(backend.user_data, NULL, 0u);
    CF_CHECK(recorder.count == 0u, "an idle frame empties the recorder");
}

static void a_recorder_grows_its_store_by_doubling(void) {
    cf_recorder recorder = {.arena = &store};
    const cf_backend backend = cf_backend_recorder(&recorder);

    cf_command stream[FIRST_CAPACITY + 1u];

    for (uint32_t i = 0u; i < FIRST_CAPACITY + 1u; ++i) {
        stream[i] = fill_at(0.0f, 0.0f, 1.0f, 1.0f, (uint8_t)i);
    }

    backend.submit(backend.user_data, stream, FIRST_CAPACITY + 1u);

    CF_CHECK(recorder.count == FIRST_CAPACITY + 1u,
             "a stream past the first doubling is held whole");
    CF_CHECK(recorder.capacity >= FIRST_CAPACITY + 1u, "the store grew to fit");
    CF_CHECK(recorder.commands[0].color.red == 0u, "the start of the stream rode the growth");
    CF_CHECK(recorder.commands[FIRST_CAPACITY].color.red == (uint8_t)FIRST_CAPACITY,
             "the end of the stream rode the growth");
}

static void a_stream_that_does_not_fit_is_truncated_and_said_so(void) {
    cf_recorder recorder = {0};
    const cf_backend backend = cf_backend_recorder(&recorder);

    /* The first doubling fits exactly; the second ask has no room left. */
    cf_arena tiny;
    CF_CHECK(cf_arena_init(&tiny, (size_t)FIRST_CAPACITY * sizeof(cf_command)),
             "the test needs an arena sized for one doubling");

    recorder.arena = &tiny;

    log_count = 0;
    cf_log_install(count_backend_logs, NULL);

    cf_command stream[100u];

    for (uint32_t i = 0u; i < 100u; ++i) {
        stream[i] = fill_at(0.0f, 0.0f, 1.0f, 1.0f, 1u);
    }

    backend.submit(backend.user_data, stream, 100u);

    CF_CHECK(recorder.count == FIRST_CAPACITY, "the commands that fit stay in the recorder");
    CF_CHECK(log_count == 1, "the truncation is reported once on the backend channel");

    cf_arena_free(&tiny);
    cf_log_install(NULL, NULL);
}

static void a_backend_that_is_not_there_submits_nothing(void) {
    cf_recorder recorder = {0};
    const cf_backend without_arena = cf_backend_recorder(&recorder);

    const cf_command stream[] = {fill_at(0.0f, 0.0f, 10.0f, 10.0f, 1u)};
    without_arena.submit(without_arena.user_data, stream, 1u);

    CF_CHECK(recorder.count == 0u, "a recorder with no arena holds nothing");

    const cf_backend without_recorder = cf_backend_recorder(NULL);
    without_recorder.submit(without_recorder.user_data, stream, 1u);
    without_recorder.present(without_recorder.user_data);
}

int main(void) {
    printf("backend record\n");
    CF_CHECK(cf_arena_init(&store, 65536u), "the recorder needs an arena");

    CF_RUN(a_recorder_holds_the_last_submitted_stream);
    CF_RUN(an_idle_frame_leaves_the_recorder_empty);
    CF_RUN(a_recorder_grows_its_store_by_doubling);
    CF_RUN(a_stream_that_does_not_fit_is_truncated_and_said_so);
    CF_RUN(a_backend_that_is_not_there_submits_nothing);

    cf_arena_free(&store);
    return CF_REPORT();
}
