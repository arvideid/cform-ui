#include <SDL3/SDL.h>

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
    /* The uploads the stream's image commands name, by id minus one. The
     * table lives as long as the window does, outside every frame arena. */
    SDL_Texture **textures;
    uint32_t texture_count;
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

        if (id != 0u && id <= state->texture_count) {
            SDL_RenderTexture(state->renderer, state->textures[id - 1u], NULL, &rect);
        }
    }
}

/* RGBA8888, one byte per channel, row after row - the layout create_texture
 * promises. The id is the table position plus one, so zero can name refusal. */
static uint32_t sdl_create_texture(void *user_data, uint32_t width, uint32_t height,
                                   const void *pixels) {
    sdl_state *state = user_data;

    if (state == NULL || width == 0u || height == 0u || pixels == NULL) {
        return 0u;
    }

    if (state->texture_count == state->texture_capacity) {
        const uint32_t capacity = state->texture_capacity == 0u ? 8u : state->texture_capacity * 2u;
        SDL_Texture **textures = SDL_realloc(state->textures, (size_t)capacity * sizeof *textures);

        if (textures == NULL) {
            return 0u;
        }

        state->textures = textures;
        state->texture_capacity = capacity;
    }

    SDL_Texture *texture = SDL_CreateTexture(state->renderer, SDL_PIXELFORMAT_RGBA8888,
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

    state->textures[state->texture_count++] = texture;
    return state->texture_count;
}

static void draw_submit(void *user_data, const cf_command *commands, uint32_t count) {
    sdl_state *state = user_data;

    if (state == NULL) {
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
 * One device event into the vocabulary the dispatch walks, false when the event
 * has no word the tree owns. Keyboard scalars pass as SDL keycodes, so a
 * printable code point is its own scalar and the editing keys land on the
 * bytes a field already understands.
 */
static bool take_event(const SDL_Event *event, cf_event *out) {
    switch (event->type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        *out = (cf_event){.type = CF_EVENT_QUIT};
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
        *out = (cf_event){.type = CF_EVENT_SCROLL,
                          .position = {event->wheel.mouse_x, event->wheel.mouse_y},
                          .wheel = {event->wheel.x, event->wheel.y}};
        return true;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        *out = (cf_event){.type = event->type == SDL_EVENT_KEY_DOWN ? CF_EVENT_KEY_DOWN
                                                                    : CF_EVENT_KEY_UP,
                          .key = (uint32_t)event->key.key};
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

    SDL_RenderPresent(state->renderer);
}

cf_backend cf_backend_sdl(const cf_sdl_config *config) {
    if (config == NULL || config->width == 0u || config->height == 0u) {
        return (cf_backend){0};
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        cf_logf(CF_LOG_BACKEND, "backend: SDL did not start: %s", SDL_GetError());
        return (cf_backend){0};
    }

    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;

    if (!SDL_CreateWindowAndRenderer(config->title != NULL ? config->title : "cform",
                                     (int)config->width, (int)config->height, 0, &window,
                                     &renderer)) {
        cf_logf(CF_LOG_BACKEND, "backend: no window: %s", SDL_GetError());
        SDL_Quit();
        return (cf_backend){0};
    }

    (void)SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    (void)SDL_SetRenderVSync(renderer, 1);

    /* The state lives as long as the window does, outside every frame arena,
     * so it goes through SDL's allocator rather than cf_alloc. */
    sdl_state *state = SDL_malloc(sizeof *state);

    if (state == NULL) {
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return (cf_backend){0};
    }

    *state = (sdl_state){.window = window, .renderer = renderer};

    return (cf_backend){.abi_version = CF_BACKEND_ABI_VERSION,
                        .user_data = state,
                        .submit = draw_submit,
                        .present = sdl_present,
                        .create_texture = sdl_create_texture,
                        .poll_event = sdl_poll,
                        .surface = sdl_surface};
}
