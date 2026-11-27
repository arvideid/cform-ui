/*
 * The route the browser holds.
 *
 * A hash is browser state and nothing else: no window on any desktop keeps one,
 * so this file exists for the one caller that runs in a tab and answers the
 * question the page cannot ask its own context - what is in the address bar.
 * Compiled only for Emscripten; everywhere else web_nav_none.c answers it and
 * links the same symbols, so a page that navigates needs no conditional of its
 * own.
 *
 * The JavaScript inside the macros below avoids the strict-equality operators on
 * purpose. clang-format reads an EM_ASM body as C, and `===` is three C tokens:
 * the formatter splits them with a space and the emitted script stops parsing.
 * Loose equality on two strings is the same test.
 */
#include <string.h>

#include <emscripten.h>

#include "cf/backend.h"

/*
 * The whole route after the `#`, written into the caller's buffer.
 *
 * The bytes are UTF-8 in both directions, so the run is measured in the
 * browser's own units rather than by `strlen`, which would stop at the first byte
 * of a name that is not ASCII. A route longer than the buffer is refused whole: a
 * page that read half an address and matched nothing would be a worse answer than
 * a page that says it was given nothing, and the caller already has a first page
 * to fall back on.
 */
bool cf_web_read_hash(char *out, uint32_t size) {
    if (out == NULL || size == 0u) {
        return false;
    }

    /* Positive for the bytes that landed, zero for an address with no route in
     * it, negative for the size a route would have needed and did not get. */
    const int landed = MAIN_THREAD_EM_ASM_INT(({
                                                  const route = window.location.hash.slice(1);

                                                  if (route.length == 0) {
                                                      return 0;
                                                  }

                                                  const needed = lengthBytesUTF8(route) + 1;

                                                  if (needed > $1) {
                                                      return -needed;
                                                  }

                                                  stringToUTF8(route, $0, $1);
                                                  return needed;
                                              }),
                                              out, (int)size);

    return landed > 0;
}

/*
 * Writes the route, so the address bar and a shared link follow the page the
 * reader is on. Assigning to `location.hash` is the whole call: the browser adds
 * no entry for a route that is already there, which is what keeps walking back
 * through the tab's history a walk through pages rather than through every click
 * on one nav row.
 */
void cf_web_write_hash(const char *route) {
    if (route == NULL) {
        return;
    }

    MAIN_THREAD_EM_ASM(({
                           const next = '#' + UTF8ToString($0);

                           if (window.location.hash != next) {
                               window.location.hash = next;
                           }
                       }),
                       route);
}

/*
 * Leaves the page for good. A link out of a canvas has no anchor element to be,
 * because the whole document is one drawing, so the only honest way to point at
 * something outside it is to ask the browser for a tab. Nothing waits for it and
 * nothing reports where it went: the tab belongs to the reader.
 */
void cf_web_open_url(const char *url) {
    if (url == NULL) {
        return;
    }

    MAIN_THREAD_EM_ASM({ window.open(UTF8ToString($0), "_blank"); }, url);
}
