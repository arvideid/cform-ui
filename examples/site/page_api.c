/*
 * API: every exported function, in the order the layers are linked.
 *
 * The declarations are copied out of the headers under `include/cf` rather than
 * paraphrased, because a signature restated is a signature that can be restated
 * wrongly, and the page that documents an API has no better source than the API. What
 * the page adds is the line under each one: the contract in the length a header comment
 * cannot be, since a header has room for a paragraph about the function it introduces
 * and none about the twenty that follow it.
 *
 * This is the file the drift gate reads. `tools/check_docs.py` takes every `cf_*`
 * declaration in the headers and fails the one that has gone missing here, and it
 * fails a name this page still lists after the header dropped it. That check is the
 * only reason a hand-written API page is an honest thing to maintain; without it this
 * page would be a slow lie, and the plan says so.
 */
#include "pages.h"

/* One symbol: the name to list, the declaration to print, what it promises. */
typedef struct api_symbol {
    const char *name;
    const char *declaration;
    const char *contract;
} api_symbol;

/* One header, and the paragraph that says what that layer is for. */
typedef struct api_group {
    const char *header;
    const char *title;
    const char *slug;
    const char *blurb;
    const api_symbol *symbols;
    uint32_t count;
} api_group;

/* types: the words every other declaration on this page is written in. */
static const api_symbol types_symbols[] = {
    {"CF_PT", "CF_PT(points)",
     "A measurement in points, which is what a style field holds and what a backend turns into "
     "pixels at the surface's density."},
    {"CF_PX", "CF_PX(pixels)",
     "The same constructor spelled the way a caller means it; one point is one pixel at a density "
     "of one, and nothing else in the tree keeps a second unit."},
    {"CF_FR", "CF_FR(shares)",
     "A share of whatever is left over, which only means something to a flexible container."},
    {"CF_PERCENT", "CF_PERCENT(portion)",
     "A fraction of the reference the pass has been given; the resolver answers it, a caller never "
     "has to."},
    {"CF_COLOR", "CF_COLOR(red, green, blue)",
     "An opaque colour from three channels, so a declaration cannot accidentally state a fourth "
     "one "
     "nobody reads."},
    {"cf_id_equal", "cf_id_equal(first, second)",
     "Both halves of a slot compared: an index that matches a recycled slot is not the same thing "
     "as the element that was there."},
    {"cf_id_is_none", "cf_id_is_none(id)",
     "The tagged empty id, which is how a missing slot is stated without a second field saying "
     "so."},
};

