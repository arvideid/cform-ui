/*
 * The draw path on a screen: a scene laid out, painted and submitted to the
 * SDL backend, left up long enough to look at. Everything the tests check
 * about this stream is headless; what a pixel looks like is the human's call.
 */
#include <stdio.h>

#include "cf/backend.h"

#define WINDOW_WIDTH 400u
#define WINDOW_HEIGHT 300u
#define DEMO_FRAMES 240u

static cf_opt_color stated(uint8_t red, uint8_t green, uint8_t blue) {
    return (cf_opt_color){.present = true, .value = (cf_color){red, green, blue, 255u}};
}

static cf_style filled(cf_opt_color color) {
    cf_style style = cf_style_default();
    style.background = color;
    return style;
}

int main(void) {
    cf_style root = filled(stated(42u, 42u, 42u));
    root.width = CF_PX((float)WINDOW_WIDTH);
    root.height = CF_PX((float)WINDOW_HEIGHT);
    root.direction = CF_DIRECTION_COLUMN;
    root.padding = (cf_box){CF_PX(16.0f), CF_PX(16.0f), CF_PX(16.0f), CF_PX(16.0f)};
    root.border = (cf_box){CF_PX(2.0f), CF_PX(2.0f), CF_PX(2.0f), CF_PX(2.0f)};
    root.border_color = stated(30u, 30u, 30u);
    root.radius = 16.0f;
    root.row_gap = CF_PX(12.0f);

    cf_style header = filled(stated(70u, 130u, 180u));
    header.height = CF_PX(56.0f);
    header.border = (cf_box){CF_PX(2.0f), CF_PX(2.0f), CF_PX(2.0f), CF_PX(2.0f)};
    header.border_color = stated(16u, 16u, 16u);
    header.radius = 10.0f;

    cf_style panel = filled(stated(60u, 179u, 113u));
    panel.height = CF_PX(64.0f);
    panel.radius = 10.0f;

    cf_style footer = filled(stated(220u, 20u, 60u));
    footer.radius = 10.0f;
    footer.flex_grow = 1.0f;

    const cf_style styles[] = {root, header, panel, footer};
    const uint32_t child_start[] = {0u, 3u, 3u, 3u, 3u};
    const uint32_t child_slots[] = {1u, 2u, 3u};

    const cf_tree tree = {
        .styles = styles, .child_start = child_start, .child_slots = child_slots, .node_count = 4u};

    cf_arena scratch;
    if (!cf_arena_init(&scratch, 65536u)) {
        printf("scenes: no scratch\n");
        return 1;
    }

    cf_rect rects[4];
    cf_layout(
        &scratch, &tree, 0u,
        (cf_available){.width = CF_PX((float)WINDOW_WIDTH), .height = CF_PX((float)WINDOW_HEIGHT)},
        (cf_point){0.0f, 0.0f}, rects);

    cf_command_list list = {0};
    cf_paint(&scratch, &tree, rects, 0u,
             (cf_rect){0.0f, 0.0f, (float)WINDOW_WIDTH, (float)WINDOW_HEIGHT}, &list);

    const cf_sdl_config config = {
        .title = "cform scenes", .width = WINDOW_WIDTH, .height = WINDOW_HEIGHT};
    const cf_backend backend = cf_backend_sdl(&config);

    if (backend.submit == NULL) {
        printf("scenes: no window\n");
        cf_arena_free(&scratch);
        return 1;
    }

    backend.submit(backend.user_data, list.commands, list.count);

    for (uint32_t frame = 0u; frame < DEMO_FRAMES; ++frame) {
        backend.present(backend.user_data);
    }

    cf_arena_free(&scratch);
    return 0;
}
