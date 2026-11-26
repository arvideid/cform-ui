#include <SDL3/SDL.h>
#include <string.h>

#include "cf/backend.h"
#include "cf/event.h"
#include "internal.h"

/*
 * The quarter arc of a corner is walked in this many straight pieces: enough
 * that a UI radius reads round at window scale, few enough that a frame's
 * vertices stay in the low dozens.
 */
#define ARC_SEGMENTS 6u
#define PERIMETER_POINTS (4u * (ARC_SEGMENTS + 1u))

/*
 * How long a dry queue sleeps before answering that nothing came. The wait
 * wakes on the first event, so the number only bounds what a lost wakeup can
 * cost: one frame is short enough that nothing on screen looks stuck on it.
 */
#define IDLE_WAIT_MS 16

typedef struct sdl_state {
    SDL_Window *window;
    SDL_Renderer *renderer;
    /* The uploads the stream's image commands name, by id minus one. A slot
     * holds a texture until the host retires it and NULL afterwards, so a
     * retired id draws nothing and the next upload is handed the number back.
     * The table lives as long as the window does, outside every frame arena. */
    SDL_Texture **textures;
    uint32_t texture_slots;
    uint32_t texture_capacity;
    /* A submit has drawn into the target and no swap has shown it yet. */
    bool damaged;
    /* Events have been handed to the shell that no drawn frame carries yet. */
    bool pending;
    /* The scissor the target is under, and whether one is on at all. Held
     * between the commands of a submit because a stream that scrolls repeats
     * one clip down a whole column of boxes, and the state call is not free. */
    SDL_Rect clip;
    bool clip_set;
    /* This backend asked for the video subsystem rather than found it already
     * there, so stopping it on the way out is this backend's duty and not a
     * host's surprise. */
    bool owns_platform;
    /* A present that the output refused. The window is then showing a frame older
     * than every one after it, and nothing here can put it right: drawing on costs
     * a failing call per shape, so the backend stops drawing and stops taking
     * uploads and leaves the recovery - close this backend, open another - to the
     * host that owns the device. */
    bool output_lost;
} sdl_state;

/*
 * One walk around a rounded rect, corner by corner in draw order. Every corner
 * is clamped to its rect - a radius past half the shorter extent would curl
 * the edges past each other - and a zero radius is a square corner whose arc
 * points all sit on it.
 */
static void rounded_perimeter(SDL_FRect rect, const float radii[4], SDL_FPoint *points) {
    const float half = (rect.w < rect.h ? rect.w : rect.h) * 0.5f;
    const SDL_FPoint centers[4] = {
        {rect.x + radii[0], rect.y + radii[0]},
        {rect.x + rect.w - radii[1], rect.y + radii[1]},
        {rect.x + rect.w - radii[2], rect.y + rect.h - radii[2]},
        {rect.x + radii[3], rect.y + rect.h - radii[3]},
    };
    const float starts[4] = {180.0f, 270.0f, 0.0f, 90.0f};

    uint32_t at = 0u;

    for (uint32_t corner = 0u; corner < 4u; ++corner) {
        const float radius =
            radii[corner] < 0.0f ? 0.0f : (radii[corner] > half ? half : radii[corner]);

        for (uint32_t step = 0u; step <= ARC_SEGMENTS; ++step) {
            const float angle =
                (starts[corner] + 90.0f * (float)step / (float)ARC_SEGMENTS) * SDL_PI_F / 180.0f;
            points[at].x = centers[corner].x + radius * SDL_cosf(angle);
            points[at].y = centers[corner].y + radius * SDL_sinf(angle);
            ++at;
        }
    }
}