/* base: the only layer that asks the heap for anything. */
static const api_symbol base_symbols[] = {
    {"cf_alloc_block", "void *cf_alloc_block(size_t size);",
     "Bytes that outlive a frame: the only seam in the tree that asks the heap."},
    {"cf_alloc", "void *cf_alloc(cf_arena *arena, size_t size);",
     "A slice of an arena. NULL when the request does not fit, and the arena says so after."},
    {"cf_free_block", "void cf_free_block(void *block);", "Hands a block back."},
    {"cf_resize_block", "void *cf_resize_block(void *block, size_t held, size_t size);",
     "The block at a new size, the bytes it held kept and the rest zeroed."},
    {"cf_arena_init", "bool cf_arena_init(cf_arena *arena, size_t capacity);",
     "An arena with room for a stated number of bytes."},
    {"cf_arena_clear", "void cf_arena_clear(cf_arena *arena);",
     "Ends every allocation the arena served, at once and without a free per pointer."},
    {"cf_arena_free", "void cf_arena_free(cf_arena *arena);",
     "Frees the arena's own backing store."},
    {"cf_arena_mark", "size_t cf_arena_mark(const cf_arena *arena);",
     "Where this call began, so its scratch can go back while anything above it stays put."},
    {"cf_arena_rewind", "void cf_arena_rewind(cf_arena *arena, size_t mark);",
     "Back to a mark; rewinds happen in the reverse order of the marks, which is what recursion "
     "does."},
    {"cf_store_init",
     "bool cf_store_init(cf_store *store, size_t element_size, size_t initial_capacity);",
     "Fixed-stride slots addressed by id, growing by doubling."},
    {"cf_store_add", "void *cf_store_add(cf_store *store, cf_id *out_id);",
     "A fresh slot, or CF_NO_ID when the store cannot grow."},
    {"cf_store_get", "void *cf_store_get(const cf_store *store, cf_id id);",
     "The element an id names; NULL for a slot that was recycled."},
    {"cf_store_remove", "void cf_store_remove(cf_store *store, cf_id id);",
     "Retires a slot, so an id held past it reads as absent rather than as someone else's."},
    {"cf_store_at", "void *cf_store_at(const cf_store *store, size_t slot);",
     "Iteration by index; NULL for a slot that is not live."},
    {"cf_store_id", "cf_id cf_store_id(const cf_store *store, size_t slot);",
     "The id held at an index, which is how a walk gets a handle it can keep."},
    {"cf_store_free", "void cf_store_free(cf_store *store);", "Frees a store's parallel arrays."},
    {"cf_store_bytes", "size_t cf_store_bytes(const cf_store *store);",
     "What the arrays cost, retired slots included; the number a long session is watched on."},
    {"cf_interner_init", "bool cf_interner_init(cf_interner *interner, size_t initial_capacity);",
     "A table of text identity by value."},
    {"cf_intern", "cf_name cf_intern(cf_interner *interner, cf_string text);",
     "The name for these bytes, copying them when it is a new one."},
    {"cf_intern_text", "cf_name cf_intern_text(cf_interner *interner, const char *chars);",
     "The same for a NUL-terminated string, which is what a build usually has."},
    {"cf_interned", "cf_string cf_interned(const cf_interner *interner, cf_name name);",
     "The bytes a name was issued for."},
    {"cf_interner_free", "void cf_interner_free(cf_interner *interner);",
     "Frees every name the table handed out."},
    {"cf_interner_bytes", "size_t cf_interner_bytes(const cf_interner *interner);",
     "What the issued names and the arrays indexing them cost."},
    {"cf_string_of", "cf_string cf_string_of(const char *chars);",
     "A borrowed view of a C string: the bytes and their length, no copy."},
    {"cf_hash_bytes", "uint32_t cf_hash_bytes(const void *bytes, size_t length);",
     "FNV-1a over a buffer."},
    {"cf_hash_fold", "uint32_t cf_hash_fold(uint32_t seed, const void *bytes, size_t length);",
     "The same hash continued from a seed, so several buffers become one identity."},
    {"cf_log_install", "void cf_log_install(cf_log_sink sink, void *user_data);",
     "Where the library's diagnostics go; a NULL sink silences them and nothing is formatted into "
     "the void."},
    {"cf_logf", "void cf_logf(cf_log_channel channel, const char *format, ...);",
     "One line on one channel, for a host that wants its own words in the same stream."},
};

/* style: the vocabulary a box is stated in, and the design system that restates it. */
static const api_symbol style_symbols[] = {
    {"cf_style_default", "cf_style cf_style_default(void);",
     "The CSS initial values: shrink 1, zero margins, everything else the zero state."},
    {"cf_style_varied",
     "cf_style cf_style_varied(cf_style base, const cf_variants *variants, uint32_t states);",
     "The base with the states' differences over it, in enum order, so a disabled box wins."},
    {"cf_theme_default", "const cf_theme *cf_theme_default(void);",
     "The neutral scale the gallery reads and this site is themed with: near-whites and one dark "
     "accent."},
    {"cf_length_resolve", "float cf_length_resolve(cf_length length, float reference);",
     "A length as a plain measurement; auto and fr hold nothing and read as zero."},
    {"cf_length_of", "bool cf_length_of(cf_length declaration, cf_length offer, float *out);",
     "A declaration turned into a length, or false when the content has to be measured instead."},
    {"cf_length_is_auto", "bool cf_length_is_auto(cf_length length);",
     "Whether the length asks for nothing."},
    {"cf_box_resolve", "cf_edges cf_box_resolve(cf_box box, float reference);",
     "A box's four sides as measurements; a negative inset reads as zero."},
    {"cf_size_clamp",
     "float cf_size_clamp(float value, cf_length min, cf_length max, cf_length reference);",
     "An extent forced inside its constraints, with min keeping the last word."},
    {"cf_tracks_repeat",
     "uint32_t cf_tracks_repeat(cf_track *out, uint32_t capacity, const cf_track *pattern, "
     "uint32_t pattern_count, uint32_t times);",
     "A track pattern tiled into a row or column, as many as the capacity holds."},
    {"cf_direction_is_row", "bool cf_direction_is_row(cf_direction direction);",
     "Whether children pack along the horizontal axis."},
    {"cf_direction_is_reverse", "bool cf_direction_is_reverse(cf_direction direction);",
     "Whether packing starts at the far edge, which is what turns a row into a mirrored one."},
    {"cf_style_clips_content", "bool cf_style_clips_content(const cf_style *style);",
     "The one question about a style that a page asks as often as it sets it, answered so nobody "
     "has to remember which field the flag sits in."},
};

