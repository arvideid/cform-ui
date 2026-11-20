/*
 * Browser backend for the image loader. emscripten_fetch() in its default
 * asynchronous mode returns immediately and invokes the completion callback
 * when the browser regains control - which happens every frame because SDL's
 * Emscripten timer drives SDL_Delay through emscripten_sleep. Waitable fetch
 * handles are no longer implemented upstream, so callbacks are the only
 * non-blocking option here.
 *
 * The body lives in the fetch handle and is freed by emscripten_fetch_close(),
 * so the callback copies it into a loader-owned buffer before closing.
 *
 * Remote URLs must satisfy CORS: the server has to send
 * Access-Control-Allow-Origin, or the request fails with a network error.
 */

#include <SDL3/SDL.h>
#include <emscripten/emscripten.h>
#include <emscripten/fetch.h>

#include "http_loader.h"

#define DOWNLOAD_CAPACITY 8
#define URL_MAX 1024
#define MAX_BODY_BYTES ((size_t)16 * 1024 * 1024)
#define DOWNLOAD_TIMEOUT_MS 30000

struct UI_HttpDownload {
    char url[URL_MAX];      /* empty string marks the slot unused */
    volatile int status;    /* UI_HttpStatus, written by the callback */
    unsigned char *data;
    size_t len;
};

static struct UI_HttpDownload downloads[DOWNLOAD_CAPACITY];

static struct UI_HttpDownload *slot_of(const emscripten_fetch_t *fetch) {
    return (struct UI_HttpDownload *)fetch->userData;
}

static void on_success(emscripten_fetch_t *fetch) {
    struct UI_HttpDownload *dl = slot_of(fetch);
    size_t len = (size_t)fetch->numBytes;

    if (fetch->status < 200 || fetch->status > 299) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "image download failed for %s: HTTP %u %s",
            dl->url, (unsigned)fetch->status, fetch->statusText);
        dl->status = UI_HTTP_FAILED;
    } else if (len == 0 || len > MAX_BODY_BYTES) {
        /* Cast to unsigned long long: MinGW's printf rejects %zu. */
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "image download for %s is %llu bytes; limit is %llu",
            dl->url, (unsigned long long)len,
            (unsigned long long)MAX_BODY_BYTES);
        dl->status = UI_HTTP_FAILED;
    } else {
        dl->data = SDL_malloc(len);

        if (dl->data == NULL) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "out of memory downloading %s (%llu bytes)",
                dl->url, (unsigned long long)len);
            dl->status = UI_HTTP_FAILED;
        } else {
            SDL_memcpy(dl->data, fetch->data, len);
            dl->len = len;
            dl->status = UI_HTTP_READY;
        }
    }

    emscripten_fetch_close(fetch);
}

static void on_error(emscripten_fetch_t *fetch) {
    struct UI_HttpDownload *dl = slot_of(fetch);

    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
        "image download failed for %s: HTTP %u %s (a CORS or DNS failure "
        "reports 0)",
        dl->url, (unsigned)fetch->status, fetch->statusText);

    dl->status = UI_HTTP_FAILED;
    emscripten_fetch_close(fetch);
}

UI_HttpDownload *ui_http_start(const char *url) {
    if (url == NULL || url[0] == '\0') {
        return NULL;
    }

    for (int i = 0; i < DOWNLOAD_CAPACITY; i++) {
        if (SDL_strcasecmp(downloads[i].url, url) == 0) {
            return &downloads[i];
        }
    }

    for (int i = 0; i < DOWNLOAD_CAPACITY; i++) {
        struct UI_HttpDownload *dl = &downloads[i];
        emscripten_fetch_attr_t attr;
        emscripten_fetch_t *fetch;

        if (dl->url[0] != '\0') {
            continue;
        }

        SDL_snprintf(dl->url, sizeof dl->url, "%s", url);
        dl->status = UI_HTTP_PENDING;
        dl->data = NULL;
        dl->len = 0;

        emscripten_fetch_attr_init(&attr);
        SDL_strlcpy(attr.requestMethod, "GET", sizeof attr.requestMethod);
        attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
        attr.onsuccess = on_success;
        attr.onerror = on_error;
        attr.timeoutMSecs = DOWNLOAD_TIMEOUT_MS;
        attr.userData = dl;

        fetch = emscripten_fetch(&attr, dl->url);

        if (fetch == NULL) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "could not start download for %s", dl->url);
            dl->status = UI_HTTP_FAILED;

            return dl;
        }

        /* Non-blocking mode: on_success/on_error finish the transfer. */
        return dl;
    }

    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
        "download table full (%d URLs); remote image ignored: %s",
        DOWNLOAD_CAPACITY, url);

    return NULL;
}

UI_HttpStatus ui_http_status(const UI_HttpDownload *download) {
    const struct UI_HttpDownload *dl = download;

    if (dl == NULL) {
        return UI_HTTP_FAILED;
    }

    return (UI_HttpStatus)dl->status;
}

const unsigned char *ui_http_bytes(const UI_HttpDownload *download, size_t *out_len) {
    const struct UI_HttpDownload *dl = download;

    if (out_len != NULL) {
        *out_len = 0;
    }

    if (dl == NULL || ui_http_status(download) != UI_HTTP_READY) {
        return NULL;
    }

    if (out_len != NULL) {
        *out_len = dl->len;
    }

    return dl->data;
}

void ui_http_shutdown(void) {
    for (int i = 0; i < DOWNLOAD_CAPACITY; i++) {
        struct UI_HttpDownload *dl = &downloads[i];

        /* A transfer that is still running will call back into this static
         * table after we return. Freeing its slot would race with that, so
         * finished downloads are released and in-flight ones are left for the
         * runtime to clean up when the page unloads. */
        if (dl->url[0] != '\0' && dl->status != UI_HTTP_PENDING) {
            SDL_free(dl->data);
            dl->data = NULL;
            dl->len = 0;
            dl->url[0] = '\0';
        }
    }
}
