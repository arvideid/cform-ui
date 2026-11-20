#include <stdio.h>

#include <cform/cform.h>

/* 60-30-10 on one neutral scale. #f5f5f5 is the dominant field, #fafafa cards
 * lift off it, #171717 is the single accent reserved for the primary action.
 * Wells (#e5e5e5) sit below cards so parent/child nesting reads by depth, not
 * hue. Text tones are chosen so every fill clears WCAG-AA against its label:
 * #0a0a0a on #fafafa is 19:1, #525252 on #fafafa is 7.9:1, #fafafa on #171717
 * is 16:1. */
#define PAGE        UI_COLOR(245, 245, 245)  /* #f5f5f5 dominant field */
#define SURFACE     UI_COLOR(250, 250, 250)  /* #fafafa elevated card */
#define SURFACE_ALT UI_COLOR(229, 229, 229)  /* #e5e5e5 recessed well, tabs */
#define BORDER      UI_COLOR(212, 212, 212)  /* #d4d4d4 hairline */
#define INK         UI_COLOR(10, 10, 10)     /* #0a0a0a primary text */
#define INK_UP      UI_COLOR(23, 23, 23)     /* #171717 accent fill */
#define MUTED       UI_COLOR(82, 82, 82)     /* #525252 secondary text */
#define ON_INK      UI_COLOR(250, 250, 250)  /* #fafafa text on accent */

static const UI_Color HOVER = { 212, 212, 212, 255 };    /* #d4d4d4 */
static const UI_Color PRESSED = { 161, 161, 161, 255 };  /* #a1a1a1 */

/* Every picture in the demo is fetched over HTTPS at runtime, so there is no
 * binary art to commit. A browser enforces CORS on cross-origin reads and
 * i.pinimg.com answers without an Access-Control-Allow-Origin header, so the
 * wasm build cannot fetch it even though libcurl happily does; the web build
 * asks hosts that answer `Access-Control-Allow-Origin: *` instead. The frames,
 * their fits and their layout are identical on both platforms. */
#ifdef __EMSCRIPTEN__
#define IMAGE_HERO   "https://httpbin.org/image/jpeg"
#define IMAGE_WIDE   "https://httpbin.org/image/png"
#define IMAGE_SQUARE "https://placehold.co/320x320/e5e5e5/525252/png"
#else
#define IMAGE_HERO \
    "https://i.pinimg.com/1200x/0d/65/55/0d6555e28635db8b1cad7ec76b306313.jpg"
#define IMAGE_WIDE \
    "https://i.pinimg.com/1200x/d2/4f/b2/d24fb208b009927d004d4327f23b7c00.jpg"
#define IMAGE_SQUARE \
    "https://i.pinimg.com/1200x/de/73/9c/de739cc20271eba69c58099bd9492e21.jpg"
#endif

/* CORS-open everywhere, so this frame looks the same on both platforms. */
#define IMAGE_REMOTE "https://placehold.co/900x200/e5e5e5/525252/png"

static const char *const LOREM =
    "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Sed do eiusmod "
    "tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim "
    "veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea "
    "commodo consequat. Duis aute irure dolor in reprehenderit in voluptate "
    "velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat "
    "cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id "
    "est laborum. Sed ut perspiciatis unde omnis iste natus error sit voluptatem "
    "accusantium doloremque laudantium, totam rem aperiam, eaque ipsa quae ab "
    "illo inventore veritatis et quasi architecto beatae vitae dicta sunt "
    "explicabo. Nemo enim ipsam voluptatem quia voluptas sit aspernatur aut "
    "odit aut fugit, sed quia consequuntur magni dolores eos qui ratione "
    "voluptatem sequi nesciunt. Neque porro quisquam est, qui dolorem ipsum "
    "quia dolor sit amet, consectetur, adipisci velit.";

static UI_Frame *g_detail;
static UI_Frame *g_current_panel;

