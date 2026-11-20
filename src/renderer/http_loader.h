#ifndef CFORM_HTTP_LOADER_H
#define CFORM_HTTP_LOADER_H

/*
 * Internal to the SDL backend: asynchronous byte downloads for remote image
 * sources. Two platform implementations exist and the build picks one -
 * http_loader_curl.c for native targets (libcurl on a worker thread) and
 * http_loader_emscripten.c for the browser (emscripten_fetch callbacks).
 * Callers only ever see this header.
 */

#include <stdbool.h>
#include <stddef.h>

#include <SDL3/SDL.h>

/* Opaque handle; the concrete struct lives in whichever backend is linked. */
typedef struct UI_HttpDownload UI_HttpDownload;

typedef enum {
    UI_HTTP_PENDING,   /* still transferring - ask again on a later frame */
    UI_HTTP_READY,     /* bytes complete and readable */
    UI_HTTP_FAILED     /* terminal error, already logged; stop asking */
} UI_HttpStatus;

/* True when src starts with http:// or https://, case-insensitive. Says which
 * strings need a transport at all, so it is not any one backend's business. */
static inline bool ui_http_is_url(const char *src) {
    if (src == NULL) {
        return false;
    }

    return SDL_strncasecmp(src, "http://", 7) == 0
        || SDL_strncasecmp(src, "https://", 8) == 0;
}

/* Begins downloading url without blocking, or returns the running download
 * when the same URL is already tracked. The handle belongs to the loader and
 * stays valid until ui_http_shutdown(); NULL means the table is full. */
UI_HttpDownload *ui_http_start(const char *url);

UI_HttpStatus ui_http_status(const UI_HttpDownload *download);

/* Downloaded bytes, valid only while the status is UI_HTTP_READY and until
 * ui_http_shutdown() runs. The caller must copy or consume them by then. */
const unsigned char *ui_http_bytes(const UI_HttpDownload *download, size_t *out_len);

/* Cancels outstanding transfers, joins worker threads and frees every buffer.
 * Must run before the renderer tears down. */
void ui_http_shutdown(void);

#endif /* CFORM_HTTP_LOADER_H */
