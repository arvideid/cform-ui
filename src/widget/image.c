#include "cf/widget.h"

cf_node cf_image(cf_ctx *ctx, const char *name, const cf_image_content *content) {
    cf_node node = {.style = cf_style_default()};

    if (ctx == NULL || name == NULL || content == NULL) {
        return node;
    }

    node.key = cf_intern_text(&ctx->names, name);
    node.measure = cf_image_measure;
    /* The tree hands the content back to the paint walk, which reads it as
     * const; the declaration type keeps one pointer for every measure. */
    node.measure_data = (void *)content;
    node.role = CF_ROLE_IMAGE;
    node.style = ctx->theme != NULL ? ctx->theme->variants[CF_ROLE_IMAGE].base : cf_style_default();
    return node;
}