/* A rounded fill is one triangle fan from the center over the perimeter. */
static void draw_rounded_fill(SDL_Renderer *renderer, SDL_FRect rect, float radius,
                              SDL_FColor color) {
    const float radii[4] = {radius, radius, radius, radius};
    SDL_FPoint perimeter[PERIMETER_POINTS];
    rounded_perimeter(rect, radii, perimeter);

    SDL_Vertex vertices[PERIMETER_POINTS + 1u];
    vertices[0].position = (SDL_FPoint){rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f};
    vertices[0].color = color;
    vertices[0].tex_coord = (SDL_FPoint){0.0f, 0.0f};

    for (uint32_t i = 0u; i < PERIMETER_POINTS; ++i) {
        vertices[i + 1u].position = perimeter[i];
        vertices[i + 1u].color = color;
        vertices[i + 1u].tex_coord = (SDL_FPoint){0.0f, 0.0f};
    }

    int indices[PERIMETER_POINTS * 3u];

    for (uint32_t i = 0u; i < PERIMETER_POINTS; ++i) {
        const uint32_t at = i * 3u;
        indices[at] = 0;
        indices[at + 1u] = (int)(i + 1u);
        indices[at + 2u] = (int)(1u + (i + 1u) % PERIMETER_POINTS);
    }

    SDL_RenderGeometry(renderer, NULL, vertices, (int)(PERIMETER_POINTS + 1u), indices,
                       (int)(PERIMETER_POINTS * 3u));
}

/*
 * The stroke between the box and the box inset by its widths: two perimeters
 * built with the same corner parametrization, joined into quads. A side with
 * no width leaves its band of the strip empty, and an inner corner gives way
 * to the wider of the two sides it joins, so uneven widths stay inside the
 * outer arc.
 */
static void draw_border(SDL_Renderer *renderer, SDL_FRect rect, float radius, cf_edges widths,
                        SDL_FColor color) {
    const SDL_FRect inner = {rect.x + widths.left, rect.y + widths.top,
                             rect.w - widths.left - widths.right,
                             rect.h - widths.top - widths.bottom};

    if (inner.w <= 0.0f || inner.h <= 0.0f) {
        return;
    }

    SDL_FPoint outer[PERIMETER_POINTS];
    SDL_FPoint inset[PERIMETER_POINTS];

    const float outer_radii[4] = {radius, radius, radius, radius};
    rounded_perimeter(rect, outer_radii, outer);

    const float inset_radii[4] = {
        radius - (widths.top > widths.left ? widths.top : widths.left),
        radius - (widths.top > widths.right ? widths.top : widths.right),
        radius - (widths.right > widths.bottom ? widths.right : widths.bottom),
        radius - (widths.bottom > widths.left ? widths.bottom : widths.left),
    };
    rounded_perimeter(inner, inset_radii, inset);

    SDL_Vertex vertices[PERIMETER_POINTS * 2u];

    for (uint32_t i = 0u; i < PERIMETER_POINTS; ++i) {
        vertices[i].position = outer[i];
        vertices[i].color = color;
        vertices[i].tex_coord = (SDL_FPoint){0.0f, 0.0f};
        vertices[PERIMETER_POINTS + i].position = inset[i];
        vertices[PERIMETER_POINTS + i].color = color;
        vertices[PERIMETER_POINTS + i].tex_coord = (SDL_FPoint){0.0f, 0.0f};
    }

    int indices[PERIMETER_POINTS * 6u];

    for (uint32_t i = 0u; i < PERIMETER_POINTS; ++i) {
        const uint32_t next = (i + 1u) % PERIMETER_POINTS;
        const uint32_t at = i * 6u;
        indices[at] = (int)i;
        indices[at + 1u] = (int)next;
        indices[at + 2u] = (int)(PERIMETER_POINTS + next);
        indices[at + 3u] = (int)i;
        indices[at + 4u] = (int)(PERIMETER_POINTS + next);
        indices[at + 5u] = (int)(PERIMETER_POINTS + i);
    }

    SDL_RenderGeometry(renderer, NULL, vertices, (int)(PERIMETER_POINTS * 2u), indices,
                       (int)(PERIMETER_POINTS * 6u));
}

/*
 * The scissor a command's clip asks for. A clip with no extent is the whole
 * output, which SDL draws when handed NULL; the boxes a scroll cuts in half
 * are the ones that need the region, and without it their borders and text
 * lines paint the part that is off the pane.
 */