/* text: measuring what a font can draw, and laying it into lines. */
static const api_symbol text_symbols[] = {
    {"cf_text_measure",
     "cf_size cf_text_measure(void *content, cf_available available, cf_intrinsic kind);",
     "The measure slot's text implementation, and the marker the paint walk reads as words."},
    {"cf_text_lines",
     "uint32_t cf_text_lines(const cf_text_content *content, float max_width, cf_text_line *out, "
     "uint32_t capacity);",
     "The lines a definite width wraps the content into - the same walk the measure counted."},
    {"cf_text_line_of",
     "uint32_t cf_text_line_of(const cf_text_content *content, const cf_text_line *lines, uint32_t "
     "count, uint32_t offset);",
     "Which laid line a byte offset falls on."},
    {"cf_text_each_run",
     "void cf_text_each_run(const cf_text_content *content, const cf_text_line *line, "
     "void (*visit)(void *user_data, const cf_text_run *run), void *user_data);",
     "Hands over the runs a laid line is drawn in, each with the face that measured it, so no "
     "device sets a glyph in a width its box was never sized for."},
    {"cf_text_caret_x",
     "float cf_text_caret_x(const cf_text_content *content, const cf_text_line *line, uint32_t "
     "offset);",
     "How far along a line the caret for an offset stands, clamped into the line."},
    {"cf_text_offset_at",
     "uint32_t cf_text_offset_at(const cf_text_content *content, const cf_text_line *line, float "
     "x);",
     "The offset a click along a line names, so a click at a word's end lands after the word."},
    {"cf_text_scroll",
     "cf_point cf_text_scroll(const cf_text_content *content, const cf_text_line *lines, uint32_t "
     "count, cf_size window);",
     "How far a box must slide to keep the caret it states inside itself."},
    {"cf_text_next_cluster", "uint32_t cf_text_next_cluster(cf_string text, uint32_t offset);",
     "The boundary a step forward lands on, so an accent written onto a letter crosses as the "
     "letter does."},
    {"cf_text_prev_cluster", "uint32_t cf_text_prev_cluster(cf_string text, uint32_t offset);",
     "The boundary a backspace stops at, which takes a joined picture apart rather than one byte "
     "of it."},
};

/* layout: the pass that turns a declaration into geometry. */
static const api_symbol layout_symbols[] = {
    {"cf_layout",
     "void cf_layout(cf_arena *scratch, const cf_tree *tree, uint32_t root, cf_available "
     "available, cf_point origin, cf_rect *rects);",
     "Flexbox as the specification defines it plus a useful subset of grid: pure arithmetic, one "
     "rect per box, no clock and no window."},
};

/* paint: the pass that turns geometry into the stream a backend draws. */
static const api_symbol paint_symbols[] = {
    {"cf_paint",
     "void cf_paint(cf_arena *scratch, const cf_tree *tree, const cf_rect *rects, uint32_t root, "
     "cf_rect viewport, cf_command_list *out);",
     "The laid tree as commands in paint order, each naming the clip it must not be seen outside."},
    {"cf_image_measure",
     "cf_size cf_image_measure(void *content, cf_available available, cf_intrinsic kind);",
     "A replaced element's measure: its own extent, whatever the offer, because it packs no "
     "tighter."},
};

