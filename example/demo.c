#include <cform/cform.h>

#define GREY_0 UI_COLOR(18, 20, 24)
#define GREY_1 UI_COLOR(34, 37, 43)
#define GREY_2 UI_COLOR(52, 56, 63)
#define GREY_3 UI_COLOR(74, 79, 88)
#define GREY_4 UI_COLOR(100, 106, 116)

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
        .width      = UI_FILL,
        .height     = UI_FILL,
        .direction  = UI_VERTICAL,
        .margin     = UI_ALL(16),
        .padding    = UI_ALL(24),
        .background = GREY_1,
    });

    UI_Frame *b = ui_frame(ctx, (UI_FrameConfig){
        .width      = UI_FILL,
        .height     = UI_FILL,
        .direction  = UI_VERTICAL,
        .margin     = UI_ALL(16),
        .padding    = UI_ALL(16),
        .background = GREY_2,
    });

    UI_Frame *c = ui_frame(ctx, (UI_FrameConfig){
        .width      = UI_FILL,
        .height     = UI_FILL,
        .margin     = UI_ALL(12),
        .background = GREY_3,
    });

    UI_Frame *core = ui_frame(ctx, (UI_FrameConfig){
        .width      = UI_FILL,
        .height     = UI_FILL,
        .background = GREY_4,
    });

    ui_add(root, a);
    ui_add(a, b);
    ui_add(b, c);
    ui_add(c, core);

    ui_render(ctx, root);
    ui_destroy(ctx);

    return 0;
}
