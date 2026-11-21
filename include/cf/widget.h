#ifndef CF_WIDGET_H
#define CF_WIDGET_H

#include "cf/node.h"
#include "cf/paint.h"
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

/*
 * A box that paints a texture the backend issued when the app uploaded it.
 * The content is borrowed: it must outlive the frame that declares the
 * image, which a static or a frame-arena block both do. The theme's resting
 * box sits under the texture, and the content's own extent sizes the box
 * unless the build states one. A NULL context, name or content answers a
 * declaration that draws nothing.
 */
cf_node cf_image(cf_ctx *ctx, const char *name, const cf_image_content *content);

/*
 * A viewport that walks its children with the wheel: the offset lives in the
 * block the name keys, the children ride a content box that moves by the
 * offset, and the box clips what it cannot show. The caller sizes the box -
 * a scroller with no height of its own hugs its content and never walks.
 * A notch walks the content toward its end. A NULL context or name answers
 * a declaration that draws nothing.
 */
cf_node cf_scroller(cf_ctx *ctx, const char *name, const cf_node *children, uint32_t child_count);

/* The row a list shows: `index` counts the list's rows from its start,
 * whether or not the window is on them. The list sizes and keys the row
 * it gets - the geometry is the virtualization's to keep. */
typedef cf_node (*cf_row_fn)(cf_ctx *ctx, uint32_t index, void *user_data);

/*
 * A scroller over rows of one height, declaring only the rows the viewport
 * can see: `viewport_height` is the box the list gives itself, `row_height`
 * the height every row is held to, and make_row declares one row at a time
 * for the indexes in view. Rows key themselves by index under the list's
 * name, so a row keeps its slot while it stays visible and yields it when
 * it does not. A NULL context, name or row builder, or a row height of
 * nothing, answers a declaration that draws nothing.
 */
cf_node cf_list(cf_ctx *ctx, const char *name, float viewport_height, float row_height,
                uint32_t row_count, cf_row_fn make_row, void *user_data);

#endif
