#include "cf/style.h"

/*
 * A static initializer must be a constant expression, which the runtime
 * constructors CF_COLOR and CF_PX are not: the theme spells its colors as
 * four bytes and its lengths as kind and value, through these two.
 */
#define RGBA(red, green, blue) {(red), (green), (blue), 255u}
#define PX(value) {CF_LENGTH_PX, (value)}

/*
 * 60-30-10 on one neutral scale: #f5f5f5 is the dominant field, #fafafa the
 * cards that lift off it, #e5e5e5 the wells that sit below them. #171717 is
 * the single accent, reserved for the primary action, and #d4d4d4 the
 * hairline every box edges with. Text tones are chosen so every fill clears
 * WCAG-AA against its label: #0a0a0a on #fafafa is 19:1, #fafafa on #171717
 * is 16:1, #525252 on #fafafa is 7.9:1.
 */

/*
 * A resting box states its margins: the zero length reads as auto, and auto
 * margins absorb the container's free space, which would center the widget
 * instead of letting its container place it. Every box this theme hands out is
 * a resting box, the three surfaces above them included - a card that hugs its
 * label instead of filling its column is the same mistake wearing a border.
 */
#define MARGINS {PX(0.0f), PX(0.0f), PX(0.0f), PX(0.0f)}

static const cf_theme
    theme =
        {
            .page = {.background = {.present = true, .value = RGBA(245, 245, 245)},
                     .margin = MARGINS},
            .bar = {.background = {.present = true, .value = RGBA(229, 229, 229)},
                    .margin = MARGINS,
                    .radius = 8.0f},
            .body = {.background = {.present = true, .value = RGBA(250, 250, 250)},
                     .border_color = {.present = true, .value = RGBA(212, 212, 212)},
                     .border = {PX(1.0f), PX(1.0f), PX(1.0f), PX(1.0f)},
                     .margin = MARGINS,
                     .radius = 8.0f},
            .ink = RGBA(10, 10, 10),
            .muted = RGBA(82, 82, 82),
            .on_ink = RGBA(250, 250, 250),
            .variants =
                {
                    [CF_ROLE_BUTTON] =
                        {.base = {.background = {.present = true, .value = RGBA(23, 23, 23)},
                                  .border_color = {.present = true, .value = RGBA(23, 23, 23)},
                                  .border = {PX(1.0f), PX(1.0f), PX(1.0f), PX(1.0f)},
                                  .padding =
                                      {PX(12.0f), PX(8.0f), PX(12.0f), PX(8.0f)},
                                  .margin =
                                      MARGINS,
                                  .radius =
                                      6.0f},
                         .hovered =
                             {.background = {.present = true, .value = RGBA(46, 46, 46)}},
                         .pressed =
                             {.background = {.present = true, .value = RGBA(10, 10, 10)}},
                         .focused =
                             {.border_color = {.present = true, .value = RGBA(82, 82, 82)}},
                         .disabled = {.background = {.present = true, .value = RGBA(229, 229, 229)},
                                      .border_color = {.present = true,
                                                       .value = RGBA(212, 212, 212)}}},
                    [CF_ROLE_FIELD] =
                        {
                            .base = {.background = {.present = true, .value = RGBA(250, 250, 250)},
                                     .border_color = {.present = true,
                                                      .value = RGBA(212, 212, 212)},
                                     .border = {PX(1.0f), PX(1.0f), PX(1.0f), PX(1.0f)},
                                     .padding = {PX(8.0f), PX(6.0f), PX(8.0f), PX(6.0f)},
                                     .margin = MARGINS,
                                     .radius = 6.0f},
                            .hovered = {.border_color = {.present = true,
                                                         .value = RGBA(163, 163, 163)}},
                            .focused = {.border_color = {.present = true,
                                                         .value = RGBA(10, 10, 10)}},
                            .disabled = {.background = {.present = true,
                                                        .value = RGBA(229, 229, 229)}}},
                    [CF_ROLE_LIST] = {.hovered = {.background = {.present = true,
                                                                 .value = RGBA(229, 229, 229)}}},
                    [CF_ROLE_SCROLL] =
                        {.base = {.background = {.present = true, .value = RGBA(212, 212, 212)},
                                  .margin = MARGINS,
                                  .radius = 3.0f},
                         .hovered = {.background = {.present = true, .value = RGBA(163, 163, 163)}},
                         .pressed = {.background = {.present = true, .value = RGBA(82, 82, 82)}}},
                    [CF_ROLE_IMAGE] = {.base = {.background = {.present = true,
                                                               .value = RGBA(229, 229, 229)},
                                                .margin = MARGINS}},
                },
};

const cf_theme *cf_theme_default(void) {
    return &theme;
}
