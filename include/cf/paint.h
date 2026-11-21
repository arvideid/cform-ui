#ifndef CF_PAINT_H
#define CF_PAINT_H

#include "cf/base.h"
#include "cf/layout.h"
#include "cf/style.h"

/*
 * The stream a backend draws. One kind per box the style states, in paint
 * order: a parent's box precedes its children and a border follows its own
 * fill, so walking the array front to back composites the frame the tree
 * declaration reads as. The stream is the paint contract - the golden tier
 * snapshots it as data and the backend ABI (cf/backend.h) carries it whole.
 */
typedef enum cf_command_kind {
    CF_COMMAND_FILL,  /* rect filled with color, corners rounded by radius */
    CF_COMMAND_BORDER /* rect stroked by widths, inside rect, rounded by radius */
} cf_command_kind;

typedef struct cf_command {
    cf_command_kind kind;
    cf_rect rect;
    cf_color color;
    /* The stroke widths of a border, painted inside rect in cf_edges order;
     * a side with no width paints nothing. Zero for a fill. */
    cf_edges widths;
    float radius;
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
 * Walks the subtree rooted at `root` in paint order and appends what each box
 * paints: a stated background fills the box layout placed at `rects[slot]`,
 * and a stated border color with a width strokes it. A box that does not
 * reach `viewport` paints nothing; one that straddles it keeps its whole rect,
 * because cutting it is the backend's scissor decision, not the stream's.
 *
 * `scratch` holds the command array. The walk visits a node once and does not
 * allocate per node beyond the commands it emits, so a caller that clears the
 * arena once per frame keeps paint allocation-free overall.
 *
 * Failure policy, identical to cf_layout's: when the arena cannot serve the
 * list, the commands that fit are kept, the walk stops there, and the
 * exhaustion is reported once on CF_LOG_PAINT. A NULL tree, rect array or
 * list is a no-op; a slot outside `node_count` is ignored.
 */
void cf_paint(cf_arena *scratch, const cf_tree *tree, const cf_rect *rects, uint32_t root,
              cf_rect viewport, cf_command_list *out);

#endif
