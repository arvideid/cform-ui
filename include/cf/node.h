#ifndef CF_NODE_H
#define CF_NODE_H

#include "cf/base.h"
#include "cf/event.h"
#include "cf/layout.h"
#include "cf/paint.h"
#include "cf/text.h"

/*
 * The node's handlers, one table rather than a field per event. The data
 * pointers are handed back unread and unchanged. They outlive the diff that
 * stored them and every dispatch that fires before the next frame begins,
 * because a build that owns a handler's data through the frame arena hands
 * the arena nothing to reuse until the next cf_frame_begin clears it.
 */
typedef struct cf_handlers {
    cf_event_fn pointer;
    void *pointer_data;
    cf_event_fn key;
    void *key_data;
    /*
     * "Do the thing this control is for", asked by the keyboard: Enter or the
     * space bar on the node that holds the focus runs it, and the event still
     * names which key did. Nothing else may be held with it - a control, a command
     * or an alt turns the key into a binding the page put somewhere else, and
     * running this ask beside it would do the thing twice. A shift is read through,
     * since it changes what the key looks like rather than what it asks. A widget
     * whose action is the same whoever asked hands its press callback to both doors;
     * a box with no ask of its own leaves it NULL, and a node that answered the key
     * itself is not asked again.
     */
    cf_event_fn activate;
    void *activate_data;
    /*
     * "The value this box holds moved", asked of the node whose widget edited it: the
     * event is the one that did it, and the bytes stay in whatever the build owns, so a
     * page reads its own buffer rather than a copy handed to it. A caret move is not a
     * change and runs no handler here; the ask is once per event, however many runs the
     * key put in. A box whose value nobody declares a door for leaves it NULL.
     */
    cf_event_fn change;
    void *change_data;
} cf_handlers;

/*
 * What a box is to a person who cannot see it, named for the answer a bridge gives rather than
 * for the style that painted it. The values are append-only, because a bridge reads the number: a
 * role added later must not move the ones a shipped screen reader already knows.
 *
 * Not every box has one. CF_ACCESS_NONE is the honest answer for a wrapper that groups boxes for
 * the painter, and a walk that named every div would read a page as a list of nothing.
 */
typedef enum cf_access {
    CF_ACCESS_NONE,
    CF_ACCESS_GROUP,      /* a box that holds other boxes and means nothing alone */
    CF_ACCESS_BUTTON,     /* a press that does a thing */
    CF_ACCESS_TEXT_BOX,   /* a box whose bytes a person types into */
    CF_ACCESS_LABEL,      /* text meant to be read, not edited */
    CF_ACCESS_IMAGE,      /* a picture, named by the build or by nothing */
    CF_ACCESS_LIST,       /* a scrollable run of items */
    CF_ACCESS_ITEM,       /* one row of a list */
    CF_ACCESS_SCROLL_AREA /* a box that scrolls its content */
} cf_access;

/*
 * Whether a box's words are worth hearing again after they change, and how soon. The question a
 * live region answers is not "what is this box" - the role above is - but "the page moved under
 * this text, interrupt me or wait". A status line that updates when a save finishes can wait; a
 * refusal that explains why a key did nothing is the reason the person pressed it. Bridges map
 * these onto the politeness a screen reader already has: WAI-ARIA's `aria-live` of "polite" and
 * "assertive", and the equivalent of it in UI Automation's live-text property.
 *
 * CF_ANNOUNCE_NONE states nothing about a box of its own, so the box takes the level of the region
 * it sits inside. Liveness belongs to a region rather than to a box, which is what makes a status
 * area cheap to build: mark the group once and every label inside it is covered, including the
 * plain wrappers that carry no role at all.
 */
typedef enum cf_announce {
    CF_ANNOUNCE_NONE,
    CF_ANNOUNCE_POLITE,   /* say the new words when the person is not mid-sentence */
    CF_ANNOUNCE_ASSERTIVE /* say them now: this is what the person was waiting for */
} cf_announce;

