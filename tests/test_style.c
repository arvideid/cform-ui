#include "cform_test.h"

#include <cform/cform.h>
#include "internal.h"

static UI_Frame *chain(UI_Context *ctx, UI_Frame **out, int count, float *opacity) {
    for (int i = 0; i < count; i++) {
        out[i] = ui_frame(ctx, (UI_FrameConfig){ .opacity = opacity[i] });

        if (i > 0) {
            ui_add(out[i - 1], out[i]);
        }
    }

    return out[count - 1];
}

static void unset_opacity_is_fully_opaque(void) {
    UI_Context *ctx = ui_create();

    UI_Frame *leaf;
    float values[] = { 0.0f };
    chain(ctx, &leaf, 1, values);

    CF_NEAR(ui_opacity(leaf), 1.0);

    ui_destroy(ctx);
}

static void opacity_multiplies_up_the_tree(void) {
    UI_Context *ctx = ui_create();

    UI_Frame *frames[3];
    float values[] = { 0.5f, 0.5f, 0.5f };
    UI_Frame *leaf = chain(ctx, frames, 3, values);

    CF_NEAR(ui_opacity(frames[0]), 0.5);
    CF_NEAR(ui_opacity(frames[1]), 0.25);
    CF_NEAR(ui_opacity(leaf), 0.125);

    ui_destroy(ctx);
}

static void ancestor_above_one_clamps_but_does_not_reset_descendants(void) {
    UI_Context *ctx = ui_create();

    UI_Frame *frames[2];
    float values[] = { 4.0f, 0.5f };
    UI_Frame *leaf = chain(ctx, frames, 2, values);

    CF_NEAR(ui_opacity(frames[0]), 1.0);
    CF_NEAR(ui_opacity(leaf), 0.5);

    ui_destroy(ctx);
}

static void opacity_of_detached_frame_ignores_old_ancestors(void) {
    UI_Context *ctx = ui_create();

    UI_Frame *frames[2];
    float values[] = { 0.5f, 0.5f };
    UI_Frame *leaf = chain(ctx, frames, 2, values);

    ui_remove(frames[0], leaf);

    CF_NEAR(ui_opacity(leaf), 0.5);

    ui_destroy(ctx);
}

static void null_frame_is_opaque(void) {
    CF_NEAR(ui_opacity(NULL), 1.0);
}

static void scaling_alpha_multiplies_and_rounds(void) {
    UI_Color red = { 200, 100, 50, 200 };

    CF_NEAR(ui_color_scale_alpha(red, 0.5f).a, 100);
    CF_NEAR(ui_color_scale_alpha(red, 1.0f).a, 200);
}

static void scaling_alpha_leaves_rgb_untouched(void) {
    UI_Color red = { 200, 100, 50, 200 };
    UI_Color faded = ui_color_scale_alpha(red, 0.25f);

    CF_NEAR(faded.r, 200);
    CF_NEAR(faded.g, 100);
    CF_NEAR(faded.b, 50);
}

static void fully_transparent_color_stays_transparent(void) {
    UI_Color clear = { 10, 20, 30, 0 };

    CF_NEAR(ui_color_scale_alpha(clear, 0.5f).a, 0);
    CF_NEAR(ui_color_scale_alpha(clear, 1.0f).a, 0);
}

static void out_of_range_factors_clamp_instead_of_wrapping(void) {
    UI_Color red = { 200, 100, 50, 200 };

    CF_NEAR(ui_color_scale_alpha(red, -3.0f).a, 0);
    CF_NEAR(ui_color_scale_alpha(red, 9.0f).a, 200);

    UI_Color opaque = UI_COLOR(1, 2, 3);
    CF_NEAR(ui_color_scale_alpha(opaque, 1.0f).a, 255);
}

int main(void) {
    printf("test_style\n");

    CF_RUN(unset_opacity_is_fully_opaque);
    CF_RUN(opacity_multiplies_up_the_tree);
    CF_RUN(ancestor_above_one_clamps_but_does_not_reset_descendants);
    CF_RUN(opacity_of_detached_frame_ignores_old_ancestors);
    CF_RUN(null_frame_is_opaque);
    CF_RUN(scaling_alpha_multiplies_and_rounds);
    CF_RUN(scaling_alpha_leaves_rgb_untouched);
    CF_RUN(fully_transparent_color_stays_transparent);
    CF_RUN(out_of_range_factors_clamp_instead_of_wrapping);

    return CF_REPORT();
}