/* node: the context a build declares into and the diff reconciles against. */
static const api_symbol node_symbols[] = {
    {"cf_ctx_init", "bool cf_ctx_init(cf_ctx *ctx, size_t frame_bytes);",
     "A context whose frame arena can hold one frame's declaration."},
    {"cf_ctx_free", "void cf_ctx_free(cf_ctx *ctx);",
     "Frees the stores, the names and the state a context kept."},
    {"cf_frame_begin", "void cf_frame_begin(cf_ctx *ctx);",
     "Starts a frame: the arena is emptied and the counter advances, so a node the build does not "
     "redeclare is one the diff retires."},
    {"cf_diff", "uint32_t cf_diff(cf_ctx *ctx, const cf_node *root);",
     "Reconciles a declaration against the retained tree and answers the root's slot."},
    {"cf_frame_tree", "const cf_tree *cf_frame_tree(const cf_ctx *ctx);",
     "The view the layout reads, or NULL before this frame's diff built one."},
    {"cf_frame_resolve", "void cf_frame_resolve(cf_ctx *ctx);",
     "Applies the theme to the view, between the diff that built it and the layout that reads it."},
    {"cf_frame_layout",
     "void cf_frame_layout(cf_ctx *ctx, cf_available available, cf_point origin);",
     "Lays this frame's tree into rect storage the context owns."},
    {"cf_frame_run",
     "void cf_frame_run(cf_ctx *ctx, cf_build_fn build, cf_size offer, float seconds, "
     "cf_command_list *out);",
     "One frame end to end, and the whole reason a refused arena yields an empty list rather than "
     "a half-laid screen."},
    {"cf_error_message", "const char *cf_error_message(const cf_ctx *ctx);",
     "Why the last boundary refused since the frame began, or NULL when nothing has."},
    {"cf_input", "void cf_input(cf_ctx *ctx, cf_event event);",
     "Dispatches one event to the node under it, climbing through the parents until a handler "
     "stops it."},
    {"cf_node_id", "cf_id cf_node_id(const cf_ctx *ctx, uint32_t slot);",
     "The number to keep: the node a frame's array slot holds, which stops naming a box the moment "
     "the tree lets it go instead of naming whoever got the slot."},
    {"cf_node_slot", "uint32_t cf_node_slot(const cf_ctx *ctx, cf_id id);",
     "This frame's place for a node, for reading its rect; no place at all when the reference "
     "names "
     "a box the page no longer has."},
    {"cf_hit_test", "cf_id cf_hit_test(const cf_ctx *ctx, cf_point position);",
     "The topmost box whose rect holds a point, in paint order read backwards, as the reference a "
     "caller may hold on to."},
    {"cf_focus", "void cf_focus(cf_ctx *ctx, cf_id node);",
     "Moves the keyboard by hand, refusing a box that declared it takes none."},
    {"cf_report_change", "void cf_report_change(cf_ctx *ctx, cf_event *event);",
     "Runs the node's own change handler with the event that moved its bytes, so a widget "
     "reports a value without asking anything above it to look."},
    {"cf_hovered", "bool cf_hovered(const cf_ctx *ctx, cf_id node);",
     "Whether the pointer rests on this node."},
    {"cf_pressed", "bool cf_pressed(const cf_ctx *ctx, cf_id node);",
     "Whether a press began here and has not ended."},
    {"cf_focused", "bool cf_focused(const cf_ctx *ctx, cf_id node);",
     "Whether this node holds the keyboard."},
    {"cf_frame_cursor", "cf_cursor cf_frame_cursor(const cf_ctx *ctx);",
     "The cursor the hovered node asks for, which a shell hands to its backend once per frame."},
    {"cf_frame_takes_text", "bool cf_frame_takes_text(const cf_ctx *ctx);",
     "Whether the focused box takes written text: when a device with a screen keyboard should show "
     "one."},
    {"cf_frame_composition_rect", "cf_rect cf_frame_composition_rect(const cf_ctx *ctx);",
     "Where the run an input method is still deciding stands, taken from the box this frame laid "
     "under it - the number a candidate window is placed with, and one that follows a page that "
     "moves."},
    {"cf_state", "void *cf_state(cf_ctx *ctx, cf_name key, size_t size);",
     "A block of build state under a name, zeroed once, kept while it is asked for and retired a "
     "frame after it is not."},
    {"cf_state_bytes", "size_t cf_state_bytes(const cf_ctx *ctx);",
     "What every live state block holds; the number staying flat is the proof a long run is not "
     "growing."},
    {"cf_scroll_of", "cf_scroll *cf_scroll_of(cf_ctx *ctx, cf_name key);",
     "The offset a scroller keeps under its name."},
    {"cf_scroll_clamp", "float cf_scroll_clamp(float offset, float viewport, float content);",
     "An offset kept inside the range the content reaches past the viewport."},
    {"cf_scrollbar_thumb", "cf_rect cf_scrollbar_thumb(cf_scrollbar bar, float offset);",
     "The thumb a scrollbar shows: sized by the viewport's share of the content, never thinner "
     "than a hand can grab."},
    {"cf_scroll_drag",
     "float cf_scroll_drag(cf_scrollbar bar, cf_point grab, cf_point position, float "
     "offset_at_grab);",
     "The offset a drag of that thumb produces, scaled by the track's free length."},
    {"cf_tween_of", "cf_tween *cf_tween_of(cf_ctx *ctx, cf_name key);",
     "The motion block a build keeps under its name, in the same memory state uses."},
    {"cf_tween_to", "void cf_tween_to(cf_tween *tween, float to, float duration, cf_ease ease);",
     "Aims a tween from wherever it sits now, so an aim mid-run restarts from the middle."},
    {"cf_tween_step", "float cf_tween_step(cf_tween *tween, float seconds);",
     "Advances a tween and answers where it now sits; a run-out tween keeps answering its end."},
    {"cf_frame_seconds", "float cf_frame_seconds(const cf_ctx *ctx);",
     "The interval this frame covers, which is what a frame-rate-independent motion is multiplied "
     "by."},
    {"cf_request_frame", "void cf_request_frame(cf_ctx *ctx);",
     "Asks for another frame whatever the tree looks like: a build that moved something no event "
     "will move again."},
    {"cf_frame_requested", "bool cf_frame_requested(const cf_ctx *ctx);",
     "Whether this frame's build asked for the one after it."},
    {"cf_request_quit", "void cf_request_quit(cf_ctx *ctx);",
     "Asks the shell running this context to stop, after the frame the ask lands in has been "
     "shown."},
    {"cf_quit_requested", "bool cf_quit_requested(const cf_ctx *ctx);",
     "Whether the page has asked to be done. Sticky: no frame clears it, only freeing the context "
     "does."},
    {"cf_dirty", "bool cf_dirty(const cf_ctx *ctx, uint32_t slot);",
     "Whether this frame changed the node; the bit lives one frame whether or not anything read "
     "it."},
    {"cf_dirty_subtree", "void cf_dirty_subtree(cf_ctx *ctx, uint32_t slot);",
     "Marks a node and everything under it dirty by hand, the way a change behind a measure "
     "callback asks for."},
    {"cf_semantic_export",
     "bool cf_semantic_export(const cf_ctx *ctx, char *out, size_t capacity);",
     "The view as the tree a screen reader walks: role, text, rect and a disabled suffix, one line "
     "per node."},
    {"cf_semantic_visit",
     "void cf_semantic_visit(const cf_ctx *ctx, void (*visit)(const cf_semantic *node, void "
     "*user_data), void *user_data);",
     "The same walk for a bridge rather than a log: the whole value as UTF-8, an id a reader can "
     "hold across frames, and the state, action and relation each box declares."},
};

