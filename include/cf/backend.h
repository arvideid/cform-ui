#ifndef CF_BACKEND_H
#define CF_BACKEND_H

#include <stdbool.h>
#include <stdint.h>

#include "cf/event.h"
#include "cf/paint.h"
#include "cf/text.h"

/*
 * The one boundary a renderer crosses to draw a frame. `abi_version` names the
 * stream layout the backend reads and the event struct it writes, and
 * `cf_backend.size` names how big the struct was where the backend was compiled;
 * a caller checks both before the first submit, because data the backend misreads
 * draws wrong rather than failing. A number a porter can copy and a size only the
 * compiler knows catch different mistakes, which is why the struct carries both.
 */
#define CF_BACKEND_ABI_VERSION 9u

typedef struct cf_backend {
    uint32_t abi_version;
    /* Set this to `sizeof(cf_backend)` as your translation unit sees it. Slots
     * are appended, never reordered, so a struct of the wrong size is a library
     * built against another revision of this header whatever its version number
     * says. */
    uint32_t size;
    void *user_data;
    /* Hands the frame's stream over. The commands are borrowed for the call
     * alone - a backend that draws after it returns copies them first. */
    void (*submit)(void *user_data, const cf_command *commands, uint32_t count);
    /* Shows what the last submit carried, to the output the backend owns. A
     * shell presents every turn of its loop, so a call may follow one with no
     * submit between them, and keeping the output honest then is the
     * backend's. */
    void (*present)(void *user_data);
    /* Uploads RGBA8888 pixels, one byte per channel, row after row, as a
     * texture the stream's image commands name. The buffer has to hold
     * width*height*4 bytes, and both sides have to be real: zero, absent pixels
     * and a side no display can carry are refused rather than half-uploaded.
     * Zero names a texture the backend refused: nothing was uploaded, nothing
     * will draw. */
    uint32_t (*create_texture)(void *user_data, uint32_t width, uint32_t height,
                               const void *pixels);
    /* Moves one device event into `out` and answers whether it took one.
     * The queue belongs to whoever reads it every frame; a backend without
     * input leaves this NULL and its caller runs without events. */
    bool (*poll_event)(void *user_data, cf_event *out);
    /* The output's extent in logical points, read fresh each call so a
     * resize takes without a callback. A backend without an output leaves
     * this NULL and its caller lays out against nothing. */
    cf_size (*surface)(void *user_data);
    /* Reads an image file, uploads it, and answers the texture to draw and the
     * extent it holds in logical points. The path is the platform's to open and
     * the formats the backend decodes; a file that will not open, a format it
     * cannot read or a backend with no decoder answers false and leaves `out`
     * alone. Load once and keep the answer: an upload per frame is a texture per
     * frame, and the id the answer carries is the host's to retire below. */
    bool (*load_image)(void *user_data, const char *path, cf_image_content *out);
    /*
     * Retires a texture the stream no longer needs. The id is the host's to
     * track and the backend's to forget: after the call an image command that
     * still names it draws nothing, and a later upload may be handed the same
     * number. A host therefore releases a texture only once no frame in flight
     * can name it - a backend that defers its drawing past submit keeps the
     * bytes alive until it has drawn them, so releasing earlier would draw a
     * texture the host believes it handed back. Zero, and an id retired
     * already, ask for nothing and say so.
     */
    void (*release_texture)(void *user_data, uint32_t texture);
    /*
     * Frees what the backend took for itself: the uploads, the window, the
     * bytes beside them. The host calls it once, after the shell it was handed
     * to has returned, because the shell borrows the backend and leaves the
     * host's window standing. Nothing may be submitted through a destroyed
     * backend, and a backend built as a zero struct has nothing here to call.
     */
    void (*destroy)(void *user_data);
} cf_backend;

/*
 * The headless backend: it keeps the last submitted stream in its own arena
 * so a test reads what a device would have drawn. The store it hands back is
 * owned, so the stream survives the caller rewinding the arena the paint list
 * came out of. The zero state is an empty recorder. It takes no uploads and no
 * device state of its own, so the two resource slots stay NULL: a host that
 * releases through this backend has to ask the slot first, exactly as it does
 * for a third-party backend that reclaims nothing.
 */
typedef struct cf_recorder {
    cf_arena *arena;
    cf_command *commands;
    uint32_t count;
    uint32_t capacity;
} cf_recorder;

/* A backend that records instead of draws. A NULL recorder submits nothing. */
cf_backend cf_backend_recorder(cf_recorder *recorder);

/*
 * The renderer backend, built when CFORM_WITH_SDL_BACKEND is on: it opens
 * its own window, answers the shell's polls with the device's events, and
 * draws the stream through SDL's GPU-accelerated renderer. A frame is
 * whatever the last submit drew, cleared to black where nothing was painted.
 * Rects arrive in logical points; the window's pixel density is the
 * backend's to apply.
 *
 * The three SDL-shaped names below (cf_backend_sdl, cf_sdl_install_log_sink
 * and cf_font_sdl) belong to the source tree. cmake --install packages the
 * headless backend only, because the SDL target's interface names SDL3::SDL3,
 * a FetchContent artifact that lives inside a build tree and nowhere else.
 * A consumer taking the installed package either builds cform with
 * CFORM_WITH_SDL_BACKEND=ON and links cf_backend_sdl from the same tree, or
 * writes its own backend against the cf_backend struct above. The
 * declarations stay here so the examples do not need a second header; an
 * installed consumer who tries to call them finds the symbol missing at the
 * linker, not at the compiler.
 */
typedef struct cf_sdl_config {
    const char *title;
    uint32_t width;
    uint32_t height;
    /* The smallest window the drag allows, in the same logical points as the
     * start size, so a page is never asked to lay out in less room than its
     * own declarations can fit. Zero leaves the window unbounded. */
    uint32_t min_width;
    uint32_t min_height;
} cf_sdl_config;

/* A backend drawing to a new window. A window that cannot open is a backend
 * that draws nothing, reported once on CF_LOG_BACKEND. */
cf_backend cf_backend_sdl(const cf_sdl_config *config);

/*
 * Route the library's diagnostics through SDL's log channel. A host that
 * owns its sink does not call this; a host that has none and wants the
 * backend's words to reach the platform's viewer does. Installing is not
 * automatic on `cf_backend_sdl` because the backend has no way to know
 * whether the caller already put a sink in place, and overwriting a
 * game engine's debug overlay with SDL_Log is exactly what a host reaching
 * for a pluggable backend wants to avoid.
 */
void cf_sdl_install_log_sink(void);

/*
 * The SDL font table over a TrueType or OpenType file, built when
 * CFORM_WITH_SDL_BACKEND is on. Measuring never rasterizes: the file is
 * read for its metrics alone, and the pen distances it answers are the ones
 * cf_text walks read. A font lives as long as its owner - the theme that
 * resolves it - does, and one that will not open measures nothing,
 * reported once on CF_LOG_BACKEND.
 */
cf_font cf_font_sdl(const char *file_path, float point_size);

/*
 * Hands back what cf_font_sdl took: the font file's metrics and the platform
 * reference the constructor held beside them. The font is the caller's, so
 * nothing above the theme asks for this; a theme that keeps a font keeps it
 * for the run. After the call the struct is the zero one, which measures and
 * draws nothing, so a page that still names it shows blanks instead of reading
 * through bytes that are no longer a font - and a second call on it is
 * nothing.
 *
 * Close the fonts before destroying the backend they were drawn with: a font
 * belongs to the platform, and the teardown may be the thing that stops it.
 */
void cf_font_sdl_close(cf_font *font);

#endif