static void apply_clip(sdl_state *state, cf_rect clip, float scale) {
    if (clip.width <= 0.0f || clip.height <= 0.0f) {
        if (state->clip_set) {
            SDL_SetRenderClipRect(state->renderer, NULL);
            state->clip_set = false;
        }

        return;
    }

    const SDL_Rect rect = {.x = (int)(clip.x * scale + 0.5f),
                           .y = (int)(clip.y * scale + 0.5f),
                           .w = (int)(clip.width * scale + 0.5f),
                           .h = (int)(clip.height * scale + 0.5f)};

    if (state->clip_set && rect.x == state->clip.x && rect.y == state->clip.y &&
        rect.w == state->clip.w && rect.h == state->clip.h) {
        return;
    }

    SDL_SetRenderClipRect(state->renderer, &rect);
    state->clip = rect;
    state->clip_set = true;
}

static void draw_command(sdl_state *state, const cf_command *command, float scale) {
    const SDL_FRect rect = {command->rect.x * scale, command->rect.y * scale,
                            command->rect.width * scale, command->rect.height * scale};
    const float radius = command->radius * scale;
    const SDL_FColor color = {
        (float)command->color.red / 255.0f, (float)command->color.green / 255.0f,
        (float)command->color.blue / 255.0f, (float)command->color.alpha / 255.0f};

    apply_clip(state, command->clip, scale);

    if (command->kind == CF_COMMAND_FILL) {
        if (radius > 0.0f) {
            draw_rounded_fill(state->renderer, rect, radius, color);
        } else {
            SDL_SetRenderDrawColorFloat(state->renderer, color.r, color.g, color.b, color.a);
            SDL_RenderFillRect(state->renderer, &rect);
        }
        return;
    }

    if (command->kind == CF_COMMAND_BORDER) {
        draw_border(state->renderer, rect, radius,
                    (cf_edges){command->widths.left * scale, command->widths.top * scale,
                               command->widths.right * scale, command->widths.bottom * scale},
                    color);
        return;
    }

    if (command->kind == CF_COMMAND_TEXT) {
        sdl_draw_text(state->renderer, command, scale);
        return;
    }

    if (command->kind == CF_COMMAND_IMAGE) {
        const uint32_t id = command->texture;

        /* An id outside the table, or one in a slot the host has handed back,
         * names nothing to draw: the rect is left as the frame had it rather
         * than read through a pointer that is no longer there. */
        if (id != 0u && id <= state->texture_slots && state->textures[id - 1u] != NULL) {
            SDL_RenderTexture(state->renderer, state->textures[id - 1u], NULL, &rect);
        }
    }
}

/* Room for more slots, doubling from eight. The base allocator zeroes what a
 * growth adds, which is what makes an untouched slot read as free. */
static bool grow_textures(sdl_state *state) {
    const uint32_t capacity = state->texture_capacity == 0u ? 8u : state->texture_capacity * 2u;
    const size_t held = (size_t)state->texture_capacity * sizeof *state->textures;
    const size_t wanted = (size_t)capacity * sizeof(SDL_Texture *);
    SDL_Texture **textures = cf_resize_block(state->textures, held, wanted);

    if (textures == NULL) {
        return false;
    }

    state->textures = textures;
    state->texture_capacity = capacity;

    return true;
}

/* RGBA8888, one byte per channel, row after row - the layout create_texture
 * promises. SDL's own RGBA8888 is that as a 32-bit value with red in the high
 * bits, which on a little-endian machine is a buffer whose first byte is
 * alpha: RGBA32 is the alias that follows the bytes the upload hands over. */

/* The longest side an upload may claim. The row stride is handed to SDL as an int
 * and the multiply that makes it is 32-bit, so a width past a quarter of that
 * range wraps into a stride narrower than the row it describes - and the copy then
 * walks out of the caller's buffer instead of refusing the ask. A device could not
 * carry the texture either. 16384 is above any window this backend opens and above
 * any image a page draws at its own size, so a real upload never meets the cap. */
#define TEXTURE_MAX_SIDE 16384u

