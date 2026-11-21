#include "cf/node.h"
#include "cf/widget.h"

#include "cform_test.h"

/*
 * The scroller is the first widget that clips: every case lays a viewport
 * over taller content, turns the wheel and reads where the content rides.
 */

static bool context_of(cf_ctx *ctx) {
    if (!cf_ctx_init(ctx, 64u * 1024u, 4u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return false;
    }

    return true;
}

/* One frame of the loop: a pane over content twice the window's height.
 * The widgets are declared inside the frame - the bytes they keep live in
 * the arena cf_frame_begin emptied. */
static void frame_of(cf_ctx *ctx, bool sized) {
    cf_frame_begin(ctx);

    cf_node child = {.style = cf_style_default()};
    child.style.width = CF_PX(100.0f);
    child.style.height = CF_PX(200.0f);
    const cf_node children[] = {child};

    cf_node pane = cf_scroller(ctx, "pane", children, 1u);
    pane.style.width = CF_PX(100.0f);

    if (sized) {
        pane.style.height = CF_PX(100.0f);
    }

    const cf_node roots[] = {pane};
    cf_node root = {.style = cf_style_default()};
    root.style.direction = CF_DIRECTION_COLUMN;
    root.style.width = CF_PX(100.0f);
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

/* The wheel lands where the pointer hovers: one move books the pane, one
 * scroll walks it. */
static void wheel(cf_ctx *ctx, float notches) {
    cf_input(ctx, (cf_event){.type = CF_EVENT_POINTER_MOVE, .position = {.x = 10.0f, .y = 10.0f}});
    cf_input(ctx, (cf_event){.type = CF_EVENT_SCROLL, .wheel = {.x = 0.0f, .y = notches}});
}

static void the_wheel_walks_the_content_and_clamps_at_both_ends(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    ctx.theme = cf_theme_default();

    frame_of(&ctx, true);

    const uint32_t pane = child_of(&ctx, ctx.root_slot, 0u);
    const uint32_t content = child_of(&ctx, pane, 0u);

    CF_NEAR(ctx.rects[pane].height, 100.0f);
    CF_NEAR(ctx.rects[content].height, 200.0f);
    CF_NEAR(ctx.rects[content].y, 0.0f);

    wheel(&ctx, 1.0f);

    CF_NEAR(ctx.rects[content].y, 0.0f);

    frame_of(&ctx, true);

    CF_NEAR(ctx.rects[content].y, -48.0f);

    wheel(&ctx, 10.0f);
    frame_of(&ctx, true);

    CF_NEAR(ctx.rects[content].y, -100.0f);

    wheel(&ctx, -100.0f);
    frame_of(&ctx, true);

    CF_NEAR(ctx.rects[content].y, 0.0f);

    cf_ctx_free(&ctx);
}

static void a_scroller_without_a_height_of_its_own_never_walks(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    ctx.theme = cf_theme_default();

    frame_of(&ctx, false);

    const uint32_t pane = child_of(&ctx, ctx.root_slot, 0u);

    CF_NEAR(ctx.rects[pane].height, 200.0f);

    wheel(&ctx, 5.0f);
    frame_of(&ctx, false);

    const uint32_t content = child_of(&ctx, pane, 0u);

    CF_NEAR(ctx.rects[content].y, 0.0f);

    cf_ctx_free(&ctx);
}

int main(void) {
    printf("widget scroll\n");

    CF_RUN(the_wheel_walks_the_content_and_clamps_at_both_ends);
    CF_RUN(a_scroller_without_a_height_of_its_own_never_walks);

    return CF_REPORT();
}
