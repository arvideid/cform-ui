#ifndef CF_NODE_H
#define CF_NODE_H

#include "cf/base.h"
#include "cf/event.h"
#include "cf/layout.h"

/*
 * The node's handlers, one table rather than a field per event. The data
 * pointers are handed back unread and unchanged, borrowed for the dispatch
 * call like measure_data.
 */
typedef struct cf_handlers {
    cf_event_fn pointer;
    void *pointer_data;
    cf_event_fn key;
    void *key_data;
} cf_handlers;

/*
 * One declared node: a value the build assembles and the diff reads, never
 * the engine's to keep. `children`, `measure_data` and the handler data are
 * borrowed for the diff and dispatch calls alone.
 *
 * `key` is the node's identity, an interned name that every build meaning
 * the same node reuses. A node without a key is reidentified by position:
 * the child that sat at its ordinal under the same parent keeps the slot.
 */
typedef struct cf_node {
    cf_name key;
    cf_style style;
    cf_measure_fn measure;
    void *measure_data;
    cf_handlers handlers;

    /* The widget family the theme styles this node as, and whether it is
     * switched off. CF_ROLE_NONE leaves the style whole; the resolver reads
     * both after the diff and before the layout. */
    cf_role role;
    bool disabled;

    const struct cf_node *children;
    uint32_t child_count;
} cf_node;

/*
 * The context a build declares into and the diff reconciles against. The
 * frame arena holds everything one frame builds and reads - the view the
 * diff hands cf_layout, the walk's scratch - and is emptied when the next
 * frame begins. The persistent arena holds the state blocks cf_state hands
 * out; it is never cleared, so a block outlives every frame and a retired
 * one leaves its bytes orphaned until the context dies.
 *
 * The node store holds the retained tree across frames, its slots addressed
 * by cf_id, so an id held past a rebuild reads as absent rather than as
 * someone else's node. The state store holds one record per state block.
 *
 * The pointer and keyboard state is held the same way, as generational ids
 * rather than slot bits: a slot the store recycles inherits nothing from the
 * interaction the node before it was having. The rects cf_frame_layout
 * writes live in the frame arena beside the view they were computed from.
 */
typedef struct cf_ctx {
    cf_arena frame;
    cf_arena persistent;
    cf_interner names;
    cf_store nodes;
    cf_store states;
    /* The design system cf_frame_resolve reads. Borrowed, never copied; a
     * build that styles every box itself leaves it NULL. */
    const cf_theme *theme;
    /* The view cf_layout reads, rebuilt by every diff into the frame arena. */
    cf_tree layout;
    cf_id hover_id;
    cf_id press_id;
    cf_id capture_id;
    cf_id focus_id;
    uint32_t root_slot;
    uint32_t frame_id;
    bool layout_ready;
    /* Absolute rects, or nothing before cf_frame_layout ran this frame. */
    cf_rect *rects;
} cf_ctx;

bool cf_ctx_init(cf_ctx *ctx, size_t frame_bytes, size_t persistent_bytes);
void cf_ctx_free(cf_ctx *ctx);

/*
 * Starts a frame: the frame arena is emptied and the frame counter advances,
 * so a node the coming build does not redeclare is one the next diff
 * retires. A build and its diff run inside a frame.
 */
void cf_frame_begin(cf_ctx *ctx);

/*
 * A block of build state keyed by an interned name. Every request under the
 * same name answers the same memory, whatever the declaration around it
 * does, and the block starts zeroed. A request larger than the one before
 * it moves the block and keeps the bytes it already held, so a pointer into
 * state is valid through the frame that obtained it and the one after.
 *
 * A name the builds stop requesting loses its block one frame later: the
 * next request under it answers fresh, zeroed memory. CF_NO_NAME names no
 * block, a zero size asks for none, and a persistent arena too small for
 * the request all answer NULL.
 */
void *cf_state(cf_ctx *ctx, cf_name key, size_t size);

/*
 * The offset a scroller keeps under its name. The block is the same keyed
 * memory cf_state serves: a build that declares the same scroller keeps its
 * offset across rebuilds, and one that stops asking loses the block a frame
 * later. The offset is measured from the content's start toward its end.
 */
typedef struct cf_scroll {
    float offset;
} cf_scroll;

/* The scroll block a build keeps under its name, or NULL for a nameless
 * request - the same refusals cf_state gives. */
cf_scroll *cf_scroll_of(cf_ctx *ctx, cf_name key);

/*
 * An offset kept inside the scrollable range: the distance the content
 * reaches past the viewport, and nothing at all when the content fits.
 */
float cf_scroll_clamp(float offset, float viewport, float content);

/*
 * The geometry one scrollbar does its math on: the viewport it shows, the
 * content it looks at, and the track it draws in. The thumb rides the
 * track's y axis.
 */
typedef struct cf_scrollbar {
    float viewport;
    float content;
    cf_rect track;
} cf_scrollbar;

/*
 * The thumb a scrollbar shows for this offset: sized by the viewport's
 * share of the content, placed by the offset's share of the range, and
 * never thinner than a hand can grab. A viewport that already shows the
 * whole content answers the whole track.
 */