uint32_t sdl_create_texture(void *user_data, uint32_t width, uint32_t height, const void *pixels) {
    sdl_state *state = user_data;

    if (state == NULL || width == 0u || height == 0u || pixels == NULL) {
        return 0u;
    }

    /* Bytes behind a texture that can never be drawn are bytes the host has to
     * chase later, and a refusal is an answer the image widget already handles. */
    if (state->output_lost) {
        return 0u;
    }

    /* Logged, unlike the asks a caller can see it making: the size an image file
     * carries is not something the page chose, and a silent refusal there reads as
     * a broken decoder. */
    if (width > TEXTURE_MAX_SIDE || height > TEXTURE_MAX_SIDE) {
        cf_logf(CF_LOG_BACKEND, "backend: a %ux%u upload is past the %u side this device holds",
                width, height, TEXTURE_MAX_SIDE);
        return 0u;
    }

    /* The lowest retired slot is the next id handed out, so a page that loads
     * and drops an image a thousand times holds one texture rather than a
     * thousand and no number outlives the bytes behind it. */
    uint32_t at = 0u;

    while (at < state->texture_slots && state->textures[at] != NULL) {
        ++at;
    }

    if (at == state->texture_slots && at == state->texture_capacity && !grow_textures(state)) {
        return 0u;
    }

    SDL_Texture *texture = SDL_CreateTexture(state->renderer, SDL_PIXELFORMAT_RGBA32,
                                             SDL_TEXTUREACCESS_STATIC, (int)width, (int)height);

    if (texture == NULL) {
        cf_logf(CF_LOG_BACKEND, "backend: no texture: %s", SDL_GetError());
        return 0u;
    }

    if (!SDL_UpdateTexture(texture, NULL, pixels, (int)(width * 4u))) {
        SDL_DestroyTexture(texture);
        cf_logf(CF_LOG_BACKEND, "backend: texture pixels did not upload: %s", SDL_GetError());
        return 0u;
    }

    state->textures[at] = texture;

    if (at == state->texture_slots) {
        state->texture_slots = at + 1u;
    }

    return at + 1u;
}

/* The host's ask to forget an upload. An id outside the table and one already
 * retired are the same nothing: the call has no answer to give, so a host that
 * releases a texture twice, or after the window took it back, is not a host
 * that has to check first. */
static void sdl_release_texture(void *user_data, uint32_t texture) {
    sdl_state *state = user_data;

    if (state == NULL || texture == 0u || texture > state->texture_slots) {
        return;
    }

    SDL_Texture *held = state->textures[texture - 1u];

    if (held != NULL) {
        SDL_DestroyTexture(held);
        state->textures[texture - 1u] = NULL;
    }
}

/* Everything the backend took for itself, in the order it was taken. The
 * window was the first thing opened and is the last thing closed, because a
 * renderer or a texture destroyed under a live window is fine while a window
 * that outlives its renderer is not. */
static void sdl_destroy(void *user_data) {
    sdl_state *state = user_data;

    if (state == NULL) {
        return;
    }

    for (uint32_t at = 0u; at < state->texture_slots; ++at) {
        if (state->textures != NULL && state->textures[at] != NULL) {
            SDL_DestroyTexture(state->textures[at]);
        }
    }

    cf_free_block(state->textures);
    SDL_StopTextInput(state->window);
    SDL_DestroyRenderer(state->renderer);
    SDL_DestroyWindow(state->window);
    /* Only the backend that started the platform may stop it: a host that had
     * SDL up before this window - an engine drawing its own frame beside it -
     * would lose its subsystems to a teardown it never asked for. */
    if (state->owns_platform) {
        SDL_Quit();
    }
    cf_free_block(state);
}

/* The density the window is drawing at, so an extent measured in device pixels
 * can be handed back in the points the layout places. */
float sdl_pixel_density(void *user_data) {
    const sdl_state *state = user_data;

    if (state == NULL) {
        return 1.0f;
    }

    const float density = SDL_GetWindowPixelDensity(state->window);

    return density > 0.0f ? density : 1.0f;
}

