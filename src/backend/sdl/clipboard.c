#include <string.h>

#include <SDL3/SDL.h>

#include "cf/backend.h"

/*
 * The desk's clipboard in the pair a field reaches for.
 *
 * Nothing is kept between the calls: SDL reads the clipboard into bytes it
 * allocated and hands them over, so `read` copies what fits into the caller's
 * buffer and gives the rest straight back - the one allocation in this file that is
 * not the library's, and the one it therefore returns to SDL. That is why asking for
 * the length alone costs one read rather than needing somewhere to remember the last
 * answer, and why the pair carries no state of its own.
 */
static cf_string clipboard_read(void *user_data, char *out, uint32_t capacity) {
    (void)user_data;

    char *text = SDL_GetClipboardText();

    if (text == NULL) {
        /* Nothing to read, or the desk refused: both are the same answer to a
         * widget, which is that the bytes stayed where they were. */
        return (cf_string){0};
    }

    const uint32_t length = (uint32_t)strlen(text);
    cf_string answer = {.chars = NULL, .length = length};

    if (out != NULL && capacity > 0u) {
        uint32_t taken = length < capacity ? length : capacity;

        /* Back off to the edge of a character. SDL hands over UTF-8, so a byte that
         * does not continue a scalar is one that starts it, and a run cut there
         * leaves no half a reader would have to decode. */
        while (taken > 0u && ((unsigned char)text[taken] & 0xC0u) == 0x80u) {
            taken--;
        }

        memcpy(out, text, taken);
        answer = (cf_string){.chars = out, .length = taken};
    }

    SDL_free(text);
    return answer;
}

static bool clipboard_write(void *user_data, cf_string utf8) {
    (void)user_data;

    if (utf8.chars == NULL || utf8.length == 0u) {
        return false;
    }

    /* The desk wants a terminated string and a widget hands over a run, so the
     * bytes are copied once into a block this side owns and handed over whole. The
     * block comes from the library's own allocator: a buffer the framework makes is
     * memory the framework tracks, whatever it is temporarily carried to. */
    char *copy = cf_alloc_block(utf8.length + 1u);

    if (copy == NULL) {
        return false;
    }

    memcpy(copy, utf8.chars, utf8.length);
    copy[utf8.length] = '\0';

    const bool taken = SDL_SetClipboardText(copy);

    cf_free_block(copy);
    return taken;
}

/*
 * The desktop clipboard as a pair to put on a context or in an app config. It takes
 * no handle, because the desk has one per process. A clipboard the platform will
 * not open reads as empty and writes as refused, which is what the pair's false
 * answers are for; nothing here decides that a page should say so.
 */
cf_clipboard cf_clipboard_sdl(void) {
    return (cf_clipboard){.read = clipboard_read, .write = clipboard_write, .user_data = NULL};
}