/* build: the sugar over the declaration vocabulary. */
static const api_symbol build_symbols[] = {
    {"cf_children",
     "const cf_node *cf_children(cf_ctx *ctx, const cf_node *children, uint32_t child_count);",
     "Copies a child list into memory the frame owns, so a caller may build one on its own stack."},
    {"cf_col",
     "cf_node cf_col(cf_ctx *ctx, cf_style style, const cf_node *children, uint32_t child_count);",
     "A container that packs its children downward, whatever the style says."},
    {"cf_row",
     "cf_node cf_row(cf_ctx *ctx, cf_style style, const cf_node *children, uint32_t child_count);",
     "A container that packs its children sideways."},
    {"CF_ROW", "CF_ROW(ctx, style, ...) cf_row((ctx), (style), CF_CHILDREN(__VA_ARGS__))",
     "The same container with the child list written inline, which is the only reason the macro "
     "exists."},
    {"CF_COL", "CF_COL(ctx, style, ...) cf_col((ctx), (style), CF_CHILDREN(__VA_ARGS__))",
     "The vertical form."},
    {"CF_CHILDREN", "CF_CHILDREN(...) (cf_node[]){__VA_ARGS__}, CF_CHILD_COUNT(__VA_ARGS__)",
     "An argument list as the pair the function takes: a compound array and its length, so a "
     "declaration reads as a tree rather than as two statements."},
    {"CF_CHILD_COUNT",
     "CF_CHILD_COUNT(...) (uint32_t)(sizeof((cf_node[]){__VA_ARGS__}) / sizeof(cf_node))",
     "The count half on its own, for a caller that keeps the list and needs only its size."},
};