/*
 * One declared node: a value the build assembles and the diff reads, never
 * the engine's to keep. `children`, `measure_data` and the handler data stay
 * borrowed through the frame that declared them: the diff reads the child
 * array, and every event dispatched before the next cf_frame_begin hands the
 * data pointers back to their callbacks.
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

    /* What the box is to a person who cannot see it. The theme family above is what
     * the painter reads, and the two are not the same question: a box themed as a
     * button can be a picture of one, and a box the build made from plain nodes can
     * be the control. A widget declares this for its callers; CF_ACCESS_NONE leaves
     * the box out of the announcement a bridge makes, which is what a wrapper is.
     *
     * `name` and `description` are the words to say, in UTF-8 as the build wrote them. A box that
     * holds words rather than boxes - a button, a row, a picture, a field - is announced by the
     * label it carries when it states none; a container is announced by the words its build gave
     * it, or by nothing. The bytes are borrowed for the frame, the same rule the declared text
     * follows. */
    cf_access access;
    cf_string name;
    cf_string description;

    /* Whether the words under this box are meant to be heard again when they change. A build marks
     * the region, not every box in it: the level is inherited downward by the walk, so a status
     * group named once announces the label that moved inside it. */
    cf_announce announce;

    /* Boxes the build means to be read together, named by the key each of them was given. A field
     * states `described_by` for the line beside it that explains what to type, because no walk can
     * pair a control with its own caption; a control states `controls` for the area it opens, so a
     * bridge can say what a press will do before the person makes it. Both are resolved when the
     * walk reaches the box, and a name the frame declares nowhere answers as no box at all - which
     * is also the answer for a dialog that has not been opened yet.
     *
     * The target has to be a box the walk announces: a key carried by a plain wrapper is passed
     * over, because a bridge handed that id would have nothing to read from it. */
    cf_name described_by;
    cf_name controls;

    /* Whether this box takes keyboard focus. A role is not the answer: a box
     * themed as a button can be a picture of one, and a box with no role at all
     * can be the field a person types into. The widgets declare it for their
     * callers; Tab walks the boxes that say yes, a press lands on one only if it
     * does, and `cf_focus` refuses the ones that do not. */
    bool focusable;

    const struct cf_node *children;
    uint32_t child_count;
} cf_node;

/*
 * The context a build declares into and the diff reconciles against. The
 * frame arena holds everything one frame builds and reads - the view the
 * diff hands cf_layout, the walk's scratch - and is emptied when the next
 * frame begins.
 *
 * The node store holds the retained tree across frames, its slots addressed
 * by cf_id, so an id held past a rebuild reads as absent rather than as
 * someone else's node. The state store holds one record per state block, and
 * each block is a heap allocation the record gives back when it retires: a
 * page that has been open for an hour holds the state it is using, not every
 * size it ever asked for.
 *
 * The pointer and keyboard state is held the same way, as generational ids
 * rather than slot bits: a slot the store recycles inherits nothing from the
 * interaction the node before it was having. The rects cf_frame_layout
 * writes live in the frame arena beside the view they were computed from.
 */