static void draw_submit(void *user_data, const cf_command *commands, uint32_t count) {
    sdl_state *state = user_data;

    if (state == NULL) {
        return;
    }

    /* The message is the failed present's to give, once. Here the ask is simply
     * dropped: the stream would fail shape by shape against an output that took
     * no frame, and the screen would keep being shown the last one it took. */
    if (state->output_lost) {
        return;
    }

    /* From here the target holds a frame the output has not shown, and the
     * events that produced it are inside it: the swap may run, and the poll
     * may sleep again. */
    state->damaged = true;
    state->pending = false;

    /* The frame starts from black: what the stream does not cover is the
     * window's own color, not the last frame's leftovers. */
    SDL_SetRenderDrawColorFloat(state->renderer, 0.0f, 0.0f, 0.0f, 1.0f);
    SDL_RenderClear(state->renderer);

    /* A resize can hand the renderer a new target with the state that went
     * with it forgotten, so the cached scissor is not trusted across it. */
    state->clip_set = false;

    const float scale = SDL_GetWindowPixelDensity(state->window);

    if (commands == NULL) {
        return;
    }

    for (uint32_t i = 0u; i < count; ++i) {
        draw_command(state, &commands[i], scale);
    }
}

/*
 * A device's code for a key into the number the tree reads. One it has no word
 * for keeps its own code: no widget above this file reads that, and inventing a
 * number for it would be worse than passing the platform's through. A Latin letter
 * is the case where passing it through is the answer - SDL's code for one is its
 * ASCII code, and event.h names that number the shortcut keys read.
 */
static uint32_t named_key(SDL_Keycode key) {
    switch (key) {
    case SDLK_BACKSPACE:
        return (uint32_t)CF_KEY_BACKSPACE;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        return (uint32_t)CF_KEY_ENTER;
    case SDLK_LEFT:
        return (uint32_t)CF_KEY_LEFT;
    case SDLK_RIGHT:
        return (uint32_t)CF_KEY_RIGHT;
    case SDLK_UP:
        return (uint32_t)CF_KEY_UP;
    case SDLK_DOWN:
        return (uint32_t)CF_KEY_DOWN;
    case SDLK_HOME:
        return (uint32_t)CF_KEY_HOME;
    case SDLK_END:
        return (uint32_t)CF_KEY_END;
    case SDLK_DELETE:
        return (uint32_t)CF_KEY_DELETE;
    case SDLK_ESCAPE:
        return (uint32_t)CF_KEY_ESCAPE;
    case SDLK_TAB:
        return (uint32_t)CF_KEY_TAB;
    case SDLK_SPACE:
        /* The bar is a control's ask and a field's character at once: the key
         * event names the ask, and SDL's text event still carries the space. */
        return (uint32_t)CF_KEY_SPACE;
    case SDLK_F12:
        return (uint32_t)CF_KEY_F12;
    default:
        return (uint32_t)key;
    }
}

/*
 * The held modifier keys as the bits the event carries. SDL's GUI is the
 * platform's windows key, its Command key or its Super key - one word here.
 */
static uint32_t named_mods(SDL_Keymod mod) {
    uint32_t mods = (uint32_t)CF_MOD_NONE;

    if ((mod & SDL_KMOD_SHIFT) != 0u) {
        mods |= (uint32_t)CF_MOD_SHIFT;
    }

    if ((mod & SDL_KMOD_CTRL) != 0u) {
        mods |= (uint32_t)CF_MOD_CONTROL;
    }

    if ((mod & SDL_KMOD_ALT) != 0u) {
        mods |= (uint32_t)CF_MOD_ALT;
    }

    if ((mod & SDL_KMOD_GUI) != 0u) {
        mods |= (uint32_t)CF_MOD_META;
    }

    return mods;
}

/*
 * One device event into the vocabulary the dispatch walks, false when the event
 * has no word the tree owns. What the keyboard wrote is carried as the text the
 * platform settled on, not as the key that produced it: shift, caps lock, the
 * layout and an input method are decided above this file. The key events that
 * survive are the ones that name no character - editing, navigation, the
 * function keys - and arrive under the numbers cf_event names, so a widget reads
 * the same word here that it writes in its own source.
 */
