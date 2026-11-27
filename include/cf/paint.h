#ifndef CF_PAINT_H
#define CF_PAINT_H

#include "cf/base.h"
#include "cf/layout.h"
#include "cf/style.h"
#include "cf/text.h"

/*
 * The stream a backend draws. One kind per box the style states, in paint
 * order: a parent's box precedes its children and a border follows its own
 * fill, so walking the array front to back composites the frame the tree
 * declaration reads as. The stream is the paint contract - the golden tier
 * snapshots it as data and the backend ABI (cf/backend.h) carries it whole.
 */
typedef enum cf_command_kind {
    CF_COMMAND_FILL,   /* rect filled with color, corners rounded by radius */
    CF_COMMAND_BORDER, /* rect stroked by widths, inside rect, rounded by radius */
    /* One run of a laid line: the stretch one font draws, set from rect's left edge
     * in that font's own rhythm. A line the stack fell back across arrives as one
     * command per face it needed, so the advances the measure paid for are the
     * advances the device sets. The ellipsis glyph follows the bytes when the flag
     * says so, and a run that carries the sign alone carries no bytes. */
    CF_COMMAND_TEXT,
    /* A texture drawn into rect: the upload the image's content names. */
    CF_COMMAND_IMAGE
} cf_command_kind;

typedef struct cf_command {
    cf_command_kind kind;
    cf_rect rect;
    /* The box this command must not paint outside: the nearest ancestor that
     * clips its content, narrowed by whatever clipped that ancestor. A sharp
     * fill (radius 0) arrives already cut to it, since a plain rectangle is the
     * whole of what it draws, but a rounded fill, an image, a stroke and a text
     * line cannot be cut by rewriting their own rect - the corner, the texture
     * UVs, the stroke and the baseline would move - so they keep their true rect
     * and the backend scissors to this clip. A clip with no extent is the output
     * itself, which the device bounds without being told. */
    cf_rect clip;
    cf_color color;
    /* The stroke widths of a border, painted inside rect in cf_edges order;
     * a side with no width paints nothing. Zero for a fill. */
    cf_edges widths;
    float radius;
    /* The run's own bytes and the font that draws them, on a text command.
     * The bytes are borrowed for the submit alone; the font's handle and ops
     * point at objects that outlive the frame they paint. */
    cf_string text;
    cf_font font;
    bool ellipsis;
    /* The texture an image command draws into rect: the id the backend
     * handed back when the app uploaded the pixels. An id the backend does
     * not know draws nothing. */
    uint32_t texture;
} cf_command;

/*
 * The frame's commands, grown by capacity doubling out of the scratch arena
 * cf_paint was given. The zero state is an empty list, so a caller clears the
 * arena once per frame and passes a zeroed list; everything the list points at
 * dies with the arena clear.
 */
typedef struct cf_command_list {
    cf_command *commands;
    uint32_t count;
    uint32_t capacity;
} cf_command_list;

/*
 * The content an image box paints: the texture the backend issued when the
 * app uploaded the pixels, and the extent the box draws at when no style
 * sizes it. The content is borrowed like a text content's - it must outlive
 * the frame that declares the box.
 */
typedef struct cf_image_content {
    uint32_t texture;
    float width;
    float height;
} cf_image_content;

/*
 * The measure an image box carries: it answers the content's own extent for
 * every offer and both intrinsic kinds, because a replaced element has no
 * packing tighter or wider than itself. The paint walk reads the measure
 * slot to know a box paints an image rather than a fill or lines.
 */
cf_size cf_image_measure(void *content, cf_available available, cf_intrinsic kind);

/*
 * Walks the subtree rooted at `root` in paint order and appends what each box
 * paints: a stated background fills the box layout placed at `rects[slot]`, a
 * stated border color with a width strokes it, a box whose measure slot is
 * cf_text_measure lays its content's lines over its own paint, one command per
 * run of a line, and a box whose measure slot is cf_image_measure paints its
 * content's texture into its rect. A box that does not
 * reach `viewport` paints nothing; one that straddles a clip keeps its whole
 * rect and names that clip in `cf_command.clip`, because cutting a rounded
 * corner, a texture, a stroke or a line by changing what it draws is not the
 * stream's decision - the backend scissors to it. Only a sharp fill (radius 0)
 * is cut in advance, since cutting a plain rectangle is the whole of what it
 * draws.
 *
 * `scratch` holds the command array and the line table a text node asks for.
 * The walk visits a node once and does not allocate per node beyond the
 * commands it emits, so a caller that clears the arena once per frame keeps
 * paint allocation-free overall.
 *
 * Failure policy, identical to cf_layout's: when the arena cannot serve the
 * list, the commands that fit are kept, the walk stops there, and the
 * exhaustion is reported once on CF_LOG_PAINT. A NULL tree, rect array or
 * list is a no-op; a slot outside `node_count` is ignored.
 */
void cf_paint(cf_arena *scratch, const cf_tree *tree, const cf_rect *rects, uint32_t root,
              cf_rect viewport, cf_command_list *out);

#endif
