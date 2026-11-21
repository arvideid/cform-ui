#ifndef CF_APP_INTERNAL_H
#define CF_APP_INTERNAL_H

#include "cf/app.h"

/*
 * The F12 overlay the shell appends to a damaged frame: one panel with the
 * counts a human tunes against - the nodes the view holds, how many of them
 * the frame changed, how many commands the page painted - read off the same
 * arrays the engine keeps, so the panel cannot lie about the frame it sits
 * on. Without a font stack the panel draws bare: the toggle still answers,
 * the numbers wait for a font.
 */
void app_inspector_append(cf_ctx *ctx, const cf_text_stack *fonts, uint32_t painted,
                          cf_command_list *out);

#endif
