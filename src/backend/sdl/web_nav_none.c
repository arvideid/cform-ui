/*
 * The route, on a platform that has no address bar.
 *
 * A window is not a URL: nothing outside the process can be pointed at the page
 * a native build is showing, so there is nothing to read and nothing to write.
 * The two answers stay in the link anyway, because the alternative is a
 * preprocessor branch in the page - and the page is the code a custom host
 * copies, so it should read as one program on every platform. A caller that
 * gets false from the read falls back to the first thing it has to show.
 */
#include "cf/backend.h"

bool cf_web_read_hash(char *out, uint32_t size) {
    (void)out;
    (void)size;

    return false;
}

void cf_web_write_hash(const char *route) {
    (void)route;
}

/* A window has no tab to open and no browser to open it in. The page that asks
 * has nowhere to send a reader, which is the same answer a host with no network
 * gives a link: nothing happens, and nothing pretends otherwise. */
void cf_web_open_url(const char *url) {
    (void)url;
}
