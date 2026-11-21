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
 * an idle frame, a stream of nothing, leaves it empty. Growth swaps the store
 * without carrying the old frame across: the new submit replaces it.
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

            recorder->commands = store;
            recorder->capacity = capacity;
        }

        recorder->commands[recorder->count++] = commands[i];
    }
}

static void record_present(void *user_data) {
    (void)user_data;
}

cf_backend cf_backend_recorder(cf_recorder *recorder) {
    return (cf_backend){.abi_version = CF_BACKEND_ABI_VERSION,
                        .user_data = recorder,
                        .submit = record_submit,
                        .present = record_present};
}
