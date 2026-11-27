#include "cf/widget.h"

cf_node cf_image(cf_ctx *ctx, const char *name, const cf_image_content *content) {
    cf_node node = {.style = cf_style_default()};

    if (ctx == NULL || name == NULL || content == NULL) {
        return node;
    }

    node.key = cf_ctx_key(ctx, name);
    node.measure = cf_image_measure;
    /* The tree hands the content back to the paint walk, which reads it as
     * const; the declaration type keeps one pointer for every measure. */
    node.measure_data = (void *)content;
    node.role = CF_ROLE_IMAGE;
    /* A picture has no words of its own: the `name` above keys the node, and the build that has a
     * sentence for the picture sets `cf_node.name` on the returned box. One without it is
     * announced as an image that says nothing, which is the honest reading of an unlabeled one. */
    node.access = CF_ACCESS_IMAGE;
    const cf_theme *theme = cf_theme_of(ctx);

    node.style = theme != NULL ? theme->variants[CF_ROLE_IMAGE].base : cf_style_default();
    return node;
}
