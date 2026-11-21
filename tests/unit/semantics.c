#include <string.h>

#include "cf/node.h"

#include "cform_test.h"

/*
 * The semantic export is the view read the way a screen reader walks it:
 * these cases declare a page, lay it out and read the dump line by line.
 */
static bool context_of(cf_ctx *ctx) {
    if (!cf_ctx_init(ctx, 64u * 1024u, 4u * 1024u)) {
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

int main(void) {
    printf("semantics\n");
    CF_RUN(a_view_walks_as_the_roles_it_kept);
    CF_RUN(an_export_without_a_view_or_room_answers_false);

    return CF_REPORT();
}