/* event: what a handler may ask the dispatch to do. */
static const api_symbol event_symbols[] = {
    {"cf_stop", "void cf_stop(cf_event *event);",
     "Keeps the event from the handlers above the one that stopped it, which is how a dialog keeps "
     "a key to itself."},
    {"cf_capture", "void cf_capture(cf_event *event);",
     "Holds the pointer on the node that asked: moves and the release keep arriving there wherever "
     "the pointer went."},
};

/* widget: the boxes a build composes instead of declaring by hand. */
static const api_symbol widget_symbols[] = {
    {"cf_label",
     "cf_node cf_label(cf_ctx *ctx, cf_string text, const cf_text_stack *stack, cf_color color);",
     "One line of text, capped at one line and closed with an ellipsis when the box cannot hold "
     "it."},
    {"cf_button",
     "cf_node cf_button(cf_ctx *ctx, const char *name, cf_on_press on_press, void *user_data, "
     "const cf_node *children, uint32_t child_count);",
     "A themed press target that fires on a click that ends inside it and on Enter or the space "
     "bar while it holds the focus."},
    {"cf_field",
     "cf_node cf_field(cf_ctx *ctx, const char *name, cf_text_edit *text, const cf_text_stack "
     "*stack);",
     "A themed text box that edits a buffer the build owns and declares that it takes the "
     "keyboard."},
    {"cf_scroller",
     "cf_node cf_scroller(cf_ctx *ctx, const char *name, const cf_node *children, uint32_t "
     "child_count);",
     "A viewport that walks its children with the wheel; the caller sizes it, since a scroller "
     "with no height never walks."},
    {"cf_list",
     "cf_node cf_list(cf_ctx *ctx, const char *name, float viewport_height, float row_height, "
     "uint32_t row_count, cf_row_fn make_row, void *user_data);",
     "A scroller over equal rows that declares only the ones the viewport can see."},
    {"cf_image",
     "cf_node cf_image(cf_ctx *ctx, const char *name, const cf_image_content *content);",
     "A box painting a texture the backend issued when the pixels were uploaded."},
};

/* app: the loop a whole program fits in. */
static const api_symbol app_symbols[] = {
    {"cf_app_run", "void cf_app_run(const cf_app_config *config);",
     "Every frame: drain the backend's events, run the pipeline against the surface offered now, "
     "submit what was painted, until the run is over."},
};

/* backend: the seam between points and pixels. */
static const api_symbol backend_symbols[] = {
    {"cf_backend_sdl", "cf_backend cf_backend_sdl(const cf_sdl_config *config);",
     "A backend drawing to a new window; a window that will not open is a backend that draws "
     "nothing, reported once."},
    {"cf_backend_recorder", "cf_backend cf_backend_recorder(cf_recorder *recorder);",
     "A backend that records instead of draws, which is what every headless check in this "
     "repository runs against."},
    {"cf_sdl_install_log_sink", "void cf_sdl_install_log_sink(void);",
     "Routes the library's diagnostics into SDL's log channel, for a host that has no sink of its "
     "own."},
    {"cf_clipboard_sdl", "cf_clipboard cf_clipboard_sdl(void);",
     "The desk's clipboard as the pair a field reads and writes; hand it to the config and copy, "
     "cut and paste reach the desktop."},
    {"cf_font_sdl", "cf_font cf_font_sdl(const char *file_path, float point_size);",
     "A font's metrics over a TrueType or OpenType file at a point size; measuring never "
     "rasterizes."},
    {"cf_font_sdl_close", "void cf_font_sdl_close(cf_font *font);",
     "Hands back what cf_font_sdl took. Nothing above the theme asks for this, so the caller that "
     "opened a font closes it."},
    {"cf_web_read_hash", "bool cf_web_read_hash(char *out, uint32_t size);",
     "The route after the # in a browser's address bar; always false where there is no browser."},
    {"cf_web_write_hash", "void cf_web_write_hash(const char *route);",
     "Puts that route back, so a nav press changes the URL a reader can copy out of the bar."},
    {"cf_web_open_url", "void cf_web_open_url(const char *url);",
     "Asks the browser for a tab at an address that is not this document, because a drawing has no "
     "anchor element."},
};