static void select_panel(UI_Frame *frame, void *user_data) {
    (void)frame;
    UI_Frame *panel = user_data;

    if (panel == g_current_panel) {
        return;
    }

    ui_remove(g_detail, g_current_panel);
    ui_add(g_detail, panel);
    g_current_panel = panel;
}

static void on_action(UI_Frame *frame, void *user_data) {
    (void)frame;
    printf("%s clicked\n", (const char *)user_data);
}

static UI_Frame *make_text(
    UI_Context *ctx,
    const char *text,
    float height,
    UI_FontStyle style,
    float size,
    UI_Color color,
    bool wrap
) {
    return ui_frame(ctx, (UI_FrameConfig){
        .width      = UI_FILL,
        .height     = UI_FIXED(height),
        .text       = text,
        .text_color = color,
        .font_size  = size,
        .font_style = style,
        .wrap_text  = wrap,
    });
}

static UI_Frame *make_box(UI_Context *ctx, float w, float h, UI_Color bg) {
    return ui_frame(ctx, (UI_FrameConfig){
        .width         = UI_FIXED(w),
        .height        = UI_FIXED(h),
        .background    = bg,
        .corner_radius = 6.0f,
    });
}

static UI_Frame *make_section(UI_Context *ctx, const char *title) {
    UI_Frame *section = ui_frame(ctx, (UI_FrameConfig){
        .width      = UI_FILL,
        .height     = UI_FIXED(120),
        .direction  = UI_VERTICAL,
        .gap        = 8.0f,
    });
    ui_add(section, make_text(ctx, title, 20, UI_FONT_BOLD, 14.0f, INK, false));

    return section;
}

static UI_Frame *make_panel_text(UI_Context *ctx) {
    UI_Frame *panel = ui_frame(ctx, (UI_FrameConfig){
        .width      = UI_FILL,
        .height     = UI_FILL,
        .direction  = UI_VERTICAL,
        .padding    = UI_ALL(16),
        .gap        = 12.0f,
        .scrollable = true,
    });

    ui_add(panel, make_text(ctx, "Reading", 28, UI_FONT_BOLD, 22.0f, INK, false));
    ui_add(panel, make_text(ctx, LOREM, 160, UI_FONT_NORMAL, 15.0f, INK, true));
    ui_add(panel, make_text(ctx,
        "Scroll down for more. The wheel moves the clipped content.",
        24, UI_FONT_ITALIC, 14.0f, MUTED, true));
    ui_add(panel, make_text(ctx, LOREM, 160, UI_FONT_NORMAL, 15.0f, INK, true));
    ui_add(panel, make_text(ctx, "Emphasis", 28, UI_FONT_BOLD, 22.0f, INK, false));
    ui_add(panel, make_text(ctx, "Bold weight.", 24, UI_FONT_BOLD, 16.0f, INK, false));
    ui_add(panel, make_text(ctx, "Italic weight.", 24, UI_FONT_ITALIC, 16.0f, INK, false));
    ui_add(panel, make_text(ctx, "Bold and italic together.", 24,
        UI_FONT_BOLD_ITALIC, 16.0f, INK, false));

    return panel;
}

