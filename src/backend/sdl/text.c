#include <SDL3_ttf/SDL_ttf.h>

#include "internal.h"

/*
 * A line is rasterized, uploaded and dropped in the submit that draws it: the
 * stream carries a handful of lines per frame, its text bytes are borrowed for
 * the call alone, and a cache keyed on them would first have to own a copy of
 * what it keys on.
 */
static void draw_surface(SDL_Renderer *renderer, SDL_Surface *surface, float x, float y,
                         float scale) {
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);

    if (texture == NULL) {
        cf_logf(CF_LOG_BACKEND, "backend: text did not upload: %s", SDL_GetError());
    } else {
        const SDL_FRect dst = {x, y, (float)surface->w * scale, (float)surface->h * scale};
        SDL_RenderTexture(renderer, texture, NULL, &dst);
        SDL_DestroyTexture(texture);
    }

    SDL_DestroySurface(surface);
}

void sdl_draw_text(SDL_Renderer *renderer, const cf_command *command, float scale) {
    TTF_Font *font = command->font.handle;

    if (font == NULL || (command->text.chars == NULL && !command->ellipsis)) {
        return;
    }

    const SDL_Color ink = {command->color.red, command->color.green, command->color.blue,
                           command->color.alpha};

    float x = command->rect.x * scale;
    const float y = command->rect.y * scale;

    /* A run the fallback's closing sign opened carries the mark and no bytes.
     * Rasterizing none would answer with a surface of no width, an upload of
     * nothing and the mark in the place it already lands, so the stretch is
     * skipped rather than because the run would come out wrong. */
    if (command->text.chars != NULL && command->text.length > 0u) {
        SDL_Surface *line =
            TTF_RenderText_Blended(font, command->text.chars, command->text.length, ink);

        if (line == NULL) {
            cf_logf(CF_LOG_BACKEND, "backend: line did not rasterize: %s", SDL_GetError());
            return;
        }

        const float width = (float)line->w * scale;
        draw_surface(renderer, line, x, y, scale);
        x += width;
    }

    /* The measured run already counts the ellipsis's advance, so the mark lands
     * where this run's glyphs stopped rather than past the box. */
    if (command->ellipsis) {
        SDL_Surface *mark = TTF_RenderGlyph_Blended(font, CF_ELLIPSIS, ink);

        if (mark == NULL) {
            cf_logf(CF_LOG_BACKEND, "backend: ellipsis did not rasterize: %s", SDL_GetError());
            return;
        }

        draw_surface(renderer, mark, x, y, scale);
    }
}
