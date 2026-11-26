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
 * is 16:1, #525252 on #fafafa is 7.9:1, and the selection band #bfdbfe under
 * the same ink is 12:1.
 */

/*
 * A resting box states its margins: the zero length reads as auto, and auto
 * margins absorb the container's free space, which would center the widget
 * instead of letting its container place it. Every box this theme hands out is
 * a resting box, the three surfaces above them included - a card that hugs its
 * label instead of filling its column is the same mistake wearing a border.
 *
 * It states its shrink for the same reason: zero here means "refuse to give
 * ground", so a row of themed boxes whose widths do not fit runs past its
 * container instead of reflowing, and a window that gets narrower loses content
 * rather than repacking it. CSS's initial is that every item shrinks.
 */
#define MARGINS {PX(0.0f), PX(0.0f), PX(0.0f), PX(0.0f)}
#define SHRINK 1.0f

static const cf_theme
    theme =
        {
            .page = {.background = {.present = true, .value = RGBA(245, 245, 245)},
                     .margin = MARGINS,
                     .flex_shrink = SHRINK},
            .bar = {.background = {.present = true, .value = RGBA(229, 229, 229)},
                    .margin = MARGINS,
                    .flex_shrink = SHRINK,
                    .radius = 8.0f},
            .body = {.background = {.present = true, .value = RGBA(250, 250, 250)},
                     .border_color = {.present = true, .value = RGBA(212, 212, 212)},
                     .border = {PX(1.0f), PX(1.0f), PX(1.0f), PX(1.0f)},
                     .margin = MARGINS,
                     .flex_shrink = SHRINK,
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
                                  .flex_shrink = SHRINK,
                                  .radius =
                                      6.0f},
                         .hovered =
                             {.background = {.present = true, .value = RGBA(46, 46, 46)}},
                         .pressed =
                             {.background = {.present = true, .value = RGBA(10, 10, 10)}},
                         /* The ring a keyboard hand leaves behind: a mid grey that clears
                          * roughly 3:1 against both the button's own dark fill and the light
                          * surface it rests on, so the edge reads on either side instead of
                          * vanishing into the fill the way a darker or lighter line would. */
                         .focused =
                             {.border_color = {.present = true, .value = RGBA(130, 130, 130)}},
                         .disabled = {.background = {.present = true, .value = RGBA(229, 229, 229)},
                                      .border_color = {.present = true,
                                                       .value = RGBA(212, 212, 212)}}},
                    [CF_ROLE_FIELD] =
                        {.base = {.background = {.present = true, .value = RGBA(250, 250, 250)},
                                  .border_color = {.present = true, .value = RGBA(212, 212, 212)},
                                  .border = {PX(1.0f), PX(1.0f), PX(1.0f), PX(1.0f)},
                                  .padding = {PX(8.0f), PX(6.0f), PX(8.0f), PX(6.0f)},
                                  .margin = MARGINS,
                                  .flex_shrink = SHRINK,
                                  .radius = 6.0f,
                                  /* A value longer than the box slides under its edge
                                   * rather than past its border. */
                                  .clips_content = true},
                         .hovered = {.border_color = {.present = true,
                                                      .value = RGBA(163, 163, 163)}},
                         .focused = {.border_color = {.present = true, .value = RGBA(10, 10, 10)},
                                     /* A caret is only worth showing to whoever can type:
                                      * the focus books both marks, and the caret takes the
                                      * ink the bytes are drawn in, which is CSS's default
                                      * for caret-color. */
                                     .caret = {.present = true, .value = RGBA(10, 10, 10)},
                                     .selection = {.present = true, .value = RGBA(191, 219, 254)}},
                         .disabled =
                             {.background = {.present = true, .value = RGBA(229, 229, 229)}}},
                    [CF_ROLE_LIST] = {.hovered = {.background = {.present = true,
                                                                 .value = RGBA(229, 229, 229)}}},
                    /* The role styles the scrollable container, not the thumb. A pane that
                     * darkens wherever the pointer happens to rest is not a control, and the
                     * thumb that answers a drag carries its own styling; so the container
                     * paints its track and nothing more under hover or a press. */
                    [CF_ROLE_SCROLL] = {.base = {.background = {.present = true,
                                                                .value = RGBA(212, 212, 212)},
                                                 .margin = MARGINS,
                                                 .flex_shrink = SHRINK,
                                                 .radius = 3.0f}},
                    [CF_ROLE_IMAGE] = {.base = {.background = {.present = true,
                                                               .value = RGBA(229, 229, 229)},
                                                .margin = MARGINS,
                                                .flex_shrink = SHRINK}},
                },
};

const cf_theme *cf_theme_default(void) {
    return &theme;
}
