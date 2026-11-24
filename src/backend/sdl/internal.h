#ifndef CF_BACKEND_SDL_INTERNAL_H
#define CF_BACKEND_SDL_INTERNAL_H

#include <SDL3/SDL.h>

#include "cf/paint.h"

/* Draws one text command's line at the rect layout placed it in, and the
 * ellipsis after it when the cap closed the line. */
void sdl_draw_text(SDL_Renderer *renderer, const cf_command *command, float scale);

/* Uploads RGBA8888 pixels, row after row, as a texture the stream's image
 * commands name. Zero is the answer of a backend that took none. */
uint32_t sdl_create_texture(void *user_data, uint32_t width, uint32_t height, const void *pixels);

/* The density the window draws at, so an extent in device pixels can be handed
 * back in the logical points the layout places. One when there is no window. */
float sdl_pixel_density(void *user_data);

/* Reads an image file and uploads it: the backend's load_image slot, false when
 * the file is not one this decoder opens. */
bool sdl_load_image(void *user_data, const char *path, cf_image_content *out);

#endif
