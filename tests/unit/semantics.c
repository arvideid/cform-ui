#include <string.h>

#include "cf/node.h"

#include "cform_test.h"

/* The box this frame laid for a slot, or a rect of no extent where it laid none. A caller outside
 * src/node cannot index rect storage, so the tier reads the answer the frame gives. */
static cf_rect box_of(const cf_ctx *ctx, uint32_t slot) {
    const cf_rect *box = cf_frame_rect(ctx, slot);

    return box != NULL ? *box : (cf_rect){0.0f, 0.0f, 0.0f, 0.0f};
}

/*
 * The two contracts this tier covers. The export is the diagnostic: a bounded, printable dump a
 * developer reads. The visit is the bridge protocol: whole UTF-8, an identity a caller may hold,
 * and the states and actions the frame actually has behind them.
 */
static bool context_of(cf_ctx *ctx) {
    if (!cf_ctx_init(ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return false;
    }

    return true;
}

/* The text the label carries: the walk reads the measure slot's content,
 * whatever fonts stand behind it. */
static const cf_text_content quiet_text = {.text = {.chars = "quiet", .length = 5u}};
static const cf_text_content typed_text = {.text = {.chars = "say hi", .length = 6u}};

static void a_view_walks_as_the_roles_it_kept(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    cf_node page = {.key = CF_NO_NAME};
    page.style = cf_style_default();
    page.style.width = CF_PX(200.0f);
    page.style.height = CF_PX(60.0f);

    cf_node button = {.key = CF_NO_NAME, .role = CF_ROLE_BUTTON};
    cf_node label = {
        .key = CF_NO_NAME, .measure = cf_text_measure, .measure_data = (void *)&quiet_text};
    const cf_node button_children[] = {label};
    button.children = button_children;
    button.child_count = 1u;

    cf_node field = {.key = CF_NO_NAME,
                     .role = CF_ROLE_FIELD,
                     .disabled = true,
                     .measure = cf_text_measure,
                     .measure_data = (void *)&typed_text};

    const cf_node page_children[] = {button, field};
    page.children = page_children;
    page.child_count = 2u;

    cf_frame_begin(&ctx);
    CF_CHECK(cf_diff(&ctx, &page) != CF_NO_LINK, "the diff claims the declaration");
    cf_frame_layout(&ctx, (cf_available){.width = CF_PX(200.0f), .height = CF_PX(60.0f)},
                    (cf_point){0.0f, 0.0f});

    char bytes[256];
    CF_CHECK(cf_semantic_export(&ctx, bytes, sizeof bytes), "the view exports");

    CF_CHECK(bytes != NULL && bytes[0] == 'b' && bytes[1] == 'o' && bytes[2] == 'x',
             "the page opens the dump as a box");
    CF_CHECK(strstr(bytes, "\n  button ") != NULL, "the button sits indented under its page");
    CF_CHECK(strstr(bytes, "'quiet'") != NULL, "the label's text rides its box's line");
    CF_CHECK(strstr(bytes, "\n  field 'say hi'") != NULL, "the field follows with its buffer");
    CF_CHECK(strstr(bytes, " disabled") != NULL, "the field's switch closes its line");

    cf_ctx_free(&ctx);
}

static void an_export_without_a_view_or_room_answers_false(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    char bytes[16];

    CF_CHECK(!cf_semantic_export(&ctx, bytes, sizeof bytes),
             "a context without a view exports nothing");

    cf_frame_begin(&ctx);
    cf_node root = {.key = CF_NO_NAME};
    root.style = cf_style_default();
    CF_CHECK(cf_diff(&ctx, &root) != CF_NO_LINK, "the diff claims the declaration");

    CF_CHECK(!cf_semantic_export(NULL, bytes, sizeof bytes), "no context, no export");
    CF_CHECK(!cf_semantic_export(&ctx, bytes, 0u), "no room, no export");
    CF_CHECK(!cf_semantic_export(&ctx, bytes, 4u),
             "a line the buffer cannot hold is refused, not cut");

    cf_ctx_free(&ctx);
}

/* A chain deep enough that its indent overruns the 160-byte line the walk
 * builds: the old writer trusted snprintf's would-have-written length as the
 * offset, so the cursor ran off the buffer end and handed back a wrapped size.
 * The bounded writer truncates the wide lines instead, so the export survives
 * and every node still gets a line. */
static void a_page_deeper_than_a_line_still_exports(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    enum { DEPTH = 60u };
    cf_node nodes[DEPTH];

    for (uint32_t i = 0u; i < DEPTH; ++i) {
        nodes[i] = (cf_node){.key = CF_NO_NAME};
        nodes[i].style = cf_style_default();

        if (i + 1u < DEPTH) {
            nodes[i].children = &nodes[i + 1u];
            nodes[i].child_count = 1u;
        }
    }

    cf_frame_begin(&ctx);
    CF_CHECK(cf_diff(&ctx, &nodes[0]) != CF_NO_LINK, "the deep page is claimed");
    cf_frame_layout(&ctx, (cf_available){.width = CF_PX(200.0f), .height = CF_PX(200.0f)},
                    (cf_point){0.0f, 0.0f});

    char bytes[16u * 1024u];
    CF_CHECK(cf_semantic_export(&ctx, bytes, sizeof bytes), "a deep page exports without harm");
    CF_CHECK(bytes[0] == 'b' && bytes[1] == 'o' && bytes[2] == 'x',
             "the root still opens as a box");

    cf_ctx_free(&ctx);
}

/* cf_role is an enum but its numeric range is a caller's to send - a stale
 * id, a mismatched build - so role_name bounds-checks and falls back to the
 * box name. A walk past CF_ROLE_LIMIT used to read one entry too far in
 * role_names and could hand back a wild pointer. */
static void a_role_the_theme_does_not_know_reads_as_a_box(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    cf_node root = {.key = CF_NO_NAME};
    root.style = cf_style_default();
    root.style.width = CF_PX(80.0f);
    root.style.height = CF_PX(40.0f);

    cf_node odd = {.key = CF_NO_NAME, .role = (cf_role)(CF_ROLE_LIMIT + 7)};
    odd.style = cf_style_default();
    odd.style.width = CF_PX(80.0f);
    odd.style.height = CF_PX(40.0f);

    const cf_node kids[] = {odd};
    root.children = kids;
    root.child_count = 1u;

    cf_frame_begin(&ctx);
    CF_CHECK(cf_diff(&ctx, &root) != CF_NO_LINK, "the diff claims a wild role");
    cf_frame_layout(&ctx, (cf_available){.width = CF_PX(80.0f), .height = CF_PX(40.0f)},
                    (cf_point){0.0f, 0.0f});

    char bytes[256];
    CF_CHECK(cf_semantic_export(&ctx, bytes, sizeof bytes), "the export survives");
    CF_CHECK(strstr(bytes, "\n  box") != NULL, "an unknown role reads as a box");

    cf_ctx_free(&ctx);
}

/* A label carrying UTF-8 bytes: the export writes them through the excerpt
 * and the line writer, so a caller reading the dump sees the same sequence
 * the build handed the text content. Losing bytes here would be an
 * accessibility bug a screen reader would trip on. */
static void a_text_excerpt_survives_the_utf_8_bytes_the_build_wrote(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    /* "héllo — ok": 6 bytes before the em dash (h, e-acute, l, l, o, space),
     * 3 for the em dash (U+2014), then "ok" and a NUL. */
    static const char utf8[] = "h\xc3\xa9llo \xe2\x80\x94 ok";
    cf_text_content content = {.text = {.chars = utf8, .length = (uint32_t)(sizeof utf8 - 1u)}};

    cf_node root = {.key = CF_NO_NAME};
    root.style = cf_style_default();
    root.style.width = CF_PX(200.0f);
    root.style.height = CF_PX(40.0f);

    cf_node label = {
        .key = CF_NO_NAME, .measure = cf_text_measure, .measure_data = (void *)&content};
    label.style = cf_style_default();

    const cf_node kids[] = {label};
    root.children = kids;
    root.child_count = 1u;

    cf_frame_begin(&ctx);
    CF_CHECK(cf_diff(&ctx, &root) != CF_NO_LINK, "the diff claims the UTF-8 label");
    cf_frame_layout(&ctx, (cf_available){.width = CF_PX(200.0f), .height = CF_PX(40.0f)},
                    (cf_point){0.0f, 0.0f});

    char bytes[256];
    CF_CHECK(cf_semantic_export(&ctx, bytes, sizeof bytes), "the export runs");
    CF_CHECK(strstr(bytes, utf8) != NULL, "the export hands back the same UTF-8 bytes");

    cf_ctx_free(&ctx);
}

/*
 * A visit hands borrowed bytes for the length of the call, so a bridge that means to compare the
 * next frame against this one copies what it was told. These cases do exactly that, which is also
 * the proof that nothing they hold outlives the frame that announced it.
 */
typedef struct said {
    cf_id id;
    uint32_t slot;
    cf_access access;
    cf_announce announce;
    cf_id labelled_by;
    cf_id described_by;
    cf_id controls;
    uint32_t states;
    uint32_t actions;
    cf_rect rect;
    cf_opt_u32 selection_start;
    cf_opt_u32 selection_end;
    char name[64];
    uint32_t name_length;
    char value[64];
    uint32_t value_length;
    char description[64];
    uint32_t description_length;
} said;

#define SPOKEN_MAX 16u
static said spoken[SPOKEN_MAX];
static uint32_t spoken_count;

static void keep_words(char *out, uint32_t *out_length, cf_string text) {
    const uint32_t length = text.chars != NULL && text.length < 63u ? text.length : 0u;

    if (length != 0u) {
        memcpy(out, text.chars, length);
    }

    out[length] = '\0';
    *out_length = length;
}

static void keep_the_visit(const cf_semantic *node, void *user_data) {
    (void)user_data;

    /* A bridge that runs out of room stops copying and keeps walking: the visit has no way to be
     * told to stop, and none is needed - the frame's tree is bounded by the build that wrote it. */
    if (spoken_count >= SPOKEN_MAX) {
        return;
    }

    said *entry = &spoken[spoken_count++];
    *entry = (said){.id = node->id,
                    .slot = node->slot,
                    .access = node->access,
                    .announce = node->announce,
                    .labelled_by = node->labelled_by,
                    .described_by = node->described_by,
                    .controls = node->controls,
                    .states = node->states,
                    .actions = node->actions,
                    .selection_start = node->selection_start,
                    .selection_end = node->selection_end,
                    .rect = node->rect};
    keep_words(entry->name, &entry->name_length, node->name);
    keep_words(entry->value, &entry->value_length, node->value);
    keep_words(entry->description, &entry->description_length, node->description);
}

static void announce(const cf_ctx *ctx) {
    spoken_count = 0u;
    cf_semantic_visit(ctx, keep_the_visit, NULL);
}

static const said *spoken_as(cf_access access) {
    for (uint32_t i = 0u; i < spoken_count; ++i) {
        if (spoken[i].access == access) {
            return &spoken[i];
        }
    }

    return NULL;
}

static bool words_are(const char *bytes, uint32_t length, const char *expected) {
    return length == (uint32_t)strlen(expected) && memcmp(bytes, expected, length) == 0;
}

/* A label is announced by the words it lays, so those words name the row a case is asking about
 * even when several rows carry the same role. */
static const said *spoken_saying(const char *words) {
    for (uint32_t i = 0u; i < spoken_count; ++i) {
        if (words_are(spoken[i].name, spoken[i].name_length, words)) {
            return &spoken[i];
        }
    }

    return NULL;
}

/* A handler that does nothing is still the door its action bit reports. */
static void ask_nothing(cf_event *event, void *user_data) {
    (void)event;
    (void)user_data;
}

static cf_node page_node(void) {
    cf_node page = {.key = CF_NO_NAME};
    page.style = cf_style_default();
    page.style.width = CF_PX(200.0f);
    page.style.height = CF_PX(200.0f);
    return page;
}

static cf_node box_node(cf_access access) {
    cf_node box = {.key = CF_NO_NAME, .access = access};
    box.style = cf_style_default();
    return box;
}

static void state_named(cf_node *box, const char *text) {
    box->name = (cf_string){text, (uint32_t)strlen(text)};
}

static void lay(cf_ctx *ctx) {
    cf_frame_layout(ctx, (cf_available){.width = CF_PX(200.0f), .height = CF_PX(200.0f)},
                    (cf_point){0.0f, 0.0f});
}

static void a_wrapper_is_passed_through_and_a_box_that_means_something_is_announced(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    static const cf_text_content line = {.text = {.chars = "Send", .length = 4u}};
    static const cf_text_content row_line = {.text = {.chars = "row 12", .length = 6u}};
    cf_node label = box_node(CF_ACCESS_LABEL);
    label.measure = cf_text_measure;
    label.measure_data = (void *)&line;

    cf_node button = box_node(CF_ACCESS_BUTTON);
    button.focusable = true;
    button.handlers.activate = ask_nothing;
    const cf_node carried[] = {label};
    button.children = carried;
    button.child_count = 1u;

    /* A group names the run of boxes it holds, so it is announced - with no words of its own, since
     * the words belong to the control inside it. */
    cf_node panel = box_node(CF_ACCESS_GROUP);
    panel.style.width = CF_PX(120.0f);
    panel.style.height = CF_PX(60.0f);

    /* A row of a list is met as one thing, so it borrows its line's words the way a button does -
     * the reading that keeps a list from being announced as a run of nothing. */
    cf_node row_label = box_node(CF_ACCESS_LABEL);
    row_label.measure = cf_text_measure;
    row_label.measure_data = (void *)&row_line;

    cf_node row = box_node(CF_ACCESS_ITEM);
    const cf_node written[] = {row_label};
    row.children = written;
    row.child_count = 1u;

    const cf_node held[] = {button, row};
    panel.children = held;
    panel.child_count = 2u;

    cf_node page = page_node();
    const cf_node kids[] = {panel};
    page.children = kids;
    page.child_count = 1u;

    cf_frame_begin(&ctx);
    CF_CHECK(cf_diff(&ctx, &page) != CF_NO_LINK, "the page is claimed");
    lay(&ctx);

    announce(NULL);
    CF_CHECK(spoken_count == 0u, "no context announces nothing");
    spoken_count = 7u;
    cf_semantic_visit(&ctx, NULL, NULL);
    CF_CHECK(spoken_count == 7u, "no visitor walks nothing");

    announce(&ctx);
    CF_CHECK(spoken_count == 5u, "the group, the two controls and their lines are announced");
    CF_CHECK(spoken[0].access == CF_ACCESS_GROUP,
             "the walk meets the wrapper's content before the page that never spoke");

    const said *said_button = spoken_as(CF_ACCESS_BUTTON);
    const said *said_label = spoken_as(CF_ACCESS_LABEL);
    const said *group = spoken_as(CF_ACCESS_GROUP);
    CF_CHECK(said_button != NULL && said_label != NULL && group != NULL, "each box is announced");
    CF_CHECK(said_button != NULL && words_are(said_button->name, said_button->name_length, "Send"),
             "a button that states no name speaks the label it carries");
    CF_CHECK(said_button != NULL && said_label != NULL &&
                 cf_id_equal(said_button->labelled_by, said_label->id),
             "and says which box it took the words from");
    CF_CHECK(said_label != NULL && words_are(said_label->name, said_label->name_length, "Send"),
             "a label announces its own text");
    CF_CHECK(said_label != NULL && cf_id_is_none(said_label->labelled_by),
             "a label that spoke for itself names no other box");
    CF_CHECK(spoken[3].access == CF_ACCESS_ITEM &&
                 words_are(spoken[3].name, spoken[3].name_length, "row 12"),
             "a row takes the words written in it as its name");
    CF_CHECK(cf_id_equal(spoken[3].labelled_by, spoken[4].id),
             "and names the line it took them from");
    CF_CHECK(said_button != NULL && (said_button->actions & (uint32_t)CF_SEMANTIC_ACTIVATE) != 0u,
             "the box with an activate handler offers activation");
    CF_CHECK(said_button != NULL && (said_button->states & (uint32_t)CF_SEMANTIC_FOCUSABLE) != 0u,
             "the box that takes focus says so");
    CF_CHECK(group != NULL && group->name_length == 0u && group->value_length == 0u,
             "a group that was never named announces no bytes");
    CF_CHECK(group != NULL && group->actions == 0u,
             "a box with no handler and no focus declares no door to open");
    CF_CHECK(group != NULL && group->rect.width == box_of(&ctx, group->slot).width &&
                 group->rect.width == 120.0f,
             "the rect a bridge is handed is the one this frame laid out");

    cf_ctx_free(&ctx);
}

static void a_field_announces_the_words_the_build_named_and_the_bytes_it_holds(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    /* The caret is past the mark: what a bridge is handed is the run, not the order it was made in.
     */
    static const cf_text_content buffer = {.text = {.chars = "ada@lnk", .length = 7u},
                                           .caret = {.present = true, .value = 3u},
                                           .mark = {.present = true, .value = 1u}};
    cf_node field = box_node(CF_ACCESS_TEXT_BOX);
    field.focusable = true;
    field.measure = cf_text_measure;
    field.measure_data = (void *)&buffer;
    state_named(&field, "User name");
    static const char hint[] = "How people find you";
    field.description = (cf_string){hint, (uint32_t)(sizeof hint - 1u)};

    cf_node page = page_node();
    const cf_node kids[] = {field};
    page.children = kids;
    page.child_count = 1u;

    cf_frame_begin(&ctx);
    CF_CHECK(cf_diff(&ctx, &page) != CF_NO_LINK, "the field is claimed");
    lay(&ctx);
    announce(&ctx);

    const said *box = spoken_as(CF_ACCESS_TEXT_BOX);
    CF_CHECK(spoken_count == 1u, "a field with no label inside it announces one box");
    CF_CHECK(box != NULL && words_are(box->name, box->name_length, "User name"),
             "the words the build stated are the words announced");
    CF_CHECK(box != NULL && words_are(box->value, box->value_length, "ada@lnk"),
             "the bytes the box lays are its value");
    CF_CHECK(box != NULL && words_are(box->description, box->description_length, hint),
             "the description the build gave rides along");
    CF_CHECK(box != NULL && (box->states & (uint32_t)CF_SEMANTIC_EDITABLE) != 0u,
             "a text box says its bytes are meant to be typed into");
    CF_CHECK(box != NULL && (box->actions & (uint32_t)CF_SEMANTIC_SET_VALUE) != 0u &&
                 (box->actions & (uint32_t)CF_SEMANTIC_SET_FOCUS) != 0u,
             "the field offers its value and its focus as doors a bridge may use");
    CF_CHECK(box != NULL && cf_id_is_none(box->labelled_by),
             "a stated name borrows nothing, so no label is named beside it");
    CF_CHECK(box != NULL && box->selection_start.present && box->selection_end.present &&
                 box->selection_start.value == 1u && box->selection_end.value == 3u,
             "the bytes a person is holding are exported as a run, low end first");

    cf_ctx_free(&ctx);
}

static void a_closed_dialog_announces_nothing(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    cf_node hidden_button = box_node(CF_ACCESS_BUTTON);
    state_named(&hidden_button, "OK");

    cf_node dialog = box_node(CF_ACCESS_GROUP);
    dialog.style.display = CF_DISPLAY_NONE;
    const cf_node held[] = {hidden_button};
    dialog.children = held;
    dialog.child_count = 1u;

    cf_node open_button = box_node(CF_ACCESS_BUTTON);
    state_named(&open_button, "Go");

    cf_node page = page_node();
    const cf_node kids[] = {dialog, open_button};
    page.children = kids;
    page.child_count = 2u;

    cf_frame_begin(&ctx);
    CF_CHECK(cf_diff(&ctx, &page) != CF_NO_LINK, "the page is claimed");
    lay(&ctx);
    announce(&ctx);

    const said *box = spoken_as(CF_ACCESS_BUTTON);
    CF_CHECK(spoken_count == 1u, "the closed dialog and everything in it announces nothing");
    CF_CHECK(box != NULL && words_are(box->name, box->name_length, "Go"),
             "the control still on the page is the one announced");

    cf_ctx_free(&ctx);
}

static void a_picture_with_no_words_is_announced_unnamed(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    /* No measure and no label beneath it: the frame has nothing to say for this box, and saying so
     * with an empty name is the bridge's business rather than the walk's invention. */
    cf_node picture = box_node(CF_ACCESS_IMAGE);

    cf_node page = page_node();
    const cf_node kids[] = {picture};
    page.children = kids;
    page.child_count = 1u;

    cf_frame_begin(&ctx);
    CF_CHECK(cf_diff(&ctx, &page) != CF_NO_LINK, "the picture is claimed");
    lay(&ctx);
    announce(&ctx);

    const said *box = spoken_as(CF_ACCESS_IMAGE);
    CF_CHECK(spoken_count == 1u, "an unnamed picture is still announced");
    CF_CHECK(box != NULL && box->name_length == 0u && box->value_length == 0u,
             "a box that lays no text has neither a name nor a value");
    CF_CHECK(box != NULL && cf_id_is_none(box->labelled_by), "and no label is named for it");
    CF_CHECK(box != NULL && box->actions == 0u, "and it offers nothing to do to it");
    CF_CHECK(box != NULL && !box->selection_start.present && !box->selection_end.present,
             "a box that lays no caret states no selection rather than a zero-length one");

    cf_ctx_free(&ctx);
}

static void an_untouched_box_keeps_its_id_while_its_words_change(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    /* One buffer, edited between the frames: the ordinary way a build assembles a label. */
    char words[32] = "Save";
    cf_node button = box_node(CF_ACCESS_BUTTON);
    button.name = (cf_string){words, 4u};

    cf_node page = page_node();
    cf_node kids[1];
    kids[0] = button;
    page.children = kids;
    page.child_count = 1u;

    cf_frame_begin(&ctx);
    CF_CHECK(cf_diff(&ctx, &page) != CF_NO_LINK, "the first frame claims the button");
    lay(&ctx);
    announce(&ctx);

    const said *first = spoken_as(CF_ACCESS_BUTTON);
    CF_CHECK(first != NULL && spoken_count == 1u, "one box is announced");
    const cf_id kept = first != NULL ? first->id : CF_NO_ID;
    const uint32_t kept_slot = first != NULL ? first->slot : CF_NO_LINK;

    memcpy(words, "Save now", 9u);
    button.name = (cf_string){words, 8u};
    kids[0] = button;

    cf_frame_begin(&ctx);
    CF_CHECK(cf_diff(&ctx, &page) != CF_NO_LINK, "the second frame claims the button");
    /* A steady pointer is not a steady sentence: were the bytes not compared, a build that edits
     * its own name buffer would announce new words from a node the frame called unchanged. */
    CF_CHECK(cf_dirty(&ctx, kept_slot), "words edited in place read as a change to the box");
    lay(&ctx);
    announce(&ctx);

    const said *again = spoken_as(CF_ACCESS_BUTTON);
    CF_CHECK(again != NULL && cf_id_equal(kept, again->id),
             "the box keeps the identity a bridge is holding while its words change");
    CF_CHECK(again != NULL && again->slot == kept_slot,
             "and keeps the slot this frame dispatches on");
    CF_CHECK(again != NULL && words_are(again->name, again->name_length, "Save now"),
             "and announces what it now says");

    cf_ctx_free(&ctx);
}

static void a_retired_box_leaves_the_walk_and_its_slot_answers_for_nobody(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    cf_node button = box_node(CF_ACCESS_BUTTON);
    state_named(&button, "Save");

    cf_node page = page_node();
    const cf_node kids[] = {button};
    page.children = kids;
    page.child_count = 1u;

    cf_frame_begin(&ctx);
    CF_CHECK(cf_diff(&ctx, &page) != CF_NO_LINK, "the first frame claims the button");
    lay(&ctx);
    announce(&ctx);

    const said *first = spoken_as(CF_ACCESS_BUTTON);
    const cf_id kept = first != NULL ? first->id : CF_NO_ID;

    /* The build stops declaring it - the dialog closed, the row scrolled out. */
    cf_node bare = page_node();
    cf_frame_begin(&ctx);
    CF_CHECK(cf_diff(&ctx, &bare) != CF_NO_LINK,
             "the second frame declares a page with nothing on it");
    lay(&ctx);
    announce(&ctx);
    CF_CHECK(spoken_count == 0u, "a box the frame did not declare leaves the walk");

    /* A different control, met again at the page's first place: the slot the store had to give it
     * may well be the one the retired button held, and a bridge must still learn that its own box
     * is gone rather than be told about whoever moved in. */
    cf_node next = box_node(CF_ACCESS_BUTTON);
    state_named(&next, "Close");
    cf_node page_again = page_node();
    const cf_node new_kids[] = {next};
    page_again.children = new_kids;
    page_again.child_count = 1u;

    cf_frame_begin(&ctx);
    CF_CHECK(cf_diff(&ctx, &page_again) != CF_NO_LINK, "the third frame claims another button");
    lay(&ctx);
    announce(&ctx);

    const said *box = spoken_as(CF_ACCESS_BUTTON);
    CF_CHECK(box != NULL && spoken_count == 1u, "the new control is announced");
    CF_CHECK(box != NULL && !cf_id_equal(kept, box->id),
             "the box that took the slot is not the box whose id the bridge kept");

    cf_ctx_free(&ctx);
}

static void the_box_holding_the_keyboard_is_the_one_announced_focused(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    cf_node first = box_node(CF_ACCESS_BUTTON);
    state_named(&first, "One");
    first.focusable = true;
    /* A box that answers the pointer is the one a press books on, so these have to: the states the
     * walk reports are the ones the dispatcher keeps, not a second set the semantics own. */
    first.handlers.pointer = ask_nothing;
    first.style.width = CF_PX(60.0f);
    first.style.height = CF_PX(40.0f);

    cf_node second = box_node(CF_ACCESS_BUTTON);
    state_named(&second, "Two");
    second.focusable = true;
    second.handlers.pointer = ask_nothing;
    second.style.width = CF_PX(60.0f);
    second.style.height = CF_PX(40.0f);

    /* Reachable is a claim the box makes, so a box that never made it is announced without it and
     * refuses the ask - the same rule Tab walks. */
    cf_node plain = box_node(CF_ACCESS_GROUP);
    state_named(&plain, "Three");
    plain.style.width = CF_PX(60.0f);
    plain.style.height = CF_PX(40.0f);

    cf_node page = page_node();
    const cf_node kids[] = {first, second, plain};
    page.children = kids;
    page.child_count = 3u;

    cf_frame_begin(&ctx);
    CF_CHECK(cf_diff(&ctx, &page) != CF_NO_LINK, "the page is claimed");
    lay(&ctx);
    announce(&ctx);
    CF_CHECK(spoken_count == 3u, "three named boxes are announced");

    /* Read the rows out once, both ways: `announce` rewrites the collected rows, so asking later
     * means naming the box by the reference a bridge would keep and reading this frame's rect by
     * the place the arrays hold it at. */
    const uint32_t first_slot = spoken[0].slot;
    const uint32_t second_slot = spoken[1].slot;
    const cf_id second_id = spoken[1].id;
    const cf_id third_id = spoken[2].id;
    CF_CHECK((spoken[0].states & (uint32_t)CF_SEMANTIC_FOCUSED) == 0u,
             "a page nobody has tabbed through focuses nothing");
    CF_CHECK((spoken[2].states & (uint32_t)CF_SEMANTIC_FOCUSABLE) == 0u &&
                 (spoken[2].actions & (uint32_t)CF_SEMANTIC_SET_FOCUS) == 0u,
             "a box that takes no focus says so and offers no ask to move it");

    cf_focus(&ctx, second_id);
    announce(&ctx);
    CF_CHECK((spoken[1].states & (uint32_t)CF_SEMANTIC_FOCUSED) != 0u,
             "the box the keyboard stands on is announced focused");
    CF_CHECK((spoken[0].states & (uint32_t)CF_SEMANTIC_FOCUSED) == 0u,
             "and the one it left is not");

    cf_focus(&ctx, third_id);
    announce(&ctx);
    CF_CHECK((spoken[1].states & (uint32_t)CF_SEMANTIC_FOCUSED) != 0u,
             "a box that takes no focus cannot be moved onto, so the keyboard stays");

    /* The press and the release are states the frame knows and a bridge cannot guess: the box under
     * a held pointer is the one wearing the pressed paint, and the hand leaving it takes that paint
     * and the resting tone with it. */
    const cf_point on_first = {box_of(&ctx, first_slot).x + 10.0f,
                               box_of(&ctx, first_slot).y + 10.0f};
    const cf_point on_second = {box_of(&ctx, second_slot).x + 10.0f,
                                box_of(&ctx, second_slot).y + 10.0f};
    cf_input(&ctx,
             (cf_event){.type = CF_EVENT_POINTER_DOWN, .target = first_slot, .position = on_first});
    announce(&ctx);
    CF_CHECK((spoken[0].states & (uint32_t)CF_SEMANTIC_PRESSED) != 0u,
             "a held press is announced as a state");
    CF_CHECK((spoken[1].states & (uint32_t)CF_SEMANTIC_PRESSED) == 0u,
             "and only on the box the hand is on");

    cf_input(&ctx,
             (cf_event){.type = CF_EVENT_POINTER_UP, .target = first_slot, .position = on_second});
    announce(&ctx);
    CF_CHECK((spoken[0].states & (uint32_t)CF_SEMANTIC_PRESSED) == 0u,
             "letting go ends the press the walk announces");
    CF_CHECK((spoken[1].states & (uint32_t)CF_SEMANTIC_HOVERED) != 0u,
             "and the pointer that came to rest on another box is announced there");

    cf_ctx_free(&ctx);
}

static void a_name_keeps_every_byte_of_the_text_the_build_wrote(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    /* Fifteen of a four-byte rune, then a tab and a quote: 62 bytes, past the bound the diagnostic
     * excerpt cuts at, and holding the two bytes that one replaces or drops. The protocol hands the
     * build's own sequence over, whole. */
    char words[80];
    uint32_t length = 0u;

    for (uint32_t i = 0u; i < 15u; ++i) {
        memcpy(words + length, "\xf0\x9f\x99\x82", 4u);
        length += 4u;
    }

    words[length++] = '\t';
    words[length++] = '\'';

    cf_node button = box_node(CF_ACCESS_BUTTON);
    button.name = (cf_string){words, length};

    cf_node page = page_node();
    const cf_node kids[] = {button};
    page.children = kids;
    page.child_count = 1u;

    cf_frame_begin(&ctx);
    CF_CHECK(cf_diff(&ctx, &page) != CF_NO_LINK, "the button is claimed");
    lay(&ctx);
    announce(&ctx);

    const said *box = spoken_as(CF_ACCESS_BUTTON);
    CF_CHECK(box != NULL && box->name_length == length,
             "the announced name is as long as the bytes the build wrote");
    CF_CHECK(box != NULL && memcmp(box->name, words, length) == 0,
             "and holds those bytes: no rune cut, no control byte replaced");

    cf_ctx_free(&ctx);
}

/* A live region marks an area of the page rather than a sentence: every box the walk reports inside
 * it carries the level, including the ones that stated nothing of their own, and the box that opens
 * the region is covered by it as much as any line inside. */
static void a_live_region_covers_every_box_inside_it(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    static const cf_text_content later_text = {.text = {.chars = "later", .length = 5u}};

    cf_node line = box_node(CF_ACCESS_LABEL);
    line.measure = cf_text_measure;
    line.measure_data = (void *)&later_text;

    cf_node control = box_node(CF_ACCESS_BUTTON);
    state_named(&control, "Save");

    const cf_node region_kids[] = {line, control};
    cf_node region = box_node(CF_ACCESS_GROUP);
    region.announce = CF_ANNOUNCE_POLITE;
    region.children = region_kids;
    region.child_count = 2u;

    cf_node elsewhere = box_node(CF_ACCESS_LABEL);
    elsewhere.measure = cf_text_measure;
    elsewhere.measure_data = (void *)&typed_text;

    const cf_node page_kids[] = {region, elsewhere};
    cf_node page = page_node();
    page.children = page_kids;
    page.child_count = 2u;

    cf_frame_begin(&ctx);
    CF_CHECK(cf_diff(&ctx, &page) != CF_NO_LINK, "the diff claims the page");
    lay(&ctx);
    announce(&ctx);

    const said *held = spoken_as(CF_ACCESS_BUTTON);
    const said *outside = spoken_saying("say hi");
    CF_CHECK(spoken_as(CF_ACCESS_GROUP) != NULL &&
                 spoken_as(CF_ACCESS_GROUP)->announce == CF_ANNOUNCE_POLITE,
             "the region is announced at the level it states, the same as anything inside it");
    CF_CHECK(held != NULL && held->announce == CF_ANNOUNCE_POLITE,
             "the control inside the region is covered by it, though it stated no level");
    CF_CHECK(spoken_saying("later") != NULL &&
                 spoken_saying("later")->announce == CF_ANNOUNCE_POLITE,
             "and so is the line beside it");
    CF_CHECK(held != NULL && outside != NULL && outside->announce == CF_ANNOUNCE_NONE,
             "a box outside every region is announced once and not watched for news");

    cf_ctx_free(&ctx);
}

/* The nearest mark wins, and what a box declares reaches its own children: one status area can hold
 * a line that interrupts while the lines beside it keep waiting their turn. */
static void the_urgent_line_inside_a_polite_region_stays_urgent(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    static const cf_text_content later_text = {.text = {.chars = "later", .length = 5u}};
    static const cf_text_content now_text = {.text = {.chars = "now", .length = 3u}};

    cf_node waiting = box_node(CF_ACCESS_LABEL);
    waiting.measure = cf_text_measure;
    waiting.measure_data = (void *)&later_text;

    cf_node word = box_node(CF_ACCESS_LABEL);
    word.measure = cf_text_measure;
    word.measure_data = (void *)&now_text;

    const cf_node shout_kids[] = {word};
    cf_node shout = box_node(CF_ACCESS_GROUP);
    /* Named, because both boxes here are groups and the row a case asks about has to be the one it
     * means: the walk reports the outer region first. */
    state_named(&shout, "shout");
    shout.announce = CF_ANNOUNCE_ASSERTIVE;
    shout.children = shout_kids;
    shout.child_count = 1u;

    const cf_node region_kids[] = {waiting, shout};
    cf_node region = box_node(CF_ACCESS_GROUP);
    region.announce = CF_ANNOUNCE_POLITE;
    region.children = region_kids;
    region.child_count = 2u;

    const cf_node page_kids[] = {region};
    cf_node page = page_node();
    page.children = page_kids;
    page.child_count = 1u;

    cf_frame_begin(&ctx);
    CF_CHECK(cf_diff(&ctx, &page) != CF_NO_LINK, "the diff claims the page");
    lay(&ctx);
    announce(&ctx);

    const said *urgent = spoken_saying("shout");
    CF_CHECK(urgent != NULL && urgent->announce == CF_ANNOUNCE_ASSERTIVE,
             "the box that stated a level keeps it over the one it sits inside");
    CF_CHECK(spoken_as(CF_ACCESS_GROUP) != NULL &&
                 spoken_as(CF_ACCESS_GROUP)->announce == CF_ANNOUNCE_POLITE,
             "and the region holding it is still announced at its own level");
    CF_CHECK(spoken_saying("now") != NULL &&
                 spoken_saying("now")->announce == CF_ANNOUNCE_ASSERTIVE,
             "and hands it down to the words inside it");
    CF_CHECK(spoken_saying("later") != NULL &&
                 spoken_saying("later")->announce == CF_ANNOUNCE_POLITE,
             "while its sibling stays under the region that covers them both");

    cf_ctx_free(&ctx);
}

/*
 * A region that changes its mind is a change to the box: the level rides the diff's signature, so a
 * build that flips a status area from polite to urgent with the same words and the same style still
 * reads as a changed node, and the frame after reports the new urgency.
 *
 * The line inside it changes nothing about itself - the walk hands the level down as it goes, so a
 * box the diff called unchanged is covered by a region it never had to hear about.
 */
static void a_region_that_becomes_urgent_reads_as_a_changed_box(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    static const cf_text_content later_text = {.text = {.chars = "later", .length = 5u}};

    cf_node line = box_node(CF_ACCESS_LABEL);
    line.measure = cf_text_measure;
    line.measure_data = (void *)&later_text;

    const cf_node region_kids[] = {line};
    cf_node region = box_node(CF_ACCESS_GROUP);
    region.announce = CF_ANNOUNCE_POLITE;
    region.children = region_kids;
    region.child_count = 1u;

    const cf_node page_kids[] = {region};
    cf_node page = page_node();
    page.children = page_kids;
    page.child_count = 1u;

    cf_frame_begin(&ctx);
    CF_CHECK(cf_diff(&ctx, &page) != CF_NO_LINK, "the first frame claims the region");
    lay(&ctx);
    announce(&ctx);

    const said *held = spoken_as(CF_ACCESS_GROUP);
    const cf_id kept = held != NULL ? held->id : CF_NO_ID;
    const uint32_t kept_slot = held != NULL ? held->slot : CF_NO_LINK;
    const uint32_t line_slot =
        spoken_saying("later") != NULL ? spoken_saying("later")->slot : CF_NO_LINK;

    region.announce = CF_ANNOUNCE_ASSERTIVE;
    const cf_node again_kids[] = {line};
    region.children = again_kids;
    const cf_node again_page_kids[] = {region};
    page.children = again_page_kids;

    cf_frame_begin(&ctx);
    CF_CHECK(cf_diff(&ctx, &page) != CF_NO_LINK, "the second frame claims it again");
    CF_CHECK(cf_dirty(&ctx, kept_slot),
             "a level stated differently is a change the diff sees for itself");
    CF_CHECK(!cf_dirty(&ctx, line_slot),
             "and it is the region that changed, not the line it covers");
    lay(&ctx);
    announce(&ctx);

    const said *urgent = spoken_as(CF_ACCESS_GROUP);
    const said *words = spoken_saying("later");
    CF_CHECK(urgent != NULL && cf_id_equal(kept, urgent->id),
             "the region keeps the identity a bridge is holding");
    CF_CHECK(urgent != NULL && urgent->announce == CF_ANNOUNCE_ASSERTIVE,
             "and says what it now means");
    CF_CHECK(words != NULL && words->announce == CF_ANNOUNCE_ASSERTIVE,
             "the line inside it is heard at the new level, having changed nothing");

    cf_ctx_free(&ctx);
}

/*
 * The relation a build states rather than a walk guesses: the key the other box was given, resolved
 * into an identity a bridge may hold. Two directions, because "whose words explain this box" and
 * "which box does this one change" are different asks - a field's caption says nothing about what a
 * press will bring about.
 */
static void a_relation_names_the_box_the_build_keyed(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    const cf_name caption_key = cf_ctx_key(&ctx, "signature-caption");
    const cf_name note_key = cf_ctx_key(&ctx, "signature-note");

    cf_node field = box_node(CF_ACCESS_TEXT_BOX);
    state_named(&field, "Signature");
    field.described_by = caption_key;

    cf_node caption = box_node(CF_ACCESS_LABEL);
    state_named(&caption, "Letters only");
    caption.key = caption_key;

    cf_node opener = box_node(CF_ACCESS_BUTTON);
    state_named(&opener, "Show note");
    opener.controls = note_key;

    cf_node note = box_node(CF_ACCESS_LABEL);
    state_named(&note, "the note");
    note.key = note_key;

    const cf_node page_kids[] = {field, caption, opener, note};
    /* The page announces itself and states a relation of its own, because the root is claimed by a
     * different branch of the diff than a child is: a relation booked for a box that holds the page
     * has to be read back as well as one written under it. */
    cf_node page = page_node();
    page.access = CF_ACCESS_GROUP;
    page.described_by = note_key;
    page.controls = caption_key;
    page.children = page_kids;
    page.child_count = 4u;

    cf_frame_begin(&ctx);
    CF_CHECK(cf_diff(&ctx, &page) != CF_NO_LINK, "the frame claims the page");
    lay(&ctx);
    announce(&ctx);
    CF_CHECK(spoken_count == 5u, "five named boxes are walked");

    const said *box = spoken_as(CF_ACCESS_TEXT_BOX);
    const said *words = spoken_saying("Letters only");
    CF_CHECK(box != NULL && words != NULL && cf_id_equal(box->described_by, words->id),
             "the field is described by the caption its build named");

    const said *button = spoken_as(CF_ACCESS_BUTTON);
    const said *held = spoken_saying("the note");
    CF_CHECK(button != NULL && held != NULL && cf_id_equal(button->controls, held->id),
             "and the button controls the box its build named");
    CF_CHECK(button != NULL && cf_id_is_none(button->described_by),
             "the two directions stay separate - one does not borrow the other's name");

    const said *group = spoken_as(CF_ACCESS_GROUP);
    CF_CHECK(group != NULL && cf_id_equal(group->described_by, held != NULL ? held->id : CF_NO_ID),
             "a relation stated by the box holding the page is read back the same way");
    CF_CHECK(group != NULL && cf_id_equal(group->controls, words != NULL ? words->id : CF_NO_ID),
             "and so is the direction it states beside that one");

    cf_ctx_free(&ctx);
}

/*
 * A relation is a name resolved at the read rather than a pointer kept behind it, so the box it
 * points at may not be in the frame at all. The honest answer then is no box: a bridge told the
 * button controls the note that was there a moment ago would read words that are gone, and a dialog
 * that has not been opened yet is exactly this case.
 */
static void a_relation_to_a_box_that_is_not_declared_yet_answers_none(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    const cf_name note_key = cf_ctx_key(&ctx, "note");

    cf_node opener = box_node(CF_ACCESS_BUTTON);
    state_named(&opener, "Show note");
    opener.controls = note_key;

    const cf_node first_kids[] = {opener};
    cf_node page = page_node();
    page.children = first_kids;
    page.child_count = 1u;

    cf_frame_begin(&ctx);
    CF_CHECK(cf_diff(&ctx, &page) != CF_NO_LINK, "the first frame declares no note");
    lay(&ctx);
    announce(&ctx);

    const said *button = spoken_as(CF_ACCESS_BUTTON);
    CF_CHECK(button != NULL && cf_id_is_none(button->controls),
             "a button controlling a box the page does not show answers no box");
    const cf_id kept = button != NULL ? button->id : CF_NO_ID;

    cf_node note = box_node(CF_ACCESS_LABEL);
    state_named(&note, "the note");
    note.key = note_key;

    const cf_node again_kids[] = {opener, note};
    cf_node again = page_node();
    again.children = again_kids;
    again.child_count = 2u;

    cf_frame_begin(&ctx);
    CF_CHECK(cf_diff(&ctx, &again) != CF_NO_LINK, "the second frame brings the note in");
    lay(&ctx);
    announce(&ctx);

    const said *opened = spoken_as(CF_ACCESS_BUTTON);
    const said *words = spoken_saying("the note");
    CF_CHECK(opened != NULL && cf_id_equal(kept, opened->id),
             "naming a box that joined the page leaves the box that named it itself");
    CF_CHECK(opened != NULL && words != NULL && cf_id_equal(opened->controls, words->id),
             "and the relation now answers the note's own identity");

    cf_ctx_free(&ctx);
}

/*
 * A key is an identity rather than a promise about a relation: the box carrying it has to be one
 * the reading includes. A wrapper nobody announced is passed over the way the walk passes it when
 * asked about it directly, and a box behind one the page switched off is not in the tree a bridge
 * walks.
 */
static void a_key_held_by_a_wrapper_or_a_switched_off_box_is_no_relation(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    const cf_name wrapper_key = cf_ctx_key(&ctx, "wrapper");
    const cf_name note_key = cf_ctx_key(&ctx, "gone-note");

    /* Announced by nothing: the box is in the tree and out of the reading. */
    cf_node wrapper = box_node(CF_ACCESS_NONE);
    wrapper.key = wrapper_key;

    cf_node field = box_node(CF_ACCESS_TEXT_BOX);
    state_named(&field, "Signature");
    field.described_by = wrapper_key;

    /* The note is declared, and the box holding it is not laid out at all. */
    cf_node note = box_node(CF_ACCESS_LABEL);
    state_named(&note, "the note");
    note.key = note_key;

    const cf_node closed_kids[] = {note};
    cf_node closed = box_node(CF_ACCESS_NONE);
    closed.style.display = CF_DISPLAY_NONE;
    closed.children = closed_kids;
    closed.child_count = 1u;

    cf_node opener = box_node(CF_ACCESS_BUTTON);
    state_named(&opener, "Show note");
    opener.controls = note_key;

    const cf_node page_kids[] = {wrapper, field, closed, opener};
    cf_node page = page_node();
    page.children = page_kids;
    page.child_count = 4u;

    cf_frame_begin(&ctx);
    CF_CHECK(cf_diff(&ctx, &page) != CF_NO_LINK, "the frame claims the page");
    lay(&ctx);
    announce(&ctx);
    CF_CHECK(spoken_saying("the note") == NULL, "the switched-off note is not in the reading");

    const said *box = spoken_as(CF_ACCESS_TEXT_BOX);
    CF_CHECK(box != NULL && cf_id_is_none(box->described_by),
             "a caption the page announces nowhere is no box to be described by");

    const said *button = spoken_as(CF_ACCESS_BUTTON);
    CF_CHECK(button != NULL && cf_id_is_none(button->controls),
             "and a note behind a box that lays nothing is no box to control");

    cf_ctx_free(&ctx);
}

/*
 * Pointing at another box is a change the diff has to see for itself. A build that moves a relation
 * while leaving the style and the words alone still sends a bridge a different box to read, so the
 * name rides the signature the same way the announced words do.
 */
static void naming_another_box_is_a_change_the_diff_sees(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    const cf_name first_key = cf_ctx_key(&ctx, "first-note");
    const cf_name second_key = cf_ctx_key(&ctx, "second-note");

    cf_node first = box_node(CF_ACCESS_LABEL);
    state_named(&first, "first note");
    first.key = first_key;

    cf_node second = box_node(CF_ACCESS_LABEL);
    state_named(&second, "second note");
    second.key = second_key;

    cf_node opener = box_node(CF_ACCESS_BUTTON);
    state_named(&opener, "Show note");
    opener.controls = first_key;

    cf_node field = box_node(CF_ACCESS_TEXT_BOX);
    state_named(&field, "Signature");
    field.described_by = first_key;

    const cf_node page_kids[] = {opener, field, first, second};
    cf_node page = page_node();
    page.children = page_kids;
    page.child_count = 4u;

    cf_frame_begin(&ctx);
    CF_CHECK(cf_diff(&ctx, &page) != CF_NO_LINK, "the first frame claims the page");
    lay(&ctx);
    announce(&ctx);

    const said *button = spoken_as(CF_ACCESS_BUTTON);
    const said *box = spoken_as(CF_ACCESS_TEXT_BOX);
    const uint32_t opener_slot = button != NULL ? button->slot : CF_NO_LINK;
    const uint32_t field_slot = box != NULL ? box->slot : CF_NO_LINK;
    const cf_id first_id =
        spoken_saying("first note") != NULL ? spoken_saying("first note")->id : CF_NO_ID;
    CF_CHECK(button != NULL && cf_id_equal(button->controls, first_id),
             "the button starts out controlling the first note");
    CF_CHECK(box != NULL && cf_id_equal(box->described_by, first_id),
             "and the field is described by the same box");

    /* Only the field's name moves. The two relations are separate claims, so each has to be a
     * reason of its own to rebuild the box that made it - a hashed struct that carried one and not
     * the other would leave a bridge watching the wrong box. */
    field.described_by = second_key;
    const cf_node again_kids[] = {opener, field, first, second};
    page.children = again_kids;

    cf_frame_begin(&ctx);
    CF_CHECK(cf_diff(&ctx, &page) != CF_NO_LINK, "the second frame claims it again");
    CF_CHECK(cf_dirty(&ctx, field_slot),
             "naming another box to explain itself is a change the diff sees without being told");
    CF_CHECK(!cf_dirty(&ctx, opener_slot), "and the box whose relation stayed put stays clean");
    lay(&ctx);
    announce(&ctx);

    const said *moved = spoken_as(CF_ACCESS_TEXT_BOX);
    const said *other = spoken_saying("second note");
    CF_CHECK(moved != NULL && other != NULL && cf_id_equal(moved->described_by, other->id),
             "the walk hands the bridge the box the build means now");
    const said *steady = spoken_as(CF_ACCESS_BUTTON);
    CF_CHECK(steady != NULL && cf_id_equal(steady->controls, first_id),
             "while the button still controls the note it was stated against");

    /* And the other way round, one frame later: the two fields ride the signature on their own, so
     * a build that moves the one a press acts on is dirty without the one that explains a box being
     * dirty with it. */
    opener.controls = second_key;
    const cf_node third_kids[] = {opener, field, first, second};
    page.children = third_kids;

    cf_frame_begin(&ctx);
    CF_CHECK(cf_diff(&ctx, &page) != CF_NO_LINK, "the third frame claims it once more");
    CF_CHECK(cf_dirty(&ctx, opener_slot), "the box a press controls is a claim of its own");
    CF_CHECK(!cf_dirty(&ctx, field_slot),
             "and the field that went on naming the same box stays clean");
    lay(&ctx);
    announce(&ctx);

    const said *latest = spoken_as(CF_ACCESS_BUTTON);
    CF_CHECK(latest != NULL && other != NULL && cf_id_equal(latest->controls, other->id),
             "and the button moves to the note the build names");

    cf_ctx_free(&ctx);
}

int main(void) {
    printf("semantics\n");
    CF_RUN(a_view_walks_as_the_roles_it_kept);
    CF_RUN(an_export_without_a_view_or_room_answers_false);
    CF_RUN(a_page_deeper_than_a_line_still_exports);
    CF_RUN(a_role_the_theme_does_not_know_reads_as_a_box);
    CF_RUN(a_text_excerpt_survives_the_utf_8_bytes_the_build_wrote);
    CF_RUN(a_wrapper_is_passed_through_and_a_box_that_means_something_is_announced);
    CF_RUN(a_field_announces_the_words_the_build_named_and_the_bytes_it_holds);
    CF_RUN(a_closed_dialog_announces_nothing);
    CF_RUN(a_picture_with_no_words_is_announced_unnamed);
    CF_RUN(an_untouched_box_keeps_its_id_while_its_words_change);
    CF_RUN(a_retired_box_leaves_the_walk_and_its_slot_answers_for_nobody);
    CF_RUN(the_box_holding_the_keyboard_is_the_one_announced_focused);
    CF_RUN(a_name_keeps_every_byte_of_the_text_the_build_wrote);
    CF_RUN(a_live_region_covers_every_box_inside_it);
    CF_RUN(the_urgent_line_inside_a_polite_region_stays_urgent);
    CF_RUN(a_region_that_becomes_urgent_reads_as_a_changed_box);
    CF_RUN(a_relation_names_the_box_the_build_keyed);
    CF_RUN(a_relation_to_a_box_that_is_not_declared_yet_answers_none);
    CF_RUN(a_key_held_by_a_wrapper_or_a_switched_off_box_is_no_relation);
    CF_RUN(naming_another_box_is_a_change_the_diff_sees);

    return CF_REPORT();
}
