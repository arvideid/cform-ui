#include <stdio.h>

#include <cform/cform.h>

#define BG        UI_COLOR(18, 20, 24)
#define PANEL     UI_COLOR(34, 37, 43)
#define PANEL_2   UI_COLOR(52, 56, 63)
#define PANEL_3   UI_COLOR(74, 79, 88)
#define FG        UI_COLOR(100, 106, 116)
#define ACCENT    UI_COLOR(96, 165, 250)

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
        .background = BG,
    });

    UI_Frame *header = ui_frame(ctx, (UI_FrameConfig){
        .width          = UI_FILL,
        .height         = UI_FIXED(48),
        .direction      = UI_HORIZONTAL,
        .padding        = UI_EDGES(12, 16, 12, 16),
        .justify_content = UI_JUSTIFY_SPACE_BETWEEN,
        .background     = PANEL,
        .corner_radius  = 12.0f,
    });

    UI_Frame *title = ui_frame(ctx, (UI_FrameConfig){
        .width         = UI_FIXED(180),
        .height        = UI_FIXED(24),
        .background    = FG,
        .corner_radius = 4.0f,
    });

    UI_Frame *gear = ui_frame(ctx, (UI_FrameConfig){
        .width         = UI_FIXED(24),
        .height        = UI_FIXED(24),
        .background    = PANEL_2,
        .border_color  = FG,
        .border_width  = 1.0f,
        .corner_radius = 4.0f,
        .on_click      = on_action,
        .user_data     = (void *)"settings",
    });

    UI_Frame *card = ui_frame(ctx, (UI_FrameConfig){
        .width         = UI_FILL,
        .height        = UI_FILL,
        .min_height    = 240.0f,
        .direction     = UI_VERTICAL,
        .padding       = UI_ALL(20),
        .gap           = 16.0f,
        .background    = PANEL,
        .border_color  = PANEL_2,
        .border_width  = 1.0f,
        .corner_radius = 16.0f,
    });

    UI_Frame *question = ui_frame(ctx, (UI_FrameConfig){
        .width         = UI_FIXED(240),
        .height        = UI_FIXED(24),
        .background    = FG,
        .corner_radius = 4.0f,
    });

    static const char *option_labels[] = { "option a", "option b", "option c" };
    UI_Frame *options_row = ui_frame(ctx, (UI_FrameConfig){
        .width      = UI_FILL,
        .height     = UI_FIXED(64),
        .max_height = 80.0f,
        .direction  = UI_HORIZONTAL,
        .gap        = 12.0f,
        .background = BG,
    });

    UI_Frame *option_a = ui_frame(ctx, (UI_FrameConfig){
        .width         = UI_FILL,
        .min_width     = 60.0f,
        .height        = UI_FIXED(64),
        .background    = PANEL_2,
        .border_color  = PANEL_3,
        .border_width  = 1.0f,
        .corner_radius = 8.0f,
        .on_click      = on_action,
        .user_data     = (void *)option_labels[0],
    });

    UI_Frame *option_b = ui_frame(ctx, (UI_FrameConfig){
        .width         = UI_FILL,
        .min_width     = 60.0f,
        .height        = UI_FIXED(64),
        .background    = PANEL_2,
        .border_color  = PANEL_3,
        .border_width  = 1.0f,
        .corner_radius = 8.0f,
        .on_click      = on_action,
        .user_data     = (void *)option_labels[1],
    });

    UI_Frame *option_c = ui_frame(ctx, (UI_FrameConfig){
        .width         = UI_FILL,
        .min_width     = 60.0f,
        .height        = UI_FIXED(64),
        .background    = PANEL_2,
        .border_color  = PANEL_3,
        .border_width  = 1.0f,
        .corner_radius = 8.0f,
        .on_click      = on_action,
        .user_data     = (void *)option_labels[2],
    });

    UI_Frame *detail = ui_frame(ctx, (UI_FrameConfig){
        .width         = UI_FILL,
        .height        = UI_FILL,
        .background    = PANEL_2,
        .border_color  = FG,
        .border_width  = 1.0f,
        .corner_radius = 8.0f,
        .on_click      = on_action,
        .user_data     = (void *)"detail",
    });

    UI_Frame *footer = ui_frame(ctx, (UI_FrameConfig){
        .width           = UI_FILL,
        .height          = UI_FIXED(40),
        .direction       = UI_HORIZONTAL,
        .justify_content = UI_JUSTIFY_END,
        .gap             = 12.0f,
        .background      = BG,
    });

    UI_Frame *cancel = ui_frame(ctx, (UI_FrameConfig){
        .width         = UI_FIXED(96),
        .height        = UI_FIXED(40),
        .max_width     = 120.0f,
        .background    = PANEL_2,
        .border_color  = PANEL_3,
        .border_width  = 1.0f,
        .corner_radius = 6.0f,
        .on_click      = on_action,
        .user_data     = (void *)"cancel",
    });

    UI_Frame *confirm = ui_frame(ctx, (UI_FrameConfig){
        .width         = UI_FIXED(120),
        .height        = UI_FIXED(40),
        .max_width     = 160.0f,
        .background    = PANEL_2,
        .border_color  = ACCENT,
        .border_width  = 2.0f,
        .corner_radius = 6.0f,
        .on_click      = on_action,
        .user_data     = (void *)"confirm",
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
