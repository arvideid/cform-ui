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
 * ends. A widget that takes children copies them into that arena, so a build
 * may hand one over on its own stack.
 */

/*
 * The ask a button answers: the slot the button kept this frame, so the
 * handler can read the interaction state and rects off the context, and the
 * user pointer the build handed the button. A click and a key are the same ask
 * here, and the callback is not told which one came.
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
 * the theme's variants paint hover, press, focus and disabled over it. The ask
 * fires on the release that starts on the button and ends inside its rect, and
 * on Enter or the space bar while the button holds the focus. The button
 * declares that focus for its caller, since a role paints a control and does not
 * make it reachable. A NULL context, name or handler answers a declaration that
 * draws nothing and fires nothing.
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
 * The bytes a field edits in place: a buffer the build owns, how much of it
 * is in use, and how much it holds, plus where the caret stands and where the
 * selection it extends runs back to. Offsets count bytes from the start of the
 * buffer, and a caret past the end reads as one at the end, so a build that
 * writes the bytes itself needs no call to move the marks.
 *
 * `mark` equal to `caret` selects nothing. The field writes through the whole
 * struct for as long as the build keeps declaring the field - the memory and
 * its lifetime stay the build's.
 */
typedef struct cf_text_edit {
    char *chars;
    uint32_t length;
    uint32_t capacity;
    uint32_t caret;
    uint32_t mark;
} cf_text_edit;

/*
 * A themed text box. The field declares that it takes the keyboard, so the press
 * that starts inside books focus on the field itself rather than on a box its role
 * named, and the keys the focused field receives edit the buffer in place: what
 * the keyboard wrote lands at the caret over whatever is selected, backspace and
 * delete take the selection or the scalar on the side they name, and left, right,
 * home and end move the caret - holding shift extends the selection instead. A
 * press places the caret under the pointer and a drag from it selects to wherever
 * the pointer went; the value never wraps, so a field keeps its height whatever it
 * holds, and the box slides along the value to keep the caret in view.
 *
 * The theme's focused variant hands out the caret and selection tones, so an
 * unfocused field shows its bytes and nothing else. A NULL context, name or
 * buffer answers a declaration that draws nothing and edits nothing; without
 * fonts it still edits and only draws nothing.
 */
cf_node cf_field(cf_ctx *ctx, const char *name, cf_text_edit *text, const cf_text_stack *stack);

/*
 * A viewport that walks its children with the wheel: the offset lives in the
 * block the name keys, the children ride a content box that moves by the
 * offset, and the box clips what it cannot show. The caller sizes the box -
 * a scroller with no height of its own hugs its content and never walks.
 * The walked range is the content box against the viewport's own, so padding
 * on the viewport is spent at the top and lost at the bottom: pad the column
 * the children ride in, not the window. A notch walks the content toward its
 * end. A NULL context or name answers a declaration that draws nothing.
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
