/*
 * Native HTTP backend for the image loader: one libcurl easy handle per
 * pending download, each running on its own detached worker thread so the
 * render loop never waits on the network. The worker writes the body into its
 * slot and publishes the terminal state with SDL_SetAtomicInt, which is a
 * full barrier - a reader that sees UI_HTTP_READY also sees the bytes.
 */

#include <SDL3/SDL.h>
#include <curl/curl.h>

#include "http_loader.h"

#define DOWNLOAD_CAPACITY 8
#define URL_MAX 1024
#define START_BODY_BYTES ((size_t)64 * 1024)
#define MAX_BODY_BYTES ((size_t)16 * 1024 * 1024)
#define SHUTDOWN_TIMEOUT_MS 3000

struct UI_HttpDownload {
    char url[URL_MAX];        /* empty string marks the slot unused */
    SDL_AtomicInt status;     /* UI_HttpStatus */
    SDL_AtomicInt cancelled;  /* set to ask the worker to stop */
    unsigned char *data;      /* owned by the loader until shutdown */
    size_t len;
    size_t cap;
};

static struct UI_HttpDownload downloads[DOWNLOAD_CAPACITY];
static bool curl_initialized;

bool ui_http_is_url(const char *src) {
    if (src == NULL) {
        return false;
    }

    return SDL_strncasecmp(src, "http://", 7) == 0
        || SDL_strncasecmp(src, "https://", 8) == 0;
}

/* Appends a received chunk, growing the buffer geometrically. Returning a
 * short count makes libcurl abort the transfer, which is how size limits and
 * cancellation take effect mid-download. */
static size_t write_bytes(void *ptr, size_t size, size_t nmemb, void *userdata) {
    struct UI_HttpDownload *dl = userdata;
    size_t incoming = size * nmemb;
    size_t wanted = dl->len + incoming;
    size_t cap = dl->cap > 0 ? dl->cap : START_BODY_BYTES;

    if (incoming == 0) {
        return 0;
    }

    if (SDL_GetAtomicInt(&dl->cancelled) != 0 || wanted > MAX_BODY_BYTES) {
        if (wanted > MAX_BODY_BYTES) {
            /* Cast to unsigned long long: MinGW's printf rejects %zu. */
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "image download too large (%llu bytes, limit %llu): %s",
                (unsigned long long)wanted,
                (unsigned long long)MAX_BODY_BYTES, dl->url);
        }

        return 0;
    }

    while (cap < wanted) {
        cap *= 2;
    }

    if (cap > dl->cap) {
        unsigned char *grown = SDL_realloc(dl->data, cap);

        if (grown == NULL) {
            return 0;
        }

        dl->data = grown;
        dl->cap = cap;
    }

    SDL_memcpy(dl->data + dl->len, ptr, incoming);
    dl->len = wanted;

    return incoming;
}

static int progress_cb(
    void *userdata,
    curl_off_t dltotal,
    curl_off_t dlnow,
    curl_off_t ultotal,
    curl_off_t ulnow
) {
    /* SDL_GetAtomicInt takes a non-const pointer even though it only reads. */
    struct UI_HttpDownload *dl = userdata;

    (void)dltotal;
    (void)dlnow;
    (void)ultotal;
    (void)ulnow;

    return SDL_GetAtomicInt(&dl->cancelled) != 0 ? 1 : 0;
}

static void log_failure(const struct UI_HttpDownload *dl, CURLcode code) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
        "image download failed for %s: %s", dl->url, curl_easy_strerror(code));

    if (code == CURLE_SSL_CACERT || code == CURLE_SSL_CACERT_BADFILE
        || code == CURLE_PEER_FAILED_VERIFICATION) {
        const curl_version_info_data *info = curl_version_info(CURLVERSION_NOW);

        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "the TLS trust store could not be loaded; set CURL_CA_BUNDLE to a "
            "CA bundle file if the system default is missing");
        /* capath is the only trust location this struct exposes; the default
         * bundle file is a build-time constant reachable via CURL_CA_BUNDLE. */
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "curl was built with capath=%s",
            info->capath != NULL ? info->capath : "(none)");
    }
}

