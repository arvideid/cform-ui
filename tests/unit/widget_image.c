#include <string.h>

#include "cf/backend.h"
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

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
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

/*
 * A decoder is a backend's own affair, so the case supplies one: it answers a
 * texture in its own numbering and the extent it holds, and refuses the file it
 * cannot read. What the test is about is that a frame carries both of those to
 * the device untouched, since that is all the layer above the seam can do.
 */
static bool fake_load_image(void *user_data, const char *path, cf_image_content *out) {
    uint32_t *calls = user_data;

    if (path == NULL || strcmp(path, "not-an-image.bmp") == 0) {
        return false;
    }

    *calls += 1u;
    *out = (cf_image_content){.texture = 42u, .width = 30.0f, .height = 12.0f};

    return true;
}

static cf_image_content loaded;

/*
 * The child array outlives the build call: cf_frame_run diffs the declaration
 * once the page that built it has returned, so the children ride the frame
 * arena beside it rather than the builder's stack.
 */
static cf_node declare_loaded(cf_ctx *ctx, void *user_data) {
    (void)user_data;
    const cf_node image = cf_image(ctx, "hero", &loaded);
    cf_node *children = cf_frame_alloc(ctx, sizeof *children);
    cf_node root = {.style = cf_style_default()};

    if (children == NULL) {
        return root;
    }

    children[0] = image;
    root.style.direction = CF_DIRECTION_COLUMN;
    root.style.width = CF_PX(100.0f);
    root.style.height = CF_PX(100.0f);
    root.children = children;
    root.child_count = 1u;

    return root;
}

static void a_loaded_image_paints_the_texture_and_the_extent_the_loader_answered(void) {
    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return;
    }

    ctx.theme = cf_theme_default();

    uint32_t calls = 0u;
    const cf_backend backend = (cf_backend){.abi_version = CF_BACKEND_ABI_VERSION,
                                            .size = sizeof(cf_backend),
                                            .user_data = &calls,
                                            .load_image = fake_load_image};
    cf_image_content unreadable = {.texture = 9u, .width = 4.0f, .height = 4.0f};

    /* A file the decoder cannot read answers false and says nothing else: a
     * half-filled content would draw a texture the backend never issued. */
    CF_CHECK(!backend.load_image(backend.user_data, "not-an-image.bmp", &unreadable) &&
                 unreadable.texture == 9u,
             "a refused load leaves the content the caller brought");

    /* Loaded once, before the frame that shows it, the way a build is told to:
     * an upload per frame would be a texture per frame. */
    CF_CHECK(backend.load_image(backend.user_data, "hero.bmp", &loaded) && calls == 1u,
             "the loader answers a texture and an extent");

    cf_command_list list = {0};
    cf_frame_run(&ctx, declare_loaded, NULL, (cf_size){100.0f, 100.0f}, 0.016f, &list);

    const cf_command *image = NULL;

    for (uint32_t i = 0u; i < list.count; ++i) {
        if (list.commands[i].kind == CF_COMMAND_IMAGE) {
            image = &list.commands[i];
        }
    }

    CF_CHECK(image != NULL, "the loaded image reaches the stream");

    if (image != NULL) {
        CF_CHECK(image->texture == 42u, "and names the texture the loader issued");
        /* The height is the extent the loader answered and the column does not
         * argue with; the width is the column's, as a box on the cross axis. */
        CF_NEAR(image->rect.height, 12.0f);
        CF_NEAR(image->rect.width, 100.0f);
    }

    cf_ctx_free(&ctx);
}

int main(void) {
    printf("widget image\n");

    CF_RUN(the_image_declares_its_texture_and_rests_on_the_theme);
    CF_RUN(a_loaded_image_paints_the_texture_and_the_extent_the_loader_answered);

    return CF_REPORT();
}