/* version: the number the build was made from. */
static const api_symbol version_symbols[] = {
    {"cf_version_string", "const char *cf_version_string(void);",
     "The version this library was compiled from, which is the same number include/cf/version.h "
     "states and the top bar of this site prints."},
};

/* The layers in the order they are linked: the base a page cannot see, up to the
 * backend a page must not reach past. A reader who wants to know what a layer is for
 * reads one paragraph, and who wants the exact signature reads the line above the
 * sentence they are on. */
static const api_group groups[] = {
    {"include/cf/types.h", "The vocabulary", "types",
     "Points, shares, colours and ids: the small types every other header states a field "
     "with, and the constructors that keep a declaration honest. A length carries which "
     "kind of number it is rather than trusting a comment, so an `fr` cannot reach a pass "
     "as a stray pixel.",
     types_symbols, SITE_COUNT(types_symbols)},
    {"include/cf/base.h", "Memory, names and logs", "base",
     "The bottom layer, and the only one that calls the heap. Everything above it takes "
     "an arena and hands its bytes back by clearing it, which is why a frame cannot leak "
     "and why nothing in a frame has to answer who owns a byte.",
     base_symbols, SITE_COUNT(base_symbols)},
    {"include/cf/style.h", "Styles and the design system", "style",
     "A box is stated as fields of one struct, in the same vocabulary CSS uses. The theme "
     "sits beside it as one style plus one variant set per widget family, so a control's "
     "resting look and its hover, press, focus and disabled come from one table rather "
     "than from a rule written per call site.",
     style_symbols, SITE_COUNT(style_symbols)},
    {"include/cf/text.h", "Text", "text",
     "Measuring and laying words, with no font in sight: the content carries a stack of "
     "fonts a backend opened, and every answer here is in points. The layout engine asks "
     "this layer how tall a paragraph is and never guesses.",
     text_symbols, SITE_COUNT(text_symbols)},
    {"include/cf/layout.h", "Layout", "layout",
     "The pass that turns a declaration into geometry, and a target a program can link by "
     "itself. It allocates from the scratch arena it is handed and reads no clock, no "
     "window and no global, which is what makes it testable against somebody else's "
     "answer instead of this one's.",
     layout_symbols, SITE_COUNT(layout_symbols)},
    {"include/cf/paint.h", "Paint", "paint",
     "The pass that turns geometry into the stream a backend draws: fills, borders, text "
     "lines and images, back to front, each carrying the clip it may not be seen outside. "
     "That list is the paint contract, and the golden tier snapshots it as data.",
     paint_symbols, SITE_COUNT(paint_symbols)},
    {"include/cf/node.h", "The tree and a frame", "node",
     "The largest layer, because it is the one a build lives in: the context, the frame's "
     "stages, keyed state, scroll and motion, dispatch and the interaction state a widget "
     "asks about. Nothing here knows what a window is.",
     node_symbols, SITE_COUNT(node_symbols)},
    {"include/cf/build.h", "Declaration sugar", "build",
     "Every macro here expands to a plain function with the same arguments in the same "
     "order, so a build that outgrows the sugar drops the macro and keeps the code.",
     build_symbols, SITE_COUNT(build_symbols)},
    {"include/cf/event.h", "Events", "event",
     "What one handler receives, and the two things it may ask the dispatch to do about it.",
     event_symbols, SITE_COUNT(event_symbols)},
    {"include/cf/widget.h", "Widgets", "widget",
     "Functions that fill a declaration, not objects that manage a tree. Each returns a "
     "cf_node whose fields it has stated, so the diff, the resolver and the dispatcher see "
     "nothing special, and a widget can be replaced by the twelve lines it stands for.",
     widget_symbols, SITE_COUNT(widget_symbols)},
    {"include/cf/app.h", "The shell", "app",
     "A loop a whole program fits in, and the only part a caller is allowed to not use: the "
     "pipeline below it is callable stage by stage.",
     app_symbols, SITE_COUNT(app_symbols)},
    {"include/cf/backend.h", "Backends", "backend",
     "The struct of function slots a window, a GPU and a font live behind, and its own "
     "version number apart from the API above it. The names below are the SDL backend's "
     "link: they exist when the library is built with it, they are not part of the "
     "installed package's headers a stranger would include, and a build with no browser "
     "answers the web three with nothing.",
     backend_symbols, SITE_COUNT(backend_symbols)},
    {"include/cf/version.h", "Version", "version",
     "One number, stated once in a header, printed by a function and checked by a release "
     "gate against the tag and the package.",
     version_symbols, SITE_COUNT(version_symbols)},
};