cf_rect cf_scrollbar_thumb(cf_scrollbar bar, float offset);

/*
 * The offset a drag produces: the grab point sat in the thumb at
 * offset_at_grab, and the pointer has since moved to position. The track's
 * free length scales the movement, so the thumb follows the pointer and
 * the offset follows the thumb.
 */
float cf_scroll_drag(cf_scrollbar bar, cf_point grab, cf_point position, float offset_at_grab);

/*
 * A value driven from one number to another over a duration, kept in the
 * same keyed block memory cf_state serves. `from` is where the value sat
 * when it was last aimed; step answers where it sits now.
 */
typedef enum cf_ease { CF_EASE_LINEAR, CF_EASE_OUT, CF_EASE_IN_OUT } cf_ease;

typedef struct cf_tween {
    float from;
    float to;
    float elapsed;
    float duration;
    cf_ease ease;
} cf_tween;

/* The tween block a build keeps under its name, or NULL for a nameless
 * request - the same refusals cf_state gives. */
cf_tween *cf_tween_of(cf_ctx *ctx, cf_name key);

/*
 * Aims the tween at `to` over `duration` seconds from wherever the value
 * sits now, so an aim while running restarts from the middle. A duration
 * the frames cannot divide snaps straight to `to`.
 */
void cf_tween_to(cf_tween *tween, float to, float duration, cf_ease ease);

/*
 * Advances the tween by `seconds` and answers the value it now holds. A
 * tween that has run out keeps answering its end until it is aimed again.
 */
float cf_tween_step(cf_tween *tween, float seconds);

/*
 * Whether this frame changed the node - a fresh slot, or a declaration
 * whose style, measure, handlers or child count differs from the one the
 * slot held. The bit lives one frame: the next begin clears it, whether
 * or not anything read it. The bytes behind measure_data are borrowed and
 * unread, so a change behind them is the callback's to report, by changing
 * what it answers rather than by setting bits.
 */
bool cf_dirty(const cf_ctx *ctx, uint32_t slot);

/*
 * Marks a node's whole subtree dirty by hand, the way a change behind a
 * measure callback asks for. The walk follows the child chains, so it
 * costs the subtree it marks.
 */
void cf_dirty_subtree(cf_ctx *ctx, uint32_t slot);

/*
 * Reconciles a declaration against the retained tree. A node that declares
 * the same depth and key keeps its slot, wherever it now sits; a node
 * without a key keeps the slot its ordinal held last frame; everything else
 * is created, and every retained slot the declaration does not reach is
 * retired. Nodes that share a depth and a key match in declaration order
 * against the slots they share, so duplicates keep the slots they kept and
 * one beyond them is new.
 *
 * Returns the root's slot, CF_NO_LINK when nothing was declared or the
 * frame arena could not serve the walk - the frame then yields no view.
 */
uint32_t cf_diff(cf_ctx *ctx, const cf_node *root);

/* The view cf_layout reads, or NULL before this frame's diff has built one. */
const cf_tree *cf_frame_tree(const cf_ctx *ctx);

/*
 * Applies the theme to this frame's view: a node that declares a role reads
 * the theme's variant set for that family, its current interaction state
 * picks the variant, and the view's style becomes the declared style with
 * those differences applied. A node without a role, a build without a
 * theme, or a frame without a view keeps every style as declared.
 *
 * Runs between the diff and the layout: the diff built the view this
 * rewrites, and the layout lays out the styles it leaves.
 */
void cf_frame_resolve(cf_ctx *ctx);

/*
 * Lays this frame's tree out into rect storage the context owns: the
 * cf_layout run the rects input dispatch reads are computed from. A frame
 * whose diff built no view is a no-op.
 */
void cf_frame_layout(cf_ctx *ctx, cf_available available, cf_point origin);

/*
 * The topmost node whose rect holds the position - later siblings above
 * earlier ones, children above the parent, paint order read backwards.
 * CF_NO_LINK before cf_frame_layout has run this frame.
 */
uint32_t cf_hit_test(const cf_ctx *ctx, cf_point position);

/*
 * Dispatches one event into the tree. A pointer event goes to the node the
 * pointer is over, or to the one that captured it; a key event goes to the
 * focused node. Handlers run target-first and the walk climbs through the
 * parents until a handler stops it or the root ran. Dispatch before
 * cf_frame_layout hit tests nothing: moves, presses and the wheel drop.
 */
void cf_input(cf_ctx *ctx, cf_event event);

/* Focus follows the press on the node a press landed on; this sets it by
 * hand. A slot that is not live names no node and is refused. */
void cf_focus(cf_ctx *ctx, uint32_t slot);

/*
 * The node's interaction state, asked by slot. Each answer is false for a
 * slot that is not live or not the one the state is held on, which is how a
 * stale id reads as nothing.
 */
bool cf_hovered(const cf_ctx *ctx, uint32_t slot);
bool cf_pressed(const cf_ctx *ctx, uint32_t slot);
bool cf_focused(const cf_ctx *ctx, uint32_t slot);

/* The cursor the hovered node asks for: the pointer hand over a node that
 * answers pointer events, the default everywhere else. */
cf_cursor cf_frame_cursor(const cf_ctx *ctx);

#endif
