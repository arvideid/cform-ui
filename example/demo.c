/*
 * cform-ui demo - padding vs margin, the simple way.
 *
 * A single column of nested boxes. Every box fills its parent, so the only
 * thing you see is the space each level creates:
 *
 *   - padding  = space *inside* a frame, between its edge and its content
 *   - margin   = space *outside* a frame, so the parent's color shows through
 *
 * Spacing follows a 4px scale (4, 8, 12, 16, 24, 32...).
 * Colors are one coherent grey ramp: identical hue, stepping lightness, so
 * neighboring regions never clash.
 */
#include <cform/cform.h>

/* Coherent monochrome palette - only the lightness changes. */
#define GREY_0 UI_COLOR(18, 20, 24)   /* outermost */
#define GREY_1 UI_COLOR(34, 37, 43)
#define GREY_2 UI_COLOR(52, 56, 63)
#define GREY_3 UI_COLOR(74, 79, 88)
#define GREY_4 UI_COLOR(100, 106, 116) /* core */

int main(void) {
    UI_Context *ctx = ui_create();

    if (ctx == NULL) {
        return 1;
    }

    /* Root: 32px of padding shows GREY_0 as a band before any child appears. */
    UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){
        .width      = UI_FILL,
        .height     = UI_FILL,
        .direction  = UI_VERTICAL,
        .padding    = UI_ALL(32),
        .background = GREY_0,
    });

    /* A: 16px margin (GREY_0 shows through) + 24px padding (GREY_1 band). */
    UI_Frame *a = ui_frame(ctx, (UI_FrameConfig){
        .width      = UI_FILL,
        .height     = UI_FILL,
        .direction  = UI_VERTICAL,
        .margin     = UI_ALL(16),
        .padding    = UI_ALL(24),
        .background = GREY_1,
    });

    /* B: same idea, tighter. */
    UI_Frame *b = ui_frame(ctx, (UI_FrameConfig){
        .width      = UI_FILL,
        .height     = UI_FILL,
        .direction  = UI_VERTICAL,
        .margin     = UI_ALL(16),
        .padding    = UI_ALL(16),
        .background = GREY_2,
    });

    /* C: the innermost box, just a margin to lift it off GREY_2. */
    UI_Frame *c = ui_frame(ctx, (UI_FrameConfig){
        .width      = UI_FILL,
        .height     = UI_FILL,
        .margin     = UI_ALL(12),
        .background = GREY_3,
    });

    /* Core: a solid fill block with no spacing of its own. */
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