typedef struct cf_ctx {
    cf_arena frame;
    cf_interner names;
    cf_store nodes;
    cf_store states;
    /* The design system cf_frame_resolve reads. Borrowed, never copied; a
     * build that styles every box itself leaves it NULL. */
    const cf_theme *theme;
    /*
     * What a widget reaches when it is asked to move bytes out of the page: the
     * host's copy, cut and paste. NULL is a page with no such place to put them,
     * which a field answers by keeping its selection rather than by failing.
     * Borrowed the way the theme is, and for as long as the context reads it.
     */
    const cf_clipboard *clipboard;
    /* The view cf_layout reads, rebuilt by every diff into the frame arena. */
    cf_tree layout;
    cf_id hover_id;
    cf_id press_id;
    cf_id capture_id;
    cf_id focus_id;
    /* The box the keyboard moved away from to get where it stands. The diff hands
     * the keyboard back to it when that box is retired, hidden or switched off, and
     * one box is all it remembers: after a hand-back the remembered box is the one
     * that just went, so a second death leaves the keyboard with nobody. */
    cf_id prior_id;
    uint32_t root_slot;
    uint32_t frame_id;
    /* The interval cf_frame_run was handed for the frame being run, and
     * whether the build running now asked for the next one. Both belong to
     * one frame: the begin of the next clears them. */
    float frame_seconds;
    bool frame_requested;
    /* Asked for by a build that wants the run over. Unlike the frame ask, which
     * the next begin wipes, this one stands: a page that has asked to close has
     * not un-asked by the frame after, and a host that reads it late must not
     * miss it. Only freeing the context puts it back. */
    bool quit_requested;
    /*
     * The keyboard's run an input method is still deciding, named by the field that is
     * holding it: the name is the claim, and no field shows a preedit the claim is not
     * pointing at. A move of the keyboard hands the claim back (a run the box never got to
     * commit is the previous box's to forget), and a field clears it when the method ends the
     * run or the person cancels it. The run's place inside its box is `composition_box`, in
     * logical points, and `composition_id` is the box that was standing when the claim was
     * taken: together they let a platform put its candidate window beside the text rather than
     * beside a caret the preedit has already pushed past it, and let the window follow a page
     * that scrolls under a run the method has not finished. The claim belongs to the keyboard
     * rather than to one frame, which is what `cf_frame_composition_rect` answers the shell
     * with.
     */
    cf_name composing;
    cf_rect composition_box;
    cf_id composition_id;
    /* The reason the last boundary refused since this frame began, or NULL.
     * Every message is a literal the library owns, so the answer outlives the
     * call that recorded it. */
    const char *rejection;
    bool layout_ready;
    /* Absolute rects, or nothing before cf_frame_layout ran this frame. */
    cf_rect *rects;
} cf_ctx;

bool cf_ctx_init(cf_ctx *ctx, size_t frame_bytes);
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
 * state is valid through the frame that obtained it and the one after - with
 * two exceptions the same frame can produce: a later larger request under
 * the same name moves the block and the earlier pointer hangs, and a name
 * this frame stops requesting has its block retired at the next
 * cf_frame_begin, so no pointer into it survives.
 *
 * A name the builds stop requesting loses its block one frame later: the
 * next request under it answers fresh, zeroed memory, and the bytes the
 * retired block held go back to the heap. CF_NO_NAME names no block, a zero
 * size asks for none, and the heap refusing either answer NULL.
 */
void *cf_state(cf_ctx *ctx, cf_name key, size_t size);

/*
 * The bytes every live state block holds. A page's worth is the count of its
 * stateful nodes times what each keeps, so the number staying flat across
 * frames is what says a long run is not growing.
 */
size_t cf_state_bytes(const cf_ctx *ctx);

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
 * The seconds the frame now running covers, the interval whoever ran the
 * pipeline measured between this frame and the one before it. A frame begun
 * without cf_frame_run covers nothing, which is what a build stepped by hand
 * reads - and why nothing moves there until the caller says how long it was.
 */
float cf_frame_seconds(const cf_ctx *ctx);

/*
 * Asks for another frame whatever the tree looks like: a build that moved
 * something no event will move again - a tween still running, content that
 * landed - has a next frame to draw and nothing to wake a resting shell
 * with. The ask lives one frame, the way a dirty bit does, so a build that
 * still wants a frame has to say so in each one.
 */
void cf_request_frame(cf_ctx *ctx);

/* Whether this frame's build asked for the one after it. */
bool cf_frame_requested(const cf_ctx *ctx);

/*
 * Asks the shell running this context to stop: the button that means "quit" has
 * nowhere else to send the word, and a page should not have to reach past its own
 * context into a window it does not own. Nothing closes on the spot - the frame
 * the ask lands in is finished, submitted and shown first, because the press that
 * asked is the press the user should see answered. cf_app_run leaves after that
 * frame; a host that runs cf_frame_run itself reads cf_quit_requested and does the
 * same, which is also how it stays open when the ask means "close this dialog"
 * rather than "exit" - the shell's owner decides what the ask is worth.
 */
void cf_request_quit(cf_ctx *ctx);

/* Whether the page has asked to be done. Sticky: no frame clears it. */
bool cf_quit_requested(const cf_ctx *ctx);