static bool take_event(const SDL_Event *event, cf_event *out) {
    switch (event->type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        *out = (cf_event){.type = CF_EVENT_QUIT};
        return true;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        /* Whichever window takes the hand is the one that will be told about the
         * release, so a drag this window was holding ends here rather than wait for
         * an event that cannot arrive any more. */
        *out = (cf_event){.type = CF_EVENT_POINTER_CANCEL};
        return true;
    case SDL_EVENT_MOUSE_MOTION:
        *out = (cf_event){.type = CF_EVENT_POINTER_MOVE,
                          .position = {event->motion.x, event->motion.y}};
        return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        *out = (cf_event){.type = event->type == SDL_EVENT_MOUSE_BUTTON_DOWN ? CF_EVENT_POINTER_DOWN
                                                                             : CF_EVENT_POINTER_UP,
                          .position = {event->button.x, event->button.y},
                          .button = event->button.button};
        return true;
    case SDL_EVENT_MOUSE_WHEEL:
        /* The device counts a notch away from the hand as positive, which is
         * the way back up a page: the offset the widgets carry grows the other
         * way, so the vertical axis is turned around. */
        *out = (cf_event){.type = CF_EVENT_SCROLL,
                          .position = {event->wheel.mouse_x, event->wheel.mouse_y},
                          .wheel = {event->wheel.x, -event->wheel.y}};
        return true;
    case SDL_EVENT_TEXT_INPUT: {
        /* SDL hands back one already-terminated run of UTF-8 and drops it on
         * the next event, so the bytes are good for this dispatch alone - the
         * lifetime the event's contract states and a field copies under. */
        const char *const run = event->text.text;
        *out = (cf_event){.type = CF_EVENT_TEXT,
                          .text = {.chars = run, .length = (uint32_t)strlen(run)}};
        return true;
    }
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        *out = (cf_event){.type = event->type == SDL_EVENT_KEY_DOWN ? CF_EVENT_KEY_DOWN
                                                                    : CF_EVENT_KEY_UP,
                          .key = named_key(event->key.key),
                          .mods = named_mods(event->key.mod)};
        return true;
    default:
        return false;
    }
}

/*
 * The queue is the app shell's to read: one event per call, false when it runs
 * dry. Events the tree has no word for are consumed here and answer nothing.
 */
static bool sdl_poll(void *user_data, cf_event *out) {
    sdl_state *state = user_data;
    SDL_Event event;
    bool waited = false;

    if (state == NULL) {
        return false;
    }

    for (;;) {
        while (SDL_PollEvent(&event)) {
            if (take_event(&event, out)) {
                state->pending = true;
                return true;
            }
        }

        /* A shell holding events it has not drawn yet must not have its frame
         * parked behind a wait, and one wait per call keeps a stream of events
         * nobody translates from sleeping here forever. */
        if (state->pending || waited || !SDL_WaitEventTimeout(NULL, IDLE_WAIT_MS)) {
            return false;
        }

        waited = true;
    }
}

/* The window's own size, which is what logical points lay out against; the
 * pixel density between points and device pixels stays inside the draw. */
static cf_size sdl_surface(void *user_data) {
    sdl_state *state = user_data;
    int width = 0;
    int height = 0;

    if (state != NULL) {
        SDL_GetWindowSize(state->window, &width, &height);
    }

    return (cf_size){(float)width, (float)height};
}

static void sdl_present(void *user_data) {
    sdl_state *state = user_data;

    if (state == NULL) {
        return;
    }

    /* The pump keeps the desktop answering even in a run that never reads
     * events; what it collects waits for the reader, where a flush would
     * drop the presses and moves a shell has not reached yet. */
    SDL_PumpEvents();

    /*
     * The shell presents every turn of its loop, including the turns in which
     * it drew nothing. A discard-model swapchain hands over an undefined
     * buffer when it is told to swap without new drawing, which shows the
     * screen an older frame: the flicker of two states of one widget and a
     * trail of where the content was. The output already carries the newest
     * frame, so there is nothing there worth showing.
     */
    if (!state->damaged) {
        return;
    }

    state->damaged = false;

    /* A refused frame is the device saying it cannot show anything - a reset
     * driver, a taken-away adapter, a window the desktop closed under the
     * backend. None of that is repairable from inside a draw call, so the
     * backend says it once and stops pretending to draw. A release and a
     * teardown still work, so the host can always hand its uploads back. */
    if (!SDL_RenderPresent(state->renderer)) {
        cf_logf(CF_LOG_BACKEND, "backend: the output took no frame: %s", SDL_GetError());
        state->output_lost = true;
    }
}

