#include <SDL3/SDL.h>

#include "internal.h"

static void ui_draw_frame(
    SDL_Renderer *renderer,
    UI_Frame *frame
) {
    SDL_FRect rect = {
        .x = frame->x,
        .y = frame->y,
        .w = frame->width,
        .h = frame->height
    };

    SDL_SetRenderDrawColor(
        renderer,
        frame->config.background.r,
        frame->config.background.g,
        frame->config.background.b,
        frame->config.background.a
    );

    SDL_RenderFillRect(renderer, &rect);

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
