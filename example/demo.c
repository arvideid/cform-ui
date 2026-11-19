#include <stdio.h>

#include <cform/cform.h>

/* Vercel-style palette. */
#define PURE_BLACK   UI_COLOR(9, 9, 11)
#define CHARCOAL     UI_COLOR(24, 24, 27)
#define CHARCOAL_UP  UI_COLOR(39, 39, 42)
#define CHARCOAL_DN  UI_COLOR(16, 16, 18)
#define SLATE        UI_COLOR(113, 113, 122)
#define LIGHT_BORDER UI_COLOR(228, 228, 231)
#define CRISP_WHITE  UI_COLOR(250, 250, 250)

/* Subtle hover wash over a charcoal surface: light border at low alpha. */
static const UI_Color HOVER_WASH = { 228, 228, 231, 24 };
static const UI_Color PRESSED_WASH = { 9, 9, 11, 255 };

static int action_count;

static void on_action(UI_Frame *frame, void *user_data) {
    (void)frame;
    printf("%s clicked: %d\n", (const char *)user_data, ++action_count);
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
        .padding    = UI_ALL(32),
        .gap        = 16.0f,
        .background = PURE_BLACK,
    });

    UI_Frame *header = ui_frame(ctx, (UI_FrameConfig){
        .width           = UI_FILL,
        .height          = UI_FIXED(48),
        .direction       = UI_HORIZONTAL,
        .padding         = UI_EDGES(12, 16, 12, 16),
        .justify_content = UI_JUSTIFY_SPACE_BETWEEN,
        .align_items     = UI_ALIGN_CENTER,
        .background      = CHARCOAL,
        .corner_radius   = 12.0f,
    });

    UI_Frame *title = ui_frame(ctx, (UI_FrameConfig){
        .width      = UI_FIXED(180),
        .height     = UI_FIXED(24),
        .text       = "cform-ui demo",
        .text_color = CRISP_WHITE,
        .font_size  = 18.0f,
    });

    UI_Frame *gear = ui_frame(ctx, (UI_FrameConfig){
        .width              = UI_FIXED(28),
        .height             = UI_FIXED(28),
        .background         = CHARCOAL,
        .border_color       = SLATE,
        .border_width       = 1.0f,
        .corner_radius      = 6.0f,
        .hover_background   = CHARCOAL_UP,
        .pressed_background = CHARCOAL_DN,
        .text               = "i",
        .text_color         = SLATE,
        .font_size          = 14.0f,
        .on_click           = on_action,
        .user_data          = (void *)"settings",
    });

    UI_Frame *card = ui_frame(ctx, (UI_FrameConfig){
        .width         = UI_FILL,
        .height        = UI_FILL,
        .min_height    = 240.0f,
        .direction     = UI_VERTICAL,
        .padding       = UI_ALL(24),
        .gap           = 16.0f,
        .background    = CHARCOAL,
        .border_color  = SLATE,
        .border_width  = 1.0f,
        .corner_radius = 16.0f,
    });

    UI_Frame *question = ui_frame(ctx, (UI_FrameConfig){
        .width      = UI_FILL,
        .height     = UI_FIXED(28),
        .text       = "Which option feels right?",
        .text_color = CRISP_WHITE,
        .font_size  = 20.0f,
    });

    UI_Frame *options_row = ui_frame(ctx, (UI_FrameConfig){
        .width         = UI_FILL,
        .height        = UI_FIXED(80),
        .max_height    = 96.0f,
        .direction     = UI_HORIZONTAL,
        .padding       = UI_ALL(12),
        .gap           = 12.0f,
        .background    = PURE_BLACK,
        .border_color  = SLATE,
        .border_width  = 1.0f,
        .corner_radius = 12.0f,
    });

    static const char *option_labels[] = { "Alpha", "Beta", "Gamma" };

    UI_Frame *option_a = ui_frame(ctx, (UI_FrameConfig){
        .width              = UI_FILL,
        .min_width          = 60.0f,
        .height             = UI_FILL,
        .background         = CHARCOAL,
        .border_color       = SLATE,
        .border_width       = 1.0f,
        .corner_radius      = 8.0f,
        .hover_background   = CHARCOAL_UP,
        .pressed_background = CHARCOAL_DN,
        .text               = option_labels[0],
        .text_color         = CRISP_WHITE,
        .font_size          = 16.0f,
        .on_click           = on_action,
        .user_data          = (void *)option_labels[0],
    });

    UI_Frame *option_b = ui_frame(ctx, (UI_FrameConfig){
        .width              = UI_FILL,
        .min_width          = 60.0f,
        .height             = UI_FILL,
        .background         = CHARCOAL,
        .border_color       = SLATE,
        .border_width       = 1.0f,
        .corner_radius      = 8.0f,
        .hover_background   = CHARCOAL_UP,
        .pressed_background = CHARCOAL_DN,
        .text               = option_labels[1],
        .text_color         = CRISP_WHITE,
        .font_size          = 16.0f,
        .on_click           = on_action,
        .user_data          = (void *)option_labels[1],
    });

    UI_Frame *option_c = ui_frame(ctx, (UI_FrameConfig){
        .width              = UI_FILL,
        .min_width          = 60.0f,
        .height             = UI_FILL,
        .background         = CHARCOAL,
        .border_color       = SLATE,
        .border_width       = 1.0f,
        .corner_radius      = 8.0f,
        .hover_background   = CHARCOAL_UP,
        .pressed_background = CHARCOAL_DN,
        .text               = option_labels[2],
        .text_color         = CRISP_WHITE,
        .font_size          = 16.0f,
        .on_click           = on_action,
        .user_data          = (void *)option_labels[2],
    });

    UI_Frame *detail = ui_frame(ctx, (UI_FrameConfig){
        .width              = UI_FILL,
        .height             = UI_FILL,
        .background         = CHARCOAL,
        .border_color       = SLATE,
        .border_width       = 1.0f,
        .corner_radius      = 8.0f,
        .hover_background   = HOVER_WASH,
        .pressed_background = PRESSED_WASH,
        .text               = "detail panel - click me",
        .text_color         = SLATE,
        .font_size          = 14.0f,
        .on_click           = on_action,
        .user_data          = (void *)"detail",
    });

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
        .max_width          = 120.0f,
        .background         = CHARCOAL,
        .border_color       = SLATE,
        .border_width       = 1.0f,
        .corner_radius      = 8.0f,
        .hover_background   = CHARCOAL_UP,
        .pressed_background = CHARCOAL_DN,
        .text               = "Cancel",
        .text_color         = CRISP_WHITE,
        .font_size          = 14.0f,
        .on_click           = on_action,
        .user_data          = (void *)"cancel",
    });

    UI_Frame *confirm = ui_frame(ctx, (UI_FrameConfig){
        .width              = UI_FIXED(120),
        .height             = UI_FIXED(40),
        .max_width          = 160.0f,
        .background         = CRISP_WHITE,
        .corner_radius      = 8.0f,
        .hover_background   = LIGHT_BORDER,
        .pressed_background = CHARCOAL_UP,
        .text               = "Confirm",
        .text_color         = PURE_BLACK,
        .font_size          = 14.0f,
        .on_click           = on_action,
        .user_data          = (void *)"confirm",
    });

    ui_add(header, title);
    ui_add(header, gear);

    ui_add(options_row, option_a);
    ui_add(options_row, option_b);
    ui_add(options_row, option_c);

    ui_add(card, question);
    ui_add(card, options_row);
    ui_add(card, detail);

    ui_add(footer, cancel);
    ui_add(footer, confirm);

    ui_add(root, header);
    ui_add(root, card);
    ui_add(root, footer);

    ui_render(ctx, root);
    ui_destroy(ctx);

    return 0;
}
