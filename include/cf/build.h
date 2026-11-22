#ifndef CF_BUILD_H
#define CF_BUILD_H

#include "cf/node.h"

/*
 * Sugar over the declaration vocabulary. A macro is never the only way to say
 * something: every form here expands to a plain function whose arguments are
 * the same in the same order, so a build that outgrows the sugar drops the
 * macro and keeps the code.
 */

/*
 * The count a children list has, as the uint32_t cf_box asks for. The operand
 * of `sizeof` is never evaluated, so the compound literal here only states the
 * type and the element count.
 */
#define CF_CHILD_COUNT(...) (uint32_t)(sizeof((cf_node[]){__VA_ARGS__}) / sizeof(cf_node))

/* The pair a container takes for its children: an array of nodes and its
 * count. Expands to two arguments, so it always trails a call's other ones. */
#define CF_CHILDREN(...) (cf_node[]){__VA_ARGS__}, CF_CHILD_COUNT(__VA_ARGS__)

/*
 * A column or a row of the children named between the commas. At least one is
 * required - the list is what counts them, and an empty list is not a thing C
 * can state. A box without children is a `cf_node` literal, or `cf_col` and
 * `cf_row` with nothing to copy.
 */
#define CF_COL(ctx, style, ...) cf_col((ctx), (style), CF_CHILDREN(__VA_ARGS__))
#define CF_ROW(ctx, style, ...) cf_row((ctx), (style), CF_CHILDREN(__VA_ARGS__))

/*
 * The children a declaration hands over, copied into memory the frame owns.
 * The diff reads a whole tree after the build that assembled it has returned,
 * so an array on a caller's stack is not enough: whatever keeps a declaration
 * together keeps its children in the frame's arena, and this is where they
 * land.
 *
 * NULL is the answer for a context that is NULL, a count of nothing, a request
 * the arena refused and children the count named but no array held; the last
 * two say which on `cf_error_message`.
 */
const cf_node *cf_children(cf_ctx *ctx, const cf_node *children, uint32_t child_count);

/*
 * The two containers the declaration vocabulary needs. The direction is stated
 * by which one is called rather than read from the style, because a build that
 * names a row means a row whatever the style says; everything else about the
 * node is the style's and the children's, and a refusal to copy the children
 * leaves the box standing on its own.
 */
cf_node cf_col(cf_ctx *ctx, cf_style style, const cf_node *children, uint32_t child_count);
cf_node cf_row(cf_ctx *ctx, cf_style style, const cf_node *children, uint32_t child_count);

#endif
