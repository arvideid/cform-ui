/* Every widget on one page, driven by a scripted hand a human can watch run. */
#include <stdio.h>

#include "cf/backend.h"
#include "cf/widget.h"

#define WINDOW_WIDTH 480u
#define WINDOW_HEIGHT 320u
#define DEMO_FRAMES 240u

static const cf_theme *theme;
static cf_font font;
static cf_text_stack fonts;
static bool quiet_off = true;
static char row_bytes[8u][16u]; /* four rows are visible; a ring of eight never collides */
/* Generated, not fetched: the repo carries no binary it can regenerate. */
static uint8_t checker[8u * 8u * 4u];

static void toggle_quiet(cf_ctx *ctx, uint32_t slot, void *user_data) {
    (void)ctx, (void)slot, (void)user_data;
    quiet_off = !quiet_off;
}

static cf_node make_row(cf_ctx *ctx, uint32_t index, void *user_data) {
    (void)user_data;
    char *bytes = row_bytes[index % 8u];
    const int length = snprintf(bytes, 16u, "row %u", index);
    return cf_label(ctx, (cf_string){bytes, (uint32_t)length}, &fonts, theme->ink);
}

/* Declare, then diff in the same scope: children are borrowed for the diff call alone. */
static void build(cf_ctx *ctx, const cf_image_content *logo) {
    cf_frame_begin(ctx);
    cf_node labels[] = {cf_label(ctx, cf_string_of("press me"), &fonts, theme->on_ink),
                        cf_label(ctx, cf_string_of("quiet"), &fonts, theme->on_ink)};
    cf_node buttons[] = {cf_button(ctx, "press", toggle_quiet, NULL, &labels[0], 1u),
                         cf_button(ctx, "quiet", toggle_quiet, NULL, &labels[1], 1u)};
    buttons[1].disabled = quiet_off;
    cf_node bar = {.style = theme->bar, .children = buttons, .child_count = 2u};
    const cf_node children[] = {bar, cf_list(ctx, "rows", 96.0f, 24.0f, 64u, make_row, NULL),
                                cf_image(ctx, "logo", logo)};
    cf_node page = {.style = theme->page, .children = children, .child_count = 3u};
    page.style.width = CF_PX((float)WINDOW_WIDTH);
    page.style.height = CF_PX((float)WINDOW_HEIGHT);
    page.style.direction = CF_DIRECTION_COLUMN;
    page.style.padding = (cf_box){CF_PX(16.0f), CF_PX(16.0f), CF_PX(16.0f), CF_PX(16.0f)};
    page.style.row_gap = CF_PX(12.0f);
    cf_diff(ctx, &page);
    cf_frame_resolve(ctx);
    cf_frame_layout(
        ctx,
        (cf_available){.width = CF_PX((float)WINDOW_WIDTH), .height = CF_PX((float)WINDOW_HEIGHT)},
        (cf_point){0.0f, 0.0f});
}

int main(void) {
    theme = cf_theme_default();
    font = cf_font_sdl(CFORM_GALLERY_FONT, 14.0f);
    fonts = (cf_text_stack){.fonts = &font, .count = font.ops != NULL ? 1u : 0u};
    cf_ctx ctx;
    if (!cf_ctx_init(&ctx, 256u * 1024u, 16u * 1024u)) {
        printf("gallery: no context\n");
        return 1;
    }
    ctx.theme = theme;
    const cf_sdl_config config = {
        .title = "cform gallery", .width = WINDOW_WIDTH, .height = WINDOW_HEIGHT};
    const cf_backend backend = cf_backend_sdl(&config);
    if (backend.submit == NULL) {
        printf("gallery: no window\n");
        return 1;
    }
    for (uint32_t i = 0u; i < 64u; ++i) {
        uint8_t *pixel = &checker[i * 4u];
        pixel[0] = pixel[1] = pixel[2] = ((i / 8u + i % 8u) & 1u) != 0u ? 150u : 60u;
        pixel[3] = 255u;
    }
    const uint32_t texture = backend.create_texture(backend.user_data, 8u, 8u, checker);
    const cf_image_content logo = {.texture = texture, .width = 32.0f, .height = 32.0f};
    for (uint32_t frame = 0u; frame < DEMO_FRAMES; ++frame) {
        /* The scripted hand: hover and press the button, then walk down to the list. */
        const cf_point at = {48.0f, frame >= 150u ? 100.0f : 34.0f};
        cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_MOVE, .position = at});
        if (frame == 60u) {
            cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_DOWN, .position = at});
        } else if (frame == 90u) {
            cf_input(&ctx, (cf_event){.type = CF_EVENT_POINTER_UP, .position = at});
        } else if (frame == 160u) {
            cf_input(&ctx, (cf_event){.type = CF_EVENT_SCROLL, .wheel = {0.0f, 1.0f}});
        }
        build(&ctx, &logo);
        cf_command_list list = {0};
        cf_paint(&ctx.frame, cf_frame_tree(&ctx), ctx.rects, 0u,
                 (cf_rect){0.0f, 0.0f, (float)WINDOW_WIDTH, (float)WINDOW_HEIGHT}, &list);
        backend.submit(backend.user_data, list.commands, list.count);
        backend.present(backend.user_data);
    }
    cf_ctx_free(&ctx);
}
