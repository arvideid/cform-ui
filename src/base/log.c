#include <stdarg.h>
#include <stdio.h>

#include "cf/base.h"

#define LOG_MESSAGE_CAPACITY 256

/*
 * The sink and its user pointer are the library's only mutable file scope, and
 * they are reachable only through cf_log_install. A backend installs once at
 * startup; nothing else in the engine knows where the words end up.
 */
static cf_log_sink installed_sink = NULL;
static void *installed_user_data = NULL;

void cf_log_install(cf_log_sink sink, void *user_data) {
    installed_sink = sink;
    installed_user_data = user_data;
}

void cf_logf(cf_log_channel channel, const char *format, ...) {
    if (installed_sink == NULL || format == NULL) {
        return;
    }

    char message[LOG_MESSAGE_CAPACITY];
    va_list arguments;

    va_start(arguments, format);
    const int written = vsnprintf(message, sizeof message, format, arguments);
    va_end(arguments);

    if (written < 0) {
        return;
    }

    installed_sink(channel, message, installed_user_data);
}