static UI_Frame *make_panel_images(UI_Context *ctx) {
    UI_Frame *panel = ui_frame(ctx, (UI_FrameConfig){
        .width      = UI_FILL,
        .height     = UI_FILL,
        .direction  = UI_VERTICAL,
        .padding    = UI_ALL(16),
        .gap        = 12.0f,
        .scrollable = true,
    });

    ui_add(panel, make_text(ctx, "Images", 28, UI_FONT_BOLD, 22.0f, INK, false));

    UI_Frame *hero = ui_frame(ctx, (UI_FrameConfig){
        .width         = UI_FILL,
        .height        = UI_FIXED(320),
        .image_src     = IMAGE_HERO,
        .image_fit     = UI_IMAGE_FIT,
        .background    = SURFACE_ALT,
        .border_color  = BORDER,
        .border_width  = 1.0f,
        .corner_radius = 12.0f,
        .link_url      = "https://github.com/libsdl-org/SDL_image",
    });
    ui_add(panel, hero);

    ui_add(panel, make_text(ctx,
        "Downloaded over HTTPS and decoded by SDL3_image. Click it to open the decoder's home.",
        24, UI_FONT_ITALIC, 14.0f, MUTED, true));

    UI_Frame *strip = ui_frame(ctx, (UI_FrameConfig){
        .width      = UI_FILL,
        .height     = UI_FIXED(200),
        .direction  = UI_HORIZONTAL,
        .gap        = 12.0f,
    });

    UI_Frame *scene = ui_frame(ctx, (UI_FrameConfig){
        .width         = UI_FILL,
        .height        = UI_FILL,
        .image_src     = IMAGE_WIDE,
        .image_fit     = UI_IMAGE_FILL,
        .border_color  = BORDER,
        .border_width  = 1.0f,
        .corner_radius = 12.0f,
    });

    UI_Frame *icon = ui_frame(ctx, (UI_FrameConfig){
        .width         = UI_FIXED(180),
        .height        = UI_FILL,
        .image_src     = IMAGE_SQUARE,
        .image_fit     = UI_IMAGE_CENTER,
        .background    = SURFACE_ALT,
        .border_color  = BORDER,
        .border_width  = 1.0f,
        .corner_radius = 12.0f,
    });

    ui_add(strip, scene);
    ui_add(strip, icon);
    ui_add(panel, strip);

    ui_add(panel, make_text(ctx,
        "Left fills the box, discarding aspect; the right draws at native pixel size and clips.",
        24, UI_FONT_ITALIC, 14.0f, MUTED, true));

    /* Downloads while the demo runs. The frame paints its background
     * immediately and the image replaces it when the bytes land; a transfer the
     * platform is not allowed to make logs once instead of retrying per frame. */
    UI_Frame *remote = ui_frame(ctx, (UI_FrameConfig){
        .width         = UI_FILL,
        .height        = UI_FIXED(200),
        .image_src     = IMAGE_REMOTE,
        .image_fit     = UI_IMAGE_FIT,
        .background    = SURFACE_ALT,
        .border_color  = BORDER,
        .border_width  = 1.0f,
        .corner_radius = 12.0f,
        .link_url      = "https://developer.mozilla.org/CORS",
    });

    ui_add(panel, make_text(ctx,
        "This frame draws its own background first, then the fetched picture lands on top of it.",
        24, UI_FONT_ITALIC, 14.0f, MUTED, true));
    ui_add(panel, remote);

    return panel;
}

