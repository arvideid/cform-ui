/*
 * Fallback HTTP backend, compiled when no transport is available for the
 * target (a native build without libcurl). Remote sources simply never load
 * and report why once; local paths are untouched because the renderer only
 * calls into here after ui_http_is_url() matched.
 */

#include <SDL3/SDL.h>

#include "http_loader.h"

UI_HttpDownload *ui_http_start(const char *url) {
    static bool warned;

    if (!warned) {
        warned = true;

        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "this build has no HTTP backend, so remote image sources are not "
            "loaded (first offender: %s). Link libcurl or build for the "
            "browser to enable them.", url != NULL ? url : "(null)");
    }

    return NULL;
}

UI_HttpStatus ui_http_status(const UI_HttpDownload *download) {
    (void)download;

    return UI_HTTP_FAILED;
}

const unsigned char *ui_http_bytes(const UI_HttpDownload *download, size_t *out_len) {
    (void)download;

    if (out_len != NULL) {
        *out_len = 0;
    }

    return NULL;
}

void ui_http_shutdown(void) {
}