/* One layer: its declarations as a panel, and the contract under each in a table that
 * lines the names up on one edge. */
static cf_node api_group_block(cf_ctx *ctx, const site_view *view, const api_group *group) {
    const uint32_t count = group->count;
    const char **lines = cf_alloc(&ctx->frame, (size_t)count * sizeof *lines);
    const char **names = cf_alloc(&ctx->frame, (size_t)count * sizeof *names);
    const char **contracts = cf_alloc(&ctx->frame, (size_t)count * sizeof *contracts);

    if (lines == NULL || names == NULL || contracts == NULL) {
        return (cf_node){.style = cf_style_default()};
    }

    for (uint32_t i = 0u; i < count; ++i) {
        lines[i] = group->symbols[i].declaration;
        names[i] = group->symbols[i].name;
        contracts[i] = group->symbols[i].contract;
    }

    const cf_node body[] = {
        site_aside(ctx, view, group->blurb),
        site_panel(ctx, view, lines, count),
        site_pairs(ctx, view, names, contracts, count, TERM_WIDTH),
    };

    return site_section(ctx, view, group->title, group->slug, body, SITE_COUNT(body));
}

cf_node site_page_api(cf_ctx *ctx, const site_view *view) {
    cf_node *sections = cf_alloc(&ctx->frame, SITE_COUNT(groups) * sizeof *sections);
    cf_node *blocks = cf_alloc(&ctx->frame, (SITE_COUNT(groups) + 2u) * sizeof *blocks);

    if (sections == NULL || blocks == NULL) {
        return site_page(ctx, view, SITE_GROUP_DOCS, "API reference", "api",
                         "Every exported function, grouped the way the headers are.", NULL, 0u);
    }

    for (uint32_t i = 0u; i < SITE_COUNT(groups); ++i) {
        sections[i] = api_group_block(ctx, view, &groups[i]);
    }

    const cf_node out[] = {
        site_link(ctx, view, "to-headers", "The headers themselves", SITE_BLOB "include/cf/"),
        site_link(ctx, view, "to-backend-docs", "Writing a backend",
                  SITE_BLOB "docs/backend-abi.md"),
    };

    /* The layers first, then the ways out: a reader who came for a signature should
     * meet it above the paragraph about how the page is kept honest. */
    for (uint32_t i = 0u; i < SITE_COUNT(groups); ++i) {
        blocks[i] = sections[i];
    }

    /* The page's own honesty clause, on the page it applies to: the gate is named here
     * so a reader who finds a line that has drifted knows what failed to run. */
    blocks[SITE_COUNT(groups)] =
        site_note(ctx, view,
                  "The declarations are the headers' and the sentences under them are this page's. "
                  "CI checks that every name in include/cf appears here and that no name here "
                  "has left the headers, because a page about an API that nobody checks becomes "
                  "a description of the API as it was.");
    blocks[SITE_COUNT(groups) + 1u] = site_strip(ctx, out, SITE_COUNT(out));

    return site_page(ctx, view, SITE_GROUP_DOCS, "API reference", "api",
                     "Every exported function, grouped the way the headers are and stated as "
                     "the headers state them.",
                     blocks, SITE_COUNT(groups) + 2u);
}