/*
 * Whether this frame changed the node - a fresh slot, or a declaration
 * whose style, measure, handlers or child count differs from the one the
 * slot held. The bit lives one frame: the next begin clears it, whether
 * or not anything read it. The bytes behind measure_data are borrowed and
 * unread, so a change behind them is the callback's to report, by changing
 * what it answers rather than by setting bits.
 *
 * Asked of this frame's slot, the way a build reads the view it just made: the
 * slot is only good until the next diff, which is what `cf_node_id` is for.
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
 * The declaration a frame runs, answering the page it declares. The answer is
 * a value the run holds, so what a build must keep alive is the memory behind
 * `children` and `measure_data` - the frame arena's blocks do, and
 * `cf_children` builds a container's children there. A page with no children
 * and a style that paints nothing is what a build says when it has nothing to
 * declare.
 */
typedef cf_node (*cf_build_fn)(cf_ctx *ctx);

/*
 * One frame, end to end: begins the frame, runs the build with `seconds` as
 * the interval it may read, diffs its declaration in the scope the build
 * declared it in, resolves the theme over the view, lays it out into the
 * offer's points, and paints the commands into `out`. A build that answers a
 * page that paints nothing, or an offer of nothing, empties the list instead
 * of painting it.
 *
 * Failure: when the frame arena cannot hold the declared tree, `cf_diff`
 * refuses, the layout reads no view, and paint writes no commands — `out`
 * comes back empty. The diagnostic names the refusal on
 * `cf_error_message`; the next frame, with a smaller declaration or a larger
 * arena, starts clean. No output is presented as complete when the arena
 * said no.
 */
void cf_frame_run(cf_ctx *ctx, cf_build_fn build, cf_size offer, float seconds,
                  cf_command_list *out);

/*
 * Why the last boundary refused since this frame began, or NULL when nothing
 * has. The answer names the refusal and not the slot, string or size that
 * asked for it: a caller that wants the request reads the frame it built.
 */
const char *cf_error_message(const cf_ctx *ctx);

/*
 * The view's semantic tree, one line per node in paint order and indented by
 * depth: the role a screen reader walks, the text the node carries, the rect
 * it sits at, and a `disabled` suffix where it is switched off. The bytes
 * land in `out`, terminated, up to `capacity`. A context without a view, or
 * a tree the buffer cannot hold, answers false with the lines that fit.
 *
 * This is the diagnostic, not the bridge protocol: an excerpt is a debugging
 * convenience, and a screen reader needs the whole UTF-8 value and an id it can
 * hold. cf_semantic_visit is that.
 */
bool cf_semantic_export(const cf_ctx *ctx, char *out, size_t capacity);

/*
 * What a box is doing, as far as the frame can tell. The bits are a set rather than a field
 * because a box is several at once: switched off, reachable by Tab, and the one the keyboard
 * stands on.
 */
typedef enum cf_semantic_state {
    CF_SEMANTIC_DISABLED = 1 << 0,
    CF_SEMANTIC_FOCUSABLE = 1 << 1,
    CF_SEMANTIC_FOCUSED = 1 << 2,
    CF_SEMANTIC_PRESSED = 1 << 3,
    CF_SEMANTIC_HOVERED = 1 << 4,
    CF_SEMANTIC_EDITABLE = 1 << 5 /* the bytes belong to a buffer a person types into */
} cf_semantic_state;

/*
 * What a bridge may ask the box to do. Each bit names a door the frame has behind it: an
 * activation runs the node's `activate`, a value goes in by the path the keyboard takes, and a
 * focus ask is `cf_focus`, which refuses a box that did not say it takes focus.
 */
typedef enum cf_semantic_action {
    CF_SEMANTIC_ACTIVATE = 1 << 0,
    CF_SEMANTIC_SET_VALUE = 1 << 1,
    CF_SEMANTIC_SET_FOCUS = 1 << 2
} cf_semantic_action;

/*
 * One box as a bridge reads it. `id` is the store's generational identity and `slot` is where
 * this frame dispatches: a bridge keeps the id and forgets the slot, because the frame after may
 * hand its box a different one, and a box that left the tree leaves its slot behind for someone
 * else. `name` is the words to say: what the build stated, or for a box that holds words rather
 * than boxes the text of the label it carries, in which case `labelled_by` names that label.
 * `value` is the bytes the box holds, `selection` the run of them a person is holding, and every
 * string is UTF-8 as declared - nothing cut, nothing replaced - borrowed for the duration of the
 * visit alone.
 *
 * `rect` is in logical points from the frame that laid out, and is zero before the first layout.
 */
