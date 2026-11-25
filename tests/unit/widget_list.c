#include <string.h>

#include "cf/node.h"
#include "cf/widget.h"

#include "cform_test.h"

/*
 * The list is the scroller with a window: only the rows in view are
 * declared, the wheel turns the window by rows, and a row keeps its slot
 * for as long as it stays visible.
 */

static bool context_of(cf_ctx *ctx) {
    if (!cf_ctx_init(ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return false;
    }

    return true;
}

/* One row: a plain box. The list holds it to the height it dealt and keys
 * it by index under the list's name. */
static cf_node make_row(cf_ctx *ctx, uint32_t index, void *user_data) {
    (void)ctx;
    (void)index;
    (void)user_data;
    return (cf_node){.style = cf_style_default()};
}

/* One frame of the loop: fifty twenty-point rows under a hundred-point
 * window. The list is declared inside the frame, beside the bytes it keeps. */
static void frame_of(cf_ctx *ctx) {
    cf_frame_begin(ctx);

    cf_node list = cf_list(ctx, "rows", 100.0f, 20.0f, 50u, make_row, NULL);
    const cf_node roots[] = {list};
    cf_node root = {.style = cf_style_default()};
    root.style.direction = CF_DIRECTION_COLUMN;
    root.style.width = CF_PX(100.0f);
    root.style.height = CF_PX(100.0f);
    root.children = roots;
    root.child_count = 1u;

    cf_diff(ctx, &root);
    cf_frame_resolve(ctx);
    cf_frame_layout(ctx, (cf_available){.width = CF_PX(100.0f), .height = CF_PX(100.0f)},
                    (cf_point){.x = 0.0f, .y = 0.0f});
}

static uint32_t child_of(const cf_ctx *ctx, uint32_t slot, uint32_t index) {
    const cf_tree *tree = cf_frame_tree(ctx);

    if (tree == NULL || slot == CF_NO_LINK) {
        return CF_NO_LINK;
    }

    return tree->child_slots[tree->child_start[slot] + index];
}

static uint32_t child_count_of(const cf_ctx *ctx, uint32_t slot) {
    const cf_tree *tree = cf_frame_tree(ctx);

    if (tree == NULL || slot == CF_NO_LINK) {
        return 0u;
    }

    return tree->child_start[slot + 1u] - tree->child_start[slot];
}

/* The wheel lands where the pointer hovers: one move books the list, one
 * scroll walks it. */
static void wheel(cf_ctx *ctx, float notches) {
    cf_input(ctx, (cf_event){.type = CF_EVENT_POINTER_MOVE, .position = {.x = 10.0f, .y = 10.0f}});
    cf_input(ctx, (cf_event){.type = CF_EVENT_SCROLL, .wheel = {.x = 0.0f, .y = notches}});
}

static void the_list_declares_only_the_rows_in_view(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    ctx.theme = cf_theme_default();

    frame_of(&ctx);

    const uint32_t list = child_of(&ctx, ctx.root_slot, 0u);
    const uint32_t content = child_of(&ctx, list, 0u);
    const uint32_t rows = child_count_of(&ctx, content);

    CF_CHECK(rows == 6u, "a hundred-point window over twenty-point rows shows six rows");
    CF_NEAR(ctx.rects[list].height, 100.0f);
    CF_NEAR(ctx.rects[content].y, 0.0f);

    for (uint32_t i = 0u; i < rows; ++i) {
        CF_NEAR(ctx.rects[child_of(&ctx, content, i)].height, 20.0f);
    }

    cf_ctx_free(&ctx);
}

static void the_wheel_turns_the_window_and_a_row_keeps_its_slot(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    ctx.theme = cf_theme_default();

    frame_of(&ctx);

    const uint32_t list = child_of(&ctx, ctx.root_slot, 0u);
    const uint32_t content = child_of(&ctx, list, 0u);
    uint32_t before[6];

    for (uint32_t i = 0u; i < 6u; ++i) {
        before[i] = child_of(&ctx, content, i);
    }

    wheel(&ctx, 2.0f);
    frame_of(&ctx);

    CF_CHECK(child_count_of(&ctx, content) == 6u, "the window stays six rows wide");
    CF_NEAR(ctx.rects[content].y, 0.0f);

    for (uint32_t i = 0u; i < 4u; ++i) {
        CF_CHECK(child_of(&ctx, content, i) == before[i + 2u],
                 "the rows that stayed kept the slots they held");
    }

    for (uint32_t i = 4u; i < 6u; ++i) {
        CF_CHECK(child_of(&ctx, content, i) != CF_NO_LINK,
                 "the rows the wheel walked onto were declared");
    }

    cf_ctx_free(&ctx);
}

static void the_window_clamps_at_the_list_s_end(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    ctx.theme = cf_theme_default();

    frame_of(&ctx);

    const uint32_t list = child_of(&ctx, ctx.root_slot, 0u);

    wheel(&ctx, 1000.0f);
    frame_of(&ctx);

    const uint32_t content = child_of(&ctx, list, 0u);
    const uint32_t last = child_of(&ctx, content, 4u);

    CF_CHECK(child_count_of(&ctx, content) == 5u,
             "at the end the window shows the rows that remain");
    CF_NEAR(ctx.rects[content].y, 0.0f);
    CF_NEAR(ctx.rects[last].y + ctx.rects[last].height, 100.0f);

    cf_ctx_free(&ctx);
}

/* The key cf_list gives a row that declares none interned under the list's
 * name and its index. The node store keeps that key in the record's first
 * field, so a case can read what a slot was interned under without the
 * private record shape. cf_store_at indexes by slot; only live slots hold a
 * row. */
static cf_name key_at(const cf_ctx *ctx, uint32_t slot) {
    struct key_only {
        cf_name key;
    } *head = cf_store_at(&ctx->nodes, slot);

    return head != NULL ? head->key : CF_NO_NAME;
}

/* A list named longer than the row-key buffer once interned every visible row
 * under the same truncated string, so the wheel could land a row on another
 * row's slot. The name is now capped before the index, so the suffix survives
 * and each row keeps its own key even when the prefix is shared. */
static void long_name_rows_keep_distinct_keys(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    ctx.theme = cf_theme_default();

    static char long_name[200];

    for (size_t i = 0u; i + 1u < sizeof long_name; ++i) {
        long_name[i] = 'x';
    }
    long_name[sizeof long_name - 1u] = '\0';

    cf_frame_begin(&ctx);
    cf_node list = cf_list(&ctx, long_name, 100.0f, 20.0f, 50u, make_row, NULL);
    const cf_node roots[] = {list};
    cf_node root = {.style = cf_style_default()};
    root.style.direction = CF_DIRECTION_COLUMN;
    root.children = roots;
    root.child_count = 1u;
    CF_CHECK(cf_diff(&ctx, &root) != CF_NO_LINK, "the diff claims the long-named list");

    const uint32_t scroll = child_of(&ctx, ctx.root_slot, 0u);
    const uint32_t content = child_of(&ctx, scroll, 0u);
    const uint32_t rows = child_count_of(&ctx, content);

    CF_CHECK(rows == 6u, "the window shows six rows");

    cf_name previous = CF_NO_NAME;
    uint32_t distinct = 0u;

    for (uint32_t i = 0u; i < rows; ++i) {
        const cf_name key = key_at(&ctx, child_of(&ctx, content, i));

        CF_CHECK(key != CF_NO_NAME, "the row interned a key");

        if (i > 0u && key != previous) {
            distinct++;
        }
        previous = key;

        /* The truncated prefix is the same for every row: it is the index the
         * old code truncated away that now keeps the rows apart. */
        const cf_string text = cf_interned(&ctx.names, key);
        char shown[256] = {0};

        if (text.length < sizeof shown) {
            memcpy(shown, text.chars, text.length);
            CF_CHECK(strstr(shown, "xxx") != NULL, "the truncated name rides each key");
        }
    }

    CF_CHECK(distinct == rows - 1u, "six rows intern six distinct keys under the long name");

    cf_ctx_free(&ctx);
}

int main(void) {
    printf("widget list\n");

    CF_RUN(the_list_declares_only_the_rows_in_view);
    CF_RUN(the_wheel_turns_the_window_and_a_row_keeps_its_slot);
    CF_RUN(the_window_clamps_at_the_list_s_end);
    CF_RUN(long_name_rows_keep_distinct_keys);

    return CF_REPORT();
}
