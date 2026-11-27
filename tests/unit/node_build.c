#include <string.h>

#include "cf/build.h"
#include "cform_test.h"

/*
 * The declaration sugar is the surface a build touches most, so these cases
 * ask what the sugar promises: the children it lists are counted and copied
 * where the diff still reads them, the direction it states reaches the layout,
 * and a refusal says which boundary refused instead of silently dropping the
 * declaration.
 */
static bool context_of(cf_ctx *ctx, size_t frame_bytes) {
    if (!cf_ctx_init(ctx, frame_bytes)) {
        CF_CHECK(false, "the context starts");
        return false;
    }

    return true;
}

static cf_style filled(float width, float height) {
    cf_style box = cf_style_default();
    box.width = CF_PX(width);
    box.height = CF_PX(height);
    box.background = (cf_opt_color){.present = true, .value = CF_COLOR(1, 2, 3)};

    return box;
}

static cf_node leaf(void) {
    cf_node node = {0};
    node.style = filled(10.0f, 10.0f);

    return node;
}

static cf_command *command_at(const cf_command_list *list, uint32_t index) {
    if (list->count <= index) {
        return NULL;
    }

    return &list->commands[index];
}

/* A column of two rows' worth: the row's children sit side by side, the
 * column's stack, and the container boxes themselves paint nothing. */
static cf_node page_of(cf_ctx *ctx, void *user_data) {
    (void)user_data;
    return CF_COL(ctx, cf_style_default(), CF_ROW(ctx, cf_style_default(), leaf(), leaf()), leaf());
}

static void the_macros_count_children_and_state_direction(void) {
    cf_ctx ctx;

    if (!context_of(&ctx, 64u * 1024u)) {
        return;
    }

    cf_command_list list = {0};
    cf_frame_run(&ctx, page_of, NULL, (cf_size){100.0f, 100.0f}, 0.0f, &list);

    CF_CHECK(list.count == 3u, "three leaves fill, two containers paint nothing");
    CF_CHECK(command_at(&list, 0u) != NULL && command_at(&list, 0u)->rect.x == 0.0f &&
                 command_at(&list, 1u)->rect.x == 10.0f,
             "the row places its children along x");
    CF_CHECK(command_at(&list, 2u) != NULL && command_at(&list, 2u)->rect.y == 10.0f,
             "the column places its children along y");

    cf_ctx_free(&ctx);
}

/* The array a build lists lives in the build's own frame; the diff reads it
 * after that frame is gone, so the copy is what the view is built from. */
static void the_children_are_copied_not_borrowed(void) {
    cf_ctx ctx;

    if (!context_of(&ctx, 64u * 1024u)) {
        return;
    }

    cf_frame_begin(&ctx);

    cf_node leaves[2] = {leaf(), leaf()};
    const cf_node page = cf_row(&ctx, cf_style_default(), leaves, 2u);
    leaves[0].style.width = CF_PX(999.0f);

    cf_command_list list = {0};
    const uint32_t root_slot = cf_diff(&ctx, &page);
    cf_frame_layout(&ctx, (cf_available){.width = CF_PX(100.0f), .height = CF_PX(100.0f)},
                    (cf_point){0.0f, 0.0f});
    cf_paint(&ctx.frame, cf_frame_tree(&ctx), ctx.rects, root_slot,
             (cf_rect){0.0f, 0.0f, 100.0f, 100.0f}, &list);

    CF_CHECK(list.count == 2u, "both children were declared");
    CF_CHECK(command_at(&list, 0u) != NULL && command_at(&list, 0u)->rect.width == 10.0f,
             "the copied child kept the width it was declared with");

    cf_ctx_free(&ctx);
}

static void a_container_without_a_context_declares_nothing(void) {
    const cf_node column = cf_col(NULL, filled(10.0f, 10.0f), NULL, 0u);

    CF_CHECK(column.children == NULL && column.child_count == 0u, "nothing was copied");
    CF_CHECK(!column.style.background.present, "the style did not arrive either");
}

static void children_named_without_an_array_are_dropped(void) {
    cf_ctx ctx;

    if (!context_of(&ctx, 64u * 1024u)) {
        return;
    }

    cf_frame_begin(&ctx);
    CF_CHECK(cf_error_message(&ctx) == NULL, "a frame that refused nothing says nothing");

    const cf_node column = cf_col(&ctx, cf_style_default(), NULL, 2u);

    CF_CHECK(column.child_count == 0u, "the children the count named are not there");
    CF_CHECK(cf_error_message(&ctx) != NULL && strstr(cf_error_message(&ctx), "children") != NULL,
             "the refusal names the children it dropped");

    cf_frame_begin(&ctx);
    CF_CHECK(cf_error_message(&ctx) == NULL, "the refusal belongs to the frame that made it");

    cf_ctx_free(&ctx);
}

/* An arena no bigger than the context's own first request cannot hold a
 * child list: the box still carries its style, and the reason is on record. */
static void an_arena_without_room_drops_the_children(void) {
    cf_ctx ctx;

    if (!context_of(&ctx, 1024u)) {
        return;
    }

    cf_frame_begin(&ctx);
    (void)cf_alloc(&ctx.frame, 1000u);

    const cf_node column = cf_col(&ctx, filled(10.0f, 10.0f), (cf_node[]){leaf(), leaf()}, 2u);

    CF_CHECK(column.child_count == 0u, "the children the arena could not hold are dropped");
    CF_CHECK(column.style.width.value == 10.0f, "the box is still the box it was styled as");
    CF_CHECK(cf_error_message(&ctx) != NULL && strstr(cf_error_message(&ctx), "arena") != NULL,
             "the refusal names the arena that ran out");

    cf_ctx_free(&ctx);
}

int main(void) {
    printf("node build\n");

    CF_RUN(the_macros_count_children_and_state_direction);
    CF_RUN(the_children_are_copied_not_borrowed);
    CF_RUN(a_container_without_a_context_declares_nothing);
    CF_RUN(children_named_without_an_array_are_dropped);
    CF_RUN(an_arena_without_room_drops_the_children);

    return CF_REPORT();
}