static UI_Frame *make_panel_layout(UI_Context *ctx) {
    UI_Frame *panel = ui_frame(ctx, (UI_FrameConfig){
        .width      = UI_FILL,
        .height     = UI_FILL,
        .direction  = UI_VERTICAL,
        .padding    = UI_ALL(16),
        .gap        = 16.0f,
        .scrollable = true,
    });

    ui_add(panel, make_text(ctx, "Layout gallery", 28, UI_FONT_BOLD, 22.0f, INK, false));

    UI_Frame *align_row = ui_frame(ctx, (UI_FrameConfig){
        .width      = UI_FILL,
        .height     = UI_FIXED(90),
        .direction  = UI_HORIZONTAL,
        .gap        = 12.0f,
        .align_items = UI_ALIGN_CENTER,
    });
    ui_add(align_row, make_box(ctx, 60, 40, INK));
    ui_add(align_row, make_box(ctx, 60, 60, MUTED));
    ui_add(align_row, make_box(ctx, 60, 20, BORDER));
    UI_Frame *align_section = make_section(ctx, "align-items: center");
    ui_add(align_section, align_row);
    ui_add(panel, align_section);

    UI_Frame *end_row = ui_frame(ctx, (UI_FrameConfig){
        .width       = UI_FILL,
        .height      = UI_FIXED(90),
        .direction   = UI_HORIZONTAL,
        .gap         = 12.0f,
        .align_items = UI_ALIGN_END,
        .background  = SURFACE_ALT,
        .corner_radius = 8.0f,
    });
    ui_add(end_row, make_box(ctx, 60, 40, INK));
    ui_add(end_row, make_box(ctx, 60, 60, MUTED));
    ui_add(end_row, make_box(ctx, 60, 20, INK_UP));
    UI_Frame *end_section = make_section(ctx, "align-items: end");
    ui_add(end_section, end_row);
    ui_add(panel, end_section);

    UI_Frame *between_row = ui_frame(ctx, (UI_FrameConfig){
        .width           = UI_FILL,
        .height          = UI_FIXED(60),
        .direction       = UI_HORIZONTAL,
        .justify_content = UI_JUSTIFY_SPACE_BETWEEN,
        .align_items     = UI_ALIGN_CENTER,
        .background      = SURFACE_ALT,
        .corner_radius   = 8.0f,
    });
    ui_add(between_row, make_box(ctx, 48, 32, INK));
    ui_add(between_row, make_box(ctx, 48, 32, MUTED));
    ui_add(between_row, make_box(ctx, 48, 32, INK));
    UI_Frame *between_section = make_section(ctx, "justify-content: space-between");
    ui_add(between_section, between_row);
    ui_add(panel, between_section);

    UI_Frame *hscroll = ui_frame(ctx, (UI_FrameConfig){
        .width      = UI_FILL,
        .height     = UI_FIXED(100),
        .direction  = UI_HORIZONTAL,
        .gap        = 12.0f,
        .align_items = UI_ALIGN_CENTER,
        .background = SURFACE,
        .border_color = BORDER,
        .border_width = 1.0f,
        .corner_radius = 8.0f,
        .scrollable = true,
    });

    for (int i = 0; i < 12; i++) {
        ui_add(hscroll, make_box(ctx, 80, 72, i % 2 ? MUTED : INK));
    }

    UI_Frame *h_section = make_section(ctx, "horizontal scroll (drag the wheel here)");
    ui_add(h_section, hscroll);
    ui_add(panel, h_section);

    return panel;
}

