/* Every widget on one page, driven by whatever the window's hand does. */
#include <stdio.h>

#include "cf/app.h"
#include "cf/build.h"
#include "cf/widget.h"

#define WINDOW_WIDTH 480u
#define WINDOW_HEIGHT 320u

static const cf_theme *theme;
static cf_font font;
static cf_text_stack fonts;
static cf_image_content logo;
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

/* Percentages are the one sizing that follows the window when the hand
 * resizes it; the theme's page states everything else. */
static cf_style page_style(void) {
    cf_style page = theme->page;
    page.width = CF_PERCENT(100.0f);
    page.height = CF_PERCENT(100.0f);
    page.padding = (cf_box){CF_PX(16.0f), CF_PX(16.0f), CF_PX(16.0f), CF_PX(16.0f)};
    page.row_gap = CF_PX(12.0f);

    return page;
}

static cf_node build(cf_ctx *ctx) {
    const cf_node press_label = cf_label(ctx, cf_string_of("press me"), &fonts, theme->on_ink);
    const cf_node quiet_label = cf_label(ctx, cf_string_of("quiet"), &fonts, theme->on_ink);

    const cf_node press = cf_button(ctx, "press", toggle_quiet, NULL, &press_label, 1u);
    cf_node quiet = cf_button(ctx, "quiet", toggle_quiet, NULL, &quiet_label, 1u);
    quiet.disabled = quiet_off;

    return CF_COL(ctx, page_style(), CF_ROW(ctx, theme->bar, press, quiet),
                  cf_list(ctx, "rows", 96.0f, 24.0f, 64u, make_row, NULL),
                  cf_image(ctx, "logo", &logo));
}

int main(void) {
    theme = cf_theme_default();
    font = cf_font_sdl(CFORM_GALLERY_FONT, 14.0f);
    fonts = (cf_text_stack){.fonts = &font, .count = font.ops != NULL ? 1u : 0u};

    for (uint32_t i = 0u; i < 64u; ++i) {
        uint8_t *pixel = &checker[i * 4u];
        pixel[0] = pixel[1] = pixel[2] = ((i / 8u + i % 8u) & 1u) != 0u ? 150u : 60u;
        pixel[3] = 255u;
    }

    const cf_backend backend = cf_backend_sdl(
        &(cf_sdl_config){.title = "cform gallery", .width = WINDOW_WIDTH, .height = WINDOW_HEIGHT});
    if (backend.submit == NULL) {
        printf("gallery: no window\n");
        return 1;
    }

    logo = (cf_image_content){.texture = backend.create_texture(backend.user_data, 8u, 8u, checker),
                              .width = 32.0f,
                              .height = 32.0f};

    cf_app_run(
        &(cf_app_config){.build = build, .backend = backend, .theme = theme, .fonts = &fonts});
}
