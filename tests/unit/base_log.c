#include <stdio.h>
#include <string.h>

#include "cf/base.h"
#include "cform_test.h"

static cf_log_channel last_channel = CF_LOG_CORE;
static char last_message[512];

static void recording_sink(cf_log_channel channel, const char *message, void *user_data) {
    last_channel = channel;
    snprintf(last_message, sizeof last_message, "%s", message);
    (*(int *)user_data)++;
}

static void sink_receives_the_formatted_message(void) {
    int calls = 0;
    cf_log_install(recording_sink, &calls);

    cf_logf(CF_LOG_LAYOUT, "main axis gap %.1f overflows by %d", 12.5, 3);

    CF_CHECK(calls == 1, "the sink is called once per message");
    CF_CHECK(strstr(last_message, "gap 12.5") != NULL, "arguments reach the formatted text");
    CF_CHECK(strstr(last_message, "by 3") != NULL, "the second argument reaches it too");

    cf_log_install(NULL, NULL);
}

static void the_sink_sees_the_channel_that_wrote_it(void) {
    int calls = 0;
    cf_log_install(recording_sink, &calls);

    cf_logf(CF_LOG_ASSET, "font not found");
    CF_CHECK(last_channel == CF_LOG_ASSET, "an asset failure is tagged as one");

    cf_logf(CF_LOG_BACKEND, "swapchain lost");
    CF_CHECK(last_channel == CF_LOG_BACKEND, "a backend failure is tagged as one");

    cf_log_install(NULL, NULL);
}

static void installing_a_null_sink_silences_every_channel(void) {
    int calls = 0;
    cf_log_install(NULL, NULL);

    cf_logf(CF_LOG_CORE, "nobody is listening");

    CF_CHECK(calls == 0, "nothing was recorded");
    cf_log_install(recording_sink, &calls);
    cf_logf(CF_LOG_CORE, "now somebody is");
    CF_CHECK(calls == 1, "installing a sink starts the flow again");
    cf_log_install(NULL, NULL);
}

static void a_long_message_is_truncated_not_lost(void) {
    int calls = 0;
    cf_log_install(recording_sink, &calls);

    char padding[2048];
    memset(padding, 'x', sizeof padding - 1);
    padding[sizeof padding - 1] = '\0';

    cf_logf(CF_LOG_PAINT, "%s", padding);

    CF_CHECK(calls == 1, "the oversized message still arrives");
    CF_CHECK(strlen(last_message) < sizeof padding, "it was clipped to the log buffer");

    cf_log_install(NULL, NULL);
}

int main(void) {
    printf("log\n");
    CF_RUN(sink_receives_the_formatted_message);
    CF_RUN(the_sink_sees_the_channel_that_wrote_it);
    CF_RUN(installing_a_null_sink_silences_every_channel);
    CF_RUN(a_long_message_is_truncated_not_lost);
    return CF_REPORT();
}
