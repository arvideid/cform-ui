#include <string.h>

#include "cf/backend.h"

/*
 * The first ask the store makes of the recorder's arena. A stream's size is
 * unknown ahead of the submit, so the store grows by doubling from here, the
 * way the paint list does, and an arena that refuses truncates the same way:
 * the commands that fit stay, and one line on the backend channel says so.
 */
#define RECORDER_FIRST_CAPACITY 64u

/*
 * The recorder holds the last submitted frame, so a submit starts it over -
 * an idle frame, a stream of nothing, leaves it empty. Growth carries what has
 * already been recorded into the bigger array, because the store is this
 * submit's and a reader of it cannot afford to lose its head.
 */
static void record_submit(void *user_data, const cf_command *commands, uint32_t count) {
    cf_recorder *recorder = user_data;

    if (recorder == NULL || recorder->arena == NULL) {
        return;
    }

    recorder->count = 0u;

    if (commands == NULL) {
        return;
    }

    for (uint32_t i = 0u; i < count; ++i) {
        cf_command stored = commands[i];

        /* A text command's bytes live in the arena the stream was built in;
         * the store outlives that arena's clear, so the bytes move into the
         * arena the store itself is carved from. */
        if (stored.kind == CF_COMMAND_TEXT && stored.text.length > 0u) {
            char *bytes = cf_alloc(recorder->arena, stored.text.length);

            if (bytes == NULL) {
                cf_logf(CF_LOG_BACKEND, "backend: recorder refused the stream at %u commands",
                        recorder->count);
                return;
            }

            memcpy(bytes, stored.text.chars, stored.text.length);
            stored.text.chars = bytes;
        }

        if (recorder->count == recorder->capacity) {
            const uint32_t capacity = recorder->capacity < RECORDER_FIRST_CAPACITY
                                          ? RECORDER_FIRST_CAPACITY
                                          : recorder->capacity * 2u;
            cf_command *store = cf_alloc(recorder->arena, (size_t)capacity * sizeof *store);

            if (store == NULL) {
                cf_logf(CF_LOG_BACKEND, "backend: recorder refused the stream at %u commands",
                        recorder->count);
                return;
            }

            if (recorder->commands != NULL) {
                memcpy(store, recorder->commands, (size_t)recorder->count * sizeof *store);
            }

            recorder->commands = store;
            recorder->capacity = capacity;
        }

        recorder->commands[recorder->count++] = stored;
    }
}

static void record_present(void *user_data) {
    (void)user_data;
}

/*
 * The shape the shell wants under the pointer, kept rather than drawn. A cursor
 * is not part of a frame's stream - an idle submit empties the commands and
 * leaves the pointer showing what it was shown - so the ask rides until the next
 * one, which is what lets a test read the shell's decision instead of the
 * frame's.
 */
static void record_set_cursor(void *user_data, cf_cursor cursor) {
    cf_recorder *recorder = user_data;

    if (recorder == NULL) {
        return;
    }

    recorder->cursor = cursor;
}

/*
 * Whether the page has a box that takes written text, kept rather than acted on.
 * A headless run has no keyboard to show and still has to answer the ask, because
 * the test that cares is the one reading whether the shell asked at all and with
 * which answer - the same reason the shape above is stored rather than drawn.
 */
static void record_set_text_input(void *user_data, bool enabled) {
    cf_recorder *recorder = user_data;

    if (recorder == NULL) {
        return;
    }

    recorder->text_input = enabled;
}

cf_backend cf_backend_recorder(cf_recorder *recorder) {
    return (cf_backend){.abi_version = CF_BACKEND_ABI_VERSION,
                        .size = sizeof(cf_backend),
                        .user_data = recorder,
                        .submit = record_submit,
                        .present = record_present,
                        .set_cursor = record_set_cursor,
                        .set_text_input = record_set_text_input};
}
