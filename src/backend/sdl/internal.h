#ifndef CF_BACKEND_SDL_INTERNAL_H
#define CF_BACKEND_SDL_INTERNAL_H

#include <SDL3/SDL.h>

#include "cf/paint.h"

/* Draws one text command's line at the rect layout placed it in, and the
 * ellipsis after it when the cap closed the line. */
void sdl_draw_text(SDL_Renderer *renderer, const cf_command *command, float scale);

#endif