typedef struct cf_semantic {
    cf_id id;
    /* This frame's array slot, valid for the visit alone: it is the number the rects are indexed
     * by, not the box. Anything kept past the frame is `id`, which stops naming a node the moment
     * the box leaves the tree instead of naming whoever took the slot. */
    uint32_t slot;
    cf_access access;
    cf_string name;
    cf_string description;
    cf_string value;
    /* Offsets into `value`, low first, so a bridge announces what is held rather than which end the
     * typing started from. Absent for a box that lays no caret - a label, a picture, a wrapper. */
    cf_opt_u32 selection_start;
    cf_opt_u32 selection_end;
    cf_id labelled_by;
    /* Whether this box sits in a live region, and how urgent that region is: the level the box
     * declared, or the one the region around it did. Only CF_ANNOUNCE_NONE means no region covers
     * the box, so a build that marked a group sees the mark on every box the walk reported inside
     * it - and a bridge knows which rows to watch for changed bytes without walking the page for
     * news it was never meant to hear. */
    cf_announce announce;
    /* The boxes this one points at, resolved to their identities for this frame: `described_by` is
     * the box whose words explain this one and `controls` the box this one opens or changes. Both
     * answer CF_NO_ID when the build stated no relation or named a box the frame did not declare -
     * a dialog that is closed is not in the reading, and the button that would open it says so.
     * `labelled_by` above is the relation the walk worked out on its own. */
    cf_id described_by;
    cf_id controls;
    cf_rect rect;
    uint32_t states;
    uint32_t actions;
} cf_semantic;

/*
 * Every box the frame declared that carries a meaning, in paint order and depth first: the walk
 * a bridge starts from. Boxes with no meaning of their own - the wrappers a build groups a page
 * with - are passed through rather than announced, so their controls are still reached, and a
 * hidden box is skipped with everything under it, the way paint and the hit test skip it, so a
 * closed dialog announces nothing. `visit` is called once per box with borrowed data and returns
 * nothing: a bridge that must refuse a box does that in its own book.
 *
 * The walk is how a bridge learns what changed: an untouched box keeps its id from frame to frame,
 * so a re-walk after a repaint reports the same boxes with new words, states or rects rather than
 * a tree that looks new every time, and a box that no longer appears is one the page retired. The
 * boxes worth comparing that way are the ones `announce` says are live, and a page whose own state
 * moved the words inside one asks for the next frame with `cf_request_frame` - the shell's idle
 * rule cannot see a change the tree was never told about.
 *
 * A context with no view calls nothing. The walk allocates nothing, so an announcement can be fed
 * from a frame that has no room left for one more command.
 */
void cf_semantic_visit(const cf_ctx *ctx, void (*visit)(const cf_semantic *node, void *user_data),
                       void *user_data);

/*
 * The reference to keep: the node this frame's array slot holds, or CF_NO_ID when the slot names
 * none. A slot is a place in this frame's arrays, and the store hands a retired one to the next
 * node the build declares, so a number carried across a rebuild can just as well read the box
 * that took it. Values that leave the frame - what a callback hands over, what a bridge stores,
 * what focus moves with - are the id, which either still names the same node or names nothing.
 */
cf_id cf_node_id(const cf_ctx *ctx, uint32_t slot);

/*
 * The slot this frame indexes a node by, or CF_NO_LINK when the id names a box the tree has let
 * go. The rects and the view's arrays are addressed by slot, so a caller holding a reference and
 * wanting this frame's geometry asks here; a handler handed the node in dispatch already has it.
 */
uint32_t cf_node_slot(const cf_ctx *ctx, cf_id id);

/*
 * The topmost node whose rect holds the position - later siblings above
 * earlier ones, children above the parent, paint order read backwards - as the
 * reference a caller may keep. CF_NO_ID when no box lies under the point or
 * cf_frame_layout has not run this frame.
 */
cf_id cf_hit_test(const cf_ctx *ctx, cf_point position);

