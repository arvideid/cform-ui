#include "internal.h"

#include "cf/backend.h"

/*
 * The decoder this backend ships with: BMP, which SDL reads inside the library
 * it already links. Anything wider - PNG, JPEG, WebP - is a dependency this
 * repository hands to the host rather than to every build, and the load_image
 * slot is exactly where a host puts its own decoder: what arrives above this
 * file is pixels either way, and nothing can tell who made them.
 */
bool sdl_load_image(void *user_data, const char *path, cf_image_content *out) {
    if (path == NULL || out == NULL) {
        return false;
    }

    SDL_Surface *loaded = SDL_LoadBMP(path);

    if (loaded == NULL) {
        cf_logf(CF_LOG_BACKEND, "backend: no image at %s: %s", path, SDL_GetError());
        return false;
    }

    /* The upload's promise is RGBA8888 packed row after row, which is what a
     * surface converted to RGBA32 hands back. A pitch wider than the row would
     * draw every line shifted, so it is refused rather than quietly copied. */
    SDL_Surface *pixels = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(loaded);

    if (pixels == NULL) {
        cf_logf(CF_LOG_BACKEND, "backend: the image is not RGBA: %s", SDL_GetError());
        return false;
    }

    if (pixels->pitch != pixels->w * 4) {
        cf_logf(CF_LOG_BACKEND, "backend: the image rows are padded: %d for %d pixels",
                pixels->pitch, pixels->w);
        SDL_DestroySurface(pixels);
        return false;
    }

    const uint32_t texture =
        sdl_create_texture(user_data, (uint32_t)pixels->w, (uint32_t)pixels->h, pixels->pixels);

    if (texture == 0u) {
        SDL_DestroySurface(pixels);
        return false;
    }

    /* The extent an image box sizes itself from when the build states none, in
     * the points the layout counts rather than the pixels the file holds. */
    const float density = sdl_pixel_density(user_data);
    *out = (cf_image_content){.texture = texture,
                              .width = (float)pixels->w / density,
                              .height = (float)pixels->h / density};

    SDL_DestroySurface(pixels);

    return true;
}