/* The library formats nothing until a sink is installed, and this is the one
 * layer with somewhere worth sending the words to: the platform's own log.
 * A host that wants its diagnostics routed here calls cf_sdl_install_log_sink
 * before it opens a window; a host that owns its sink - a game engine with a
 * debug overlay, a service that pipes stderr somewhere else - simply does
 * not, and the backend leaves the caller's sink alone. */
static void sdl_log(cf_log_channel channel, const char *message, void *user_data) {
    (void)channel, (void)user_data;
    SDL_Log("%s", message);
}

void cf_sdl_install_log_sink(void) {
    cf_log_install(sdl_log, NULL);
}

cf_backend cf_backend_sdl(const cf_sdl_config *config) {
    if (config == NULL || config->width == 0u || config->height == 0u) {
        return (cf_backend){0};
    }

    /* Whether the platform is this backend's to stop is decided here, before the
     * ask: a host that already had SDL up keeps it, and the failure paths below
     * and the teardown leave the subsystems exactly as they found them. */
    const bool started_platform = SDL_WasInit(SDL_INIT_VIDEO) == 0u;

    if (started_platform && !SDL_Init(SDL_INIT_VIDEO)) {
        cf_logf(CF_LOG_BACKEND, "backend: SDL did not start: %s", SDL_GetError());
        return (cf_backend){0};
    }

    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;

    /* Resizable from the first frame: the shell re-reads the surface every
     * frame and lays the page out against whatever it says, so a window the
     * person drags is the same page at another width rather than a clipped
     * one. The size in the config is where it starts, not where it stays. */
    if (!SDL_CreateWindowAndRenderer(config->title != NULL ? config->title : "cform",
                                     (int)config->width, (int)config->height, SDL_WINDOW_RESIZABLE,
                                     &window, &renderer)) {
        cf_logf(CF_LOG_BACKEND, "backend: no window: %s", SDL_GetError());

        if (started_platform) {
            SDL_Quit();
        }

        return (cf_backend){0};
    }

    (void)SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    (void)SDL_SetRenderVSync(renderer, 1);

    /* A floor on the drag, in the same points SDL_GetWindowSize reports, so it
     * bites at the size the page lays out against rather than at some pixel
     * count the density has since moved. */
    if (config->min_width != 0u || config->min_height != 0u) {
        SDL_SetWindowMinimumSize(window, (int)config->min_width, (int)config->min_height);
    }

    /* Text events are off until a window asks for them, and a window that
     * never receives them has no way to say what the keyboard typed. */
    SDL_StartTextInput(window);

    /* The state and its texture table live as long as the window does,
     * outside every frame arena, so they go through the base allocator -
     * the only seam that asks the heap for bytes a caller hands back.
     * Everything SDL keeps for itself (the window, the renderer, the
     * per-texture GPU resource) is third-party internal allocation and stays
     * SDL's to free, which the destroy slot below does on the host's ask. */
    sdl_state *state = cf_alloc_block(sizeof *state);

    if (state == NULL) {
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);

        if (started_platform) {
            SDL_Quit();
        }

        return (cf_backend){0};
    }

    state->window = window;
    state->renderer = renderer;
    state->owns_platform = started_platform;

    return (cf_backend){.abi_version = CF_BACKEND_ABI_VERSION,
                        .size = sizeof(cf_backend),
                        .user_data = state,
                        .submit = draw_submit,
                        .present = sdl_present,
                        .create_texture = sdl_create_texture,
                        .poll_event = sdl_poll,
                        .surface = sdl_surface,
                        .load_image = sdl_load_image,
                        .release_texture = sdl_release_texture,
                        .destroy = sdl_destroy};
}
