#include <SDL3/SDL.h>

#include "cf/backend.h"

/*
 * The quarter arc of a corner is walked in this many straight pieces: enough
 * that a UI radius reads round at window scale, few enough that a frame's
 * vertices stay in the low dozens.
 */
#define ARC_SEGMENTS 6u
#define PERIMETER_POINTS (4u * (ARC_SEGMENTS + 1u))

typedef struct sdl_state {
    SDL_Window *window;
    SDL_Renderer *renderer;
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

static void draw_command(sdl_state *state, const cf_command *command, float scale) {
    const SDL_FRect rect = {command->rect.x * scale, command->rect.y * scale,
                            command->rect.width * scale, command->rect.height * scale};
    const float radius = command->radius * scale;
    const SDL_FColor color = {
        (float)command->color.red / 255.0f, (float)command->color.green / 255.0f,
        (float)command->color.blue / 255.0f, (float)command->color.alpha / 255.0f};

    if (command->kind == CF_COMMAND_FILL) {
        if (radius > 0.0f) {
            draw_rounded_fill(state->renderer, rect, radius, color);
        } else {
            SDL_SetRenderDrawColorFloat(state->renderer, color.r, color.g, color.b, color.a);
            SDL_RenderFillRect(state->renderer, &rect);
        }
        return;
    }

    draw_border(state->renderer, rect, radius,
                (cf_edges){command->widths.left * scale, command->widths.top * scale,
                           command->widths.right * scale, command->widths.bottom * scale},
                color);
}

static void draw_submit(void *user_data, const cf_command *commands, uint32_t count) {
    sdl_state *state = user_data;

    if (state == NULL) {
        return;
    }

    /* The frame starts from black: what the stream does not cover is the
     * window's own color, not the last frame's leftovers. */
    SDL_SetRenderDrawColorFloat(state->renderer, 0.0f, 0.0f, 0.0f, 1.0f);
    SDL_RenderClear(state->renderer);

    const float scale = SDL_GetWindowPixelDensity(state->window);

    if (commands == NULL) {
        return;
    }

    uint32_t skipped = 0u;

    for (uint32_t i = 0u; i < count; ++i) {
        /* Glyphs are the font layer's raster work, not built here yet; the
         * lines the stream carries are named once per submit, not per line. */
        if (commands[i].kind == CF_COMMAND_TEXT) {
            skipped++;
            continue;
        }

        draw_command(state, &commands[i], scale);
    }

    if (skipped > 0u) {
        cf_logf(CF_LOG_BACKEND, "backend: %u text lines have no glyphs to draw them yet", skipped);
    }
}

static void sdl_present(void *user_data) {
    sdl_state *state = user_data;

    if (state == NULL) {
        return;
    }

    /* No app layer exists yet to own the event queue, so present drains it: a
     * window whose queue backs up stops answering to the desktop. */
    SDL_PumpEvents();
    SDL_FlushEvents(SDL_EVENT_QUIT, SDL_EVENT_LAST);

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
                        .present = sdl_present};
}
