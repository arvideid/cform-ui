#ifndef CF_WIDGET_H
#define CF_WIDGET_H

#include "cf/node.h"
#include "cf/text.h"

/*
 * The widgets are declaration-filling functions: each returns a cf_node whose
 * fields it has stated, so a build composes them like any other declaration
 * and the diff, resolver and dispatcher see nothing special. Whatever a
 * widget needs beyond the node vocabulary - text, fonts, tones - arrives as
 * an argument, and the per-frame bytes it keeps live in the context's frame
 * arena, which holds from cf_frame_begin until the frame that declared them
 * ends.
 */

/*
 * The click a button fires: the slot the button kept this frame, so the
 * handler can read the interaction state and rects off the context, and the
 * user pointer the build handed the button.
 */
typedef void (*cf_on_press)(cf_ctx *ctx, uint32_t slot, void *user_data);

/*
 * One line of text, drawn through the fonts the build loaded and toned with
 * the color it states. The bytes are borrowed: they must outlive the frame
 * that declares the label, which a static or an arena string both do. The
 * text caps at one line and closes with the ellipsis when the box cannot
 * hold it. A NULL context or a font stack without fonts answers a
 * declaration that measures and paints nothing.
 */
cf_node cf_label(cf_ctx *ctx, cf_string text, const cf_text_stack *stack, cf_color color);

/*
 * A themed press target. The name keys the button's identity across builds;
 * the children - a label, an icon - sit inside the theme's resting box, and
 * the theme's variants paint hover, press, focus and disabled over it. The
 * click fires on the release that starts on the button and ends inside its
 * rect. A NULL context, name or handler answers a declaration that draws
 * nothing and fires nothing.
 */
cf_node cf_button(cf_ctx *ctx, const char *name, cf_on_press on_press, void *user_data,
                  const cf_node *children, uint32_t child_count);

#endif
