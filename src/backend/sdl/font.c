#include <SDL3_ttf/SDL_ttf.h>

#include "cf/backend.h"

/*
 * The table a TTF_Font answers through: the line's rhythm from the line
 * skip, pen distances from glyph metrics plus the kerning pair, and the
 * drawability question the font itself resolves. Measuring never
 * rasterizes - no glyph is rendered, only priced.
 */
static cf_text_metrics font_metrics(void *font) {
    return (cf_text_metrics){.line_height = (float)TTF_GetFontLineSkip(font)};
}

static float font_advance(void *font, uint32_t codepoint, uint32_t previous) {
    int advance = 0;

    if (!TTF_GetGlyphMetrics(font, codepoint, NULL, NULL, NULL, NULL, &advance)) {
        return 0.0f;
    }

    int kerning = 0;

    if (previous != 0u && !TTF_GetGlyphKerning(font, previous, codepoint, &kerning)) {
        kerning = 0;
    }

    return (float)advance + (float)kerning;
}

static bool font_has_glyph(void *font, uint32_t codepoint) {
    return TTF_FontHasGlyph(font, codepoint);
}

static const cf_text_ops font_ops = {
    .metrics = font_metrics, .advance = font_advance, .has_glyph = font_has_glyph};

cf_font cf_font_sdl(const char *file_path, float point_size) {
    if (file_path == NULL || point_size <= 0.0f) {
        return (cf_font){0};
    }

    if (!TTF_Init()) {
        cf_logf(CF_LOG_BACKEND, "backend: SDL_ttf did not start: %s", SDL_GetError());
        return (cf_font){0};
    }

    TTF_Font *font = TTF_OpenFont(file_path, point_size);

    if (font == NULL) {
        cf_logf(CF_LOG_BACKEND, "backend: no font at %s: %s", file_path, SDL_GetError());
        TTF_Quit();
        return (cf_font){0};
    }

    return (cf_font){.handle = font, .ops = &font_ops};
}