int main(void) {
    UI_Context *ctx = ui_create();

    if (ctx == NULL) {
        return 1;
    }

    UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){
        .width      = UI_FILL,
        .height     = UI_FILL,
        .direction  = UI_VERTICAL,
        .padding    = UI_ALL(28),
        .gap        = 16.0f,
        .background = PAGE,
    });

    UI_Frame *header = ui_frame(ctx, (UI_FrameConfig){
        .width           = UI_FILL,
        .height          = UI_FIXED(48),
        .direction       = UI_HORIZONTAL,
        .padding         = UI_EDGES(10, 16, 10, 16),
        .justify_content = UI_JUSTIFY_SPACE_BETWEEN,
        .align_items     = UI_ALIGN_CENTER,
        .background      = SURFACE,
        .border_color    = BORDER,
        .border_width    = 1.0f,
        .corner_radius   = 12.0f,
    });

    UI_Frame *title = ui_frame(ctx, (UI_FrameConfig){
        .width      = UI_FIXED(200),
        .height     = UI_FIXED(24),
        .text       = "cform-ui demo",
        .text_color = INK,
        .font_size  = 18.0f,
        .font_style = UI_FONT_BOLD,
    });

    UI_Frame *docs = ui_frame(ctx, (UI_FrameConfig){
        .width              = UI_FIXED(96),
        .height             = UI_FIXED(32),
        .background         = SURFACE_ALT,
        .border_color       = BORDER,
        .border_width       = 1.0f,
        .corner_radius      = 8.0f,
        .hover_background   = BORDER,
        .text               = "SDL docs",
        .text_color         = INK,
        .font_size          = 13.0f,
        .link_url           = "https://wiki.libsdl.org/SDL3/FrontPage",
    });

    ui_add(header, title);
    ui_add(header, docs);

    UI_Frame *card = ui_frame(ctx, (UI_FrameConfig){
        .width         = UI_FILL,
        .height        = UI_FILL,
        .min_height    = 240.0f,
        .direction     = UI_VERTICAL,
        .padding       = UI_ALL(20),
        .gap           = 16.0f,
        .background    = SURFACE,
        .border_color  = BORDER,
        .border_width  = 1.0f,
        .corner_radius = 16.0f,
    });

    UI_Frame *question = ui_frame(ctx, (UI_FrameConfig){
        .width      = UI_FILL,
        .height     = UI_FIXED(28),
        .text       = "Which example would you like to see?",
        .text_color = INK,
        .font_size  = 20.0f,
        .font_style = UI_FONT_BOLD,
    });

    UI_Frame *options_row = ui_frame(ctx, (UI_FrameConfig){
        .width         = UI_FILL,
        .height        = UI_FIXED(56),
        .direction     = UI_HORIZONTAL,
        .gap           = 12.0f,
    });

    UI_Frame *panel_text = make_panel_text(ctx);
    UI_Frame *panel_images = make_panel_images(ctx);
    UI_Frame *panel_layout = make_panel_layout(ctx);

    static const char *const option_labels[] = { "Alpha", "Beta", "Gamma" };
    UI_Frame *panels[] = { panel_text, panel_images, panel_layout };

    for (int i = 0; i < 3; i++) {
        UI_Frame *tab = ui_frame(ctx, (UI_FrameConfig){
            .width              = UI_FILL,
            .min_width          = 60.0f,
            .height             = UI_FILL,
            .background         = SURFACE_ALT,
            .border_color       = BORDER,
            .border_width       = 1.0f,
            .corner_radius      = 10.0f,
            .hover_background   = HOVER,
            .pressed_background = PRESSED,
            .text               = option_labels[i],
            .text_color         = INK,
            .font_size          = 16.0f,
            .font_style         = UI_FONT_BOLD,
            .on_click           = select_panel,
            .user_data          = panels[i],
        });
        ui_add(options_row, tab);
    }

    g_detail = ui_frame(ctx, (UI_FrameConfig){
        .width         = UI_FILL,
        .height        = UI_FILL,
        .background    = SURFACE_ALT,
        .border_color  = BORDER,
        .border_width  = 1.0f,
        .corner_radius = 12.0f,
    });
    g_current_panel = panel_text;
    ui_add(g_detail, panel_text);

    ui_add(card, question);
    ui_add(card, options_row);
    ui_add(card, g_detail);

    UI_Frame *footer = ui_frame(ctx, (UI_FrameConfig){
        .width           = UI_FILL,
        .height          = UI_FIXED(48),
        .direction       = UI_HORIZONTAL,
        .justify_content = UI_JUSTIFY_END,
        .align_items     = UI_ALIGN_CENTER,
        .gap             = 12.0f,
    });

    UI_Frame *cancel = ui_frame(ctx, (UI_FrameConfig){
        .width              = UI_FIXED(96),
        .height             = UI_FIXED(40),
        .background         = SURFACE,
        .border_color       = BORDER,
        .border_width       = 1.0f,
        .corner_radius      = 8.0f,
        .hover_background   = SURFACE_ALT,
        .pressed_background = BORDER,
        .text               = "Cancel",
        .text_color         = INK,
        .font_size          = 14.0f,
        .on_click           = on_action,
        .user_data          = (void *)"cancel",
    });

    UI_Frame *confirm = ui_frame(ctx, (UI_FrameConfig){
        .width              = UI_FIXED(120),
        .height             = UI_FIXED(40),
        .background         = INK_UP,
        .corner_radius      = 8.0f,
        .hover_background   = INK,
        .pressed_background = INK,
        .text               = "Confirm",
        .text_color         = ON_INK,
        .font_size          = 14.0f,
        .font_style         = UI_FONT_BOLD,
        .on_click           = on_action,
        .user_data          = (void *)"confirm",
    });

    ui_add(footer, cancel);
    ui_add(footer, confirm);

    ui_add(root, header);
    ui_add(root, card);
    ui_add(root, footer);

    ui_render(ctx, root);
    ui_destroy(ctx);

    return 0;
}
