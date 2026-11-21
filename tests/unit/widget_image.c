#include "cf/node.h"
#include "cf/widget.h"

#include "cform_test.h"

/*
 * The image is the first widget whose paint is not its own box's: the case
 * checks what it declares, what the theme resolves over it, and the extent
 * its content answers when the build states no size.
 */

static const cf_image_content image_content = {.texture = 3u, .width = 16.0f, .height = 24.0f};

static uint32_t child_of(const cf_ctx *ctx, uint32_t slot, uint32_t index) {
    const cf_tree *tree = cf_frame_tree(ctx);

    if (tree == NULL || slot == CF_NO_LINK) {
        return CF_NO_LINK;
    }

    return tree->child_slots[tree->child_start[slot] + index];
}

static void the_image_declares_its_texture_and_rests_on_the_theme(void) {
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u, 4u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();

    cf_frame_begin(&ctx);
    cf_node image = cf_image(&ctx, "logo", &image_content);
    const cf_node roots[] = {image};
    cf_node root = {.style = cf_style_default()};
    root.style.direction = CF_DIRECTION_COLUMN;
    root.style.width = CF_PX(100.0f);
    root.style.height = CF_PX(100.0f);
    root.children = roots;
    root.child_count = 1u;

    cf_diff(&ctx, &root);
    cf_frame_resolve(&ctx);
    cf_frame_layout(&ctx, (cf_available){.width = CF_PX(100.0f), .height = CF_PX(100.0f)},
                    (cf_point){.x = 0.0f, .y = 0.0f});

    const uint32_t slot = child_of(&ctx, ctx.root_slot, 0u);
    const cf_tree *tree = cf_frame_tree(&ctx);

    CF_CHECK(slot != CF_NO_LINK && tree != NULL, "the image took a slot in the frame");
    CF_CHECK(tree->measures[slot] == cf_image_measure, "the image carries its measure");
    CF_CHECK(tree->measure_data[slot] == (void *)&image_content,
             "and the content the measure answers");
    /* The content's extent answers the main axis; the cross axis stretches
     * to the container, as a replaced element does. */
    CF_NEAR(ctx.rects[slot].height, 24.0f);
    CF_NEAR(ctx.rects[slot].width, 100.0f);

    const cf_style resting = ctx.layout.styles[slot];

    CF_CHECK(resting.background.present, "the theme's image box rests on its own tone");

    cf_ctx_free(&ctx);
}

int main(void) {
    printf("widget image\n");

    CF_RUN(the_image_declares_its_texture_and_rests_on_the_theme);

    return CF_REPORT();
}
