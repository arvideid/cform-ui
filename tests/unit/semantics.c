#include <string.h>

#include "cf/node.h"

#include "cform_test.h"

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
 * box name. A walk past CF_ROLE_COUNT used to read one entry too far in
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

    cf_node odd = {.key = CF_NO_NAME, .role = (cf_role)(CF_ROLE_COUNT + 7)};
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
    cf_id labelled_by;
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
                    .labelled_by = node->labelled_by,
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
    CF_CHECK(group != NULL && group->rect.width == ctx.rects[group->slot].width &&
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

    /* Read the slots out once: `announce` rewrites the collected rows, and a bridge that meant to
     * hold a slot would be holding a number this frame is free to hand someone else. */
    const uint32_t first_slot = spoken[0].slot;
    const uint32_t second_slot = spoken[1].slot;
    const uint32_t third_slot = spoken[2].slot;
    CF_CHECK((spoken[0].states & (uint32_t)CF_SEMANTIC_FOCUSED) == 0u,
             "a page nobody has tabbed through focuses nothing");
    CF_CHECK((spoken[2].states & (uint32_t)CF_SEMANTIC_FOCUSABLE) == 0u &&
                 (spoken[2].actions & (uint32_t)CF_SEMANTIC_SET_FOCUS) == 0u,
             "a box that takes no focus says so and offers no ask to move it");

    cf_focus(&ctx, second_slot);
    announce(&ctx);
    CF_CHECK((spoken[1].states & (uint32_t)CF_SEMANTIC_FOCUSED) != 0u,
             "the box the keyboard stands on is announced focused");
    CF_CHECK((spoken[0].states & (uint32_t)CF_SEMANTIC_FOCUSED) == 0u,
             "and the one it left is not");

    cf_focus(&ctx, third_slot);
    announce(&ctx);
    CF_CHECK((spoken[1].states & (uint32_t)CF_SEMANTIC_FOCUSED) != 0u,
             "a box that takes no focus cannot be moved onto, so the keyboard stays");

    /* The press and the release are states the frame knows and a bridge cannot guess: the box under
     * a held pointer is the one wearing the pressed paint, and the hand leaving it takes that paint
     * and the resting tone with it. */
    const cf_point on_first = {ctx.rects[first_slot].x + 10.0f, ctx.rects[first_slot].y + 10.0f};
    const cf_point on_second = {ctx.rects[second_slot].x + 10.0f, ctx.rects[second_slot].y + 10.0f};
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

    return CF_REPORT();
}