/*
 * Dispatches one event into the tree. A pointer event goes to the node the
 * pointer is over, or to the one that captured it; a key event goes to the
 * focused node. Handlers run target-first and the walk climbs through the
 * parents until a handler stops it or the root ran. Dispatch before
 * cf_frame_layout hit tests nothing: moves, presses and the wheel drop.
 *
 * A cancellation reaches the box holding the press or the capture and ends both
 * without clicking anything: the pointer is gone, so no release followed it into a
 * rect. The hover the pointer booked goes with it, since a box the hand left behind
 * has no reason to keep wearing the tone of a pointer resting on it.
 *
 * Tab and reverse Tab (the same key with shift held) move focus to the next and
 * previous box that takes it, in document order, wrapping at the ends - unless
 * the node holding focus answered the key first, which is how a dialog keeps the
 * keyboard inside itself. A page with nothing focusable loses focus.
 *
 * Enter and the space bar, with no chord held, ask the focused node to do its job: its
 * activate handler runs, and no other node is asked.
 */
void cf_input(cf_ctx *ctx, cf_event event);

/* Focus follows the press onto a box that takes it; this moves focus by hand. An id that names no
 * live node names no box, and a live box that declares no focus is refused - the same rule Tab
 * walks, so a host cannot land focus somewhere the keyboard could not reach. The box the keyboard
 * leaves is the one it comes back to when the newcomer goes away, which is what lets a dialog
 * close without leaving the page holding a keyboard nobody can answer.
 */
void cf_focus(cf_ctx *ctx, cf_id node);

/*
 * The ask a widget hands back when it has changed the value its node holds: the node's
 * own `change` handler runs, given the event that moved the bytes, and nobody above it
 * is asked - the container that wants to know reads the buffer in its build. A widget
 * calls this from inside a dispatch, on the event it was handed, whose target names the
 * node. A node that is gone, switched off or has declared no door answers nothing, so a
 * widget does not have to look before it reports.
 */
void cf_report_change(cf_ctx *ctx, cf_event *event);

/*
 * The node's interaction state, asked by reference. An id the store no longer holds answers false
 * for every state, and an id that names a different box than the one a state sits on answers false
 * for that one: a page that retired its button reads it as unfocused rather than as whoever was
 * given its slot.
 */
bool cf_hovered(const cf_ctx *ctx, cf_id node);
bool cf_pressed(const cf_ctx *ctx, cf_id node);
bool cf_focused(const cf_ctx *ctx, cf_id node);

/*
 * The cursor the hovered node asks for: the pointer hand over a node that acts
 * on the pointer, the I-beam over a field that edits the bytes under it, the
 * default everywhere else. cf_app_run puts that answer on the backend's
 * `set_cursor` whenever it moves, so a shell built on cf_frame_run asks the same
 * question once per frame and hands it where a cursor can be set.
 */
cf_cursor cf_frame_cursor(const cf_ctx *ctx);

/*
 * Whether the box holding the keyboard takes written text: true for a field the
 * keyboard can reach by Tab, false when focus is on a button, on nothing, or on
 * a box that has since been switched off or hidden. A device with a screen
 * keyboard or an input method should be showing one only while the answer is
 * true, so cf_app_run puts it on the backend's `set_text_input` whenever it
 * moves; a host that runs the pipeline itself asks the same question in its own
 * loop.
 */
bool cf_frame_takes_text(const cf_ctx *ctx);

/*
 * The box the run an input method is holding stands in, in the logical points the rects were
 * laid in, or a rect of no extent when the page is deciding nothing. A platform drawing its own
 * candidate window puts it beside this; cf_app_run hands it to the backend's
 * `set_candidate_rect` whenever it moves, and a host running the pipeline itself asks the same
 * question in its own loop. Nothing is answered once the keyboard has moved or gone nowhere text
 * can go, which is what retires a window that stood beside a box the person clicked away from
 * mid-composition.
 *
 * The answer is measured on the frame it is read from: the run's place inside its box is what
 * the field staged, and the box is the one this frame laid under it, so a page that scrolls or
 * resizes while a method is still deciding carries the candidate window along with the letters it
 * belongs to. A box that has left the view answers nothing, which is the run leaving with it.
 */
cf_rect cf_frame_composition_rect(const cf_ctx *ctx);

#endif
