#include <cform/cform.h>

#define GREY_0 UI_COLOR(18, 20, 24)
#define GREY_1 UI_COLOR(34, 37, 43)
#define GREY_2 UI_COLOR(52, 56, 63)
#define GREY_3 UI_COLOR(74, 79, 88)
#define GREY_4 UI_COLOR(100, 106, 116)
#define ACCENT UI_COLOR(96, 165, 250)

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
        .background = GREY_0,
    });

    UI_Frame *a = ui_frame(ctx, (UI_FrameConfig){
        .width        = UI_FILL,
        .height       = UI_FILL,
        .direction    = UI_VERTICAL,
        .margin       = UI_ALL(16),
        .padding      = UI_ALL(24),
        .background   = GREY_1,
        .border_color = UI_COLOR(55, 60, 70),
        .border_width = 1.0f,
        .corner_radius = 16.0f,
    });

    UI_Frame *b = ui_frame(ctx, (UI_FrameConfig){
        .width         = UI_FILL,
        .height        = UI_FILL,
        .direction     = UI_VERTICAL,
        .margin        = UI_ALL(16),
        .padding       = UI_ALL(16),
        .background    = GREY_2,
        .corner_radius = 12.0f,
    });

    UI_Frame *c = ui_frame(ctx, (UI_FrameConfig){
        .width         = UI_FILL,
        .height        = UI_FILL,
        .margin        = UI_ALL(12),
        .min_width     = 200.0f,
        .max_height    = 240.0f,
        .background    = GREY_3,
        .border_color  = ACCENT,
        .border_width  = 2.0f,
        .corner_radius = 8.0f,
    });

    UI_Frame *core = ui_frame(ctx, (UI_FrameConfig){
        .width         = UI_FILL,
        .height        = UI_FILL,
        .background    = GREY_4,
        .corner_radius = 4.0f,
    });

    ui_add(root, a);
    ui_add(a, b);
    ui_add(b, c);
    ui_add(c, core);

    ui_render(ctx, root);
    ui_destroy(ctx);

    return 0;
}
