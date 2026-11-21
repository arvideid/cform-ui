#include "cf/widget.h"

cf_node cf_label(cf_ctx *ctx, cf_string text, const cf_text_stack *stack, cf_color color) {
    cf_node node = {.style = cf_style_default()};

    if (ctx == NULL || stack == NULL || stack->fonts == NULL || stack->count == 0u) {
        return node;
    }

    cf_text_content *content = cf_alloc(&ctx->frame, sizeof *content);

    if (content == NULL) {
        return node;
    }

    *content = (cf_text_content){
        .stack = *stack,
        .text = text,
        .max_lines = {.present = true, .value = 1u},
        .ellipsis = true,
    };

    node.measure = cf_text_measure;
    node.measure_data = content;
    node.style.color = (cf_opt_color){.present = true, .value = color};
    return node;
}
