#include <math.h>

#include <SDL3/SDL.h>

#include "internal.h"

static void set_color(SDL_Renderer *renderer, UI_Color color) {
    SDL_SetRenderDrawColor(
        renderer,
        color.r,
        color.g,
        color.b,
        color.a
    );
}

/* Scanline fill for an axis-aligned rect with rounded corners. Radius is
 * clamped to half the shorter side so a rectangle can never over-round.
 * Falls through to the fast path when the radius is zero. */
static void fill_round_rect(
    SDL_Renderer *renderer,
    SDL_FRect rect,
    float radius
) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) {
        return;
    }

    float max_radius = (rect.w < rect.h ? rect.w : rect.h) * 0.5f;

    if (radius > max_radius) {
        radius = max_radius;
    }

    if (radius <= 0.0f) {
        SDL_RenderFillRect(renderer, &rect);
        return;
    }

    float cy_top = rect.y + radius;
    float cy_bottom = rect.y + rect.h - radius;
    float r_squared = radius * radius;

    int y_first = (int)rect.y;
    int y_last = (int)(rect.y + rect.h);

    for (int y = y_first; y < y_last; y++) {
        float fy = (float)y + 0.5f;
        float inset = 0.0f;
        float dy = 0.0f;

        if (fy < cy_top) {
            dy = cy_top - fy;
        } else if (fy > cy_bottom) {
            dy = fy - cy_bottom;
        }

        if (dy > 0.0f) {
            if (dy >= radius) {
                continue;
            }

            inset = radius - sqrtf(r_squared - dy * dy);
        }

        SDL_FRect row = {
            rect.x + inset,
            (float)y,
            rect.w - 2.0f * inset,
            1.0f
        };
        SDL_RenderFillRect(renderer, &row);
    }
}

static void ui_draw_frame(
    SDL_Renderer *renderer,
    UI_Frame *frame
) {
    const UI_FrameConfig *config = &frame->config;

    SDL_FRect outer = {
        .x = frame->x,
        .y = frame->y,
        .w = frame->width,
        .h = frame->height
    };

    if (config->border_width > 0.0f) {
        set_color(renderer, config->border_color);
        fill_round_rect(renderer, outer, config->corner_radius);

        float bw = config->border_width;
        SDL_FRect inner = {
            .x = outer.x + bw,
            .y = outer.y + bw,
            .w = outer.w - 2.0f * bw,
            .h = outer.h - 2.0f * bw
        };
        float inner_radius = config->corner_radius > bw
            ? config->corner_radius - bw
            : 0.0f;

        set_color(renderer, config->background);
        fill_round_rect(renderer, inner, inner_radius);
    } else {
        set_color(renderer, config->background);
        fill_round_rect(renderer, outer, config->corner_radius);
    }

    for (int i = 0; i < frame->child_count; i++) {
        ui_draw_frame(renderer, frame->children[i]);
    }
}

void ui_render(UI_Context *ctx, UI_Frame *root) {
    if (ctx == NULL || root == NULL) {
        return;
    }

    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;

    if (SDL_CreateWindowAndRenderer(
            "cform-ui",
            ctx->window_width,
            ctx->window_height,
            0,
            &window,
            &renderer) == false) {
        SDL_Log("Failed to create window/renderer: %s", SDL_GetError());
        return;
    }

    bool running = true;

    while (running) {
        SDL_Event event;

        while (SDL_PollEvent(&event) != 0) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
        }

        int width = 0;
        int height = 0;
        SDL_GetWindowSize(window, &width, &height);

        ui_layout_frame(root, 0.0f, 0.0f, (float)width, (float)height);

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        ui_draw_frame(renderer, root);
        SDL_RenderPresent(renderer);

        SDL_Delay(16);
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
}