static int SDLCALL transfer(void *userdata) {
    struct UI_HttpDownload *dl = userdata;
    CURL *easy = curl_easy_init();
    CURLcode code;
    int status = UI_HTTP_FAILED;

    if (easy == NULL) {
        SDL_SetAtomicInt(&dl->status, UI_HTTP_FAILED);

        return 0;
    }

    curl_easy_setopt(easy, CURLOPT_URL, dl->url);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, write_bytes);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, dl);
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
    /* Treat 4xx/5xx as errors instead of handing an HTML error page to the
     * image decoder. */
    curl_easy_setopt(easy, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(easy, CURLOPT_TIMEOUT, 30L);
    /* Let curl advertise and apply gzip/br/zstd when the server offers them. */
    curl_easy_setopt(easy, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(easy, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(easy, CURLOPT_XFERINFOFUNCTION, progress_cb);
    curl_easy_setopt(easy, CURLOPT_XFERINFODATA, dl);
    /* Threaded transfers must not let curl install signal handlers. */
    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);

    code = curl_easy_perform(easy);
    curl_easy_cleanup(easy);

    if (code == CURLE_OK && dl->len > 0) {
        status = UI_HTTP_READY;
    } else if (code != CURLE_OK && SDL_GetAtomicInt(&dl->cancelled) == 0) {
        log_failure(dl, code);
    }

    /* Publishes the buffer written above; readers must see one to see the
     * other. */
    SDL_SetAtomicInt(&dl->status, status);

    return 0;
}

UI_HttpDownload *ui_http_start(const char *url) {
    if (url == NULL || url[0] == '\0') {
        return NULL;
    }

    if (!curl_initialized) {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "curl_global_init() failed; remote images are unavailable");

            return NULL;
        }

        curl_initialized = true;
    }

    for (int i = 0; i < DOWNLOAD_CAPACITY; i++) {
        if (SDL_strcasecmp(downloads[i].url, url) == 0) {
            return &downloads[i];
        }
    }

    for (int i = 0; i < DOWNLOAD_CAPACITY; i++) {
        struct UI_HttpDownload *dl = &downloads[i];
        SDL_Thread *thread;

        if (dl->url[0] != '\0') {
            continue;
        }

        SDL_snprintf(dl->url, sizeof dl->url, "%s", url);
        SDL_SetAtomicInt(&dl->cancelled, 0);
        SDL_SetAtomicInt(&dl->status, UI_HTTP_PENDING);

        thread = SDL_CreateThread(transfer, "cform_http", dl);

        if (thread == NULL) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "failed to start download thread: %s", SDL_GetError());
            dl->url[0] = '\0';
            SDL_SetAtomicInt(&dl->status, UI_HTTP_FAILED);

            return NULL;
        }

        /* The worker publishes its own terminal state, so nobody ever needs
         * to join it; the table is reclaimed by ui_http_shutdown(). */
        SDL_DetachThread(thread);

        return dl;
    }

    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
        "download table full (%d URLs); remote image ignored: %s",
        DOWNLOAD_CAPACITY, url);

    return NULL;
}

UI_HttpStatus ui_http_status(const UI_HttpDownload *download) {
    if (download == NULL) {
        return UI_HTTP_FAILED;
    }

    return (UI_HttpStatus)SDL_GetAtomicInt(
        &((struct UI_HttpDownload *)download)->status);
}

const unsigned char *ui_http_bytes(const UI_HttpDownload *download, size_t *out_len) {
    struct UI_HttpDownload *dl = (struct UI_HttpDownload *)download;

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
    Uint64 deadline = SDL_GetTicks() + SHUTDOWN_TIMEOUT_MS;
    bool waiting = true;

    for (int i = 0; i < DOWNLOAD_CAPACITY; i++) {
        if (downloads[i].url[0] != '\0') {
            SDL_SetAtomicInt(&downloads[i].cancelled, 1);
        }
    }

    /* Workers own their buffers, so nothing may be freed while one is still
     * appending. Cancellation makes them stop at the next chunk. */
    while (waiting && SDL_GetTicks() < deadline) {
        waiting = false;

        for (int i = 0; i < DOWNLOAD_CAPACITY; i++) {
            if (downloads[i].url[0] != '\0'
                && SDL_GetAtomicInt(&downloads[i].status) == UI_HTTP_PENDING) {
                waiting = true;
            }
        }

        if (waiting) {
            SDL_Delay(5);
        }
    }

    for (int i = 0; i < DOWNLOAD_CAPACITY; i++) {
        struct UI_HttpDownload *dl = &downloads[i];

        /* A zero-initialized slot also reads as UI_HTTP_PENDING, so only
         * slots that actually held a URL can be abandoned. */
        if (dl->url[0] != '\0'
            && SDL_GetAtomicInt(&dl->status) == UI_HTTP_PENDING) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "%s was still downloading when the renderer shut down; "
                "giving up after %d ms", dl->url, SHUTDOWN_TIMEOUT_MS);
        }

        SDL_free(dl->data);
        dl->data = NULL;
        dl->len = 0;
        dl->cap = 0;
        dl->url[0] = '\0';
    }

    if (curl_initialized) {
        curl_global_cleanup();
        curl_initialized = false;
    }
}
