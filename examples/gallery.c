/*
 * One page that reads top to bottom: a rule of the framework, the words that
 * say what it does, and the box underneath that lets a hand check it. The
 * document is a single scroller, so the wheel is the only navigation.
 *
 * What is here is only the program around that page: the font the configure
 * step fetched, the window, the image the backend reads, and the shell that
 * runs the frames. The page itself lives in gallery_page.h, shared as source
 * with the headless tier so the two drivers cannot hold a page each.
 */
#include <stdio.h>

#include "cf/app.h"
#include "gallery_page.h"

/* Point sizes the two stacks are read at, and nothing else about them: the
 * file is the configure step's path, and offline it opens nothing. */
#define BODY_POINT 14.0f
#define TITLE_POINT 22.0f

int main(int argc, char **argv) {
    /* Route the library's diagnostics through SDL's log viewer. A host that
     * owns a sink of its own would leave this out and the backend would not
     * overwrite it. */
    cf_sdl_install_log_sink();

    theme = cf_theme_default();
    body_font = cf_font_sdl(CFORM_GALLERY_FONT, BODY_POINT);
    title_font = cf_font_sdl(CFORM_GALLERY_FONT, TITLE_POINT);
    body = (cf_text_stack){.fonts = &body_font, .count = body_font.ops != NULL ? 1u : 0u};
    title = (cf_text_stack){.fonts = &title_font, .count = title_font.ops != NULL ? 1u : 0u};
    gallery_init_assets();

    const cf_backend backend = cf_backend_sdl(&(cf_sdl_config){.title = "cform gallery",
                                                               .width = WINDOW_WIDTH,
                                                               .height = WINDOW_HEIGHT,
                                                               .min_width = WINDOW_MIN_WIDTH,
                                                               .min_height = WINDOW_MIN_HEIGHT});
    if (backend.submit == NULL) {
        printf("gallery: no window\n");
        return 1;
    }

    /* An image is the backend's to read, so a path named on the command line
     * goes through its decoder and lands as a texture. Without one the page
     * still has a texture to show - a pattern uploaded here, in the bytes the
     * slot promises - because a demo that needs an asset it cannot ship is a
     * demo that does not run. */
    if (argc > 1 && backend.load_image != NULL) {
        if (!backend.load_image(backend.user_data, argv[1], &logo)) {
            printf("gallery: %s is not an image this backend reads\n", argv[1]);
        }
    } else {
        logo = (cf_image_content){.texture =
                                      backend.create_texture(backend.user_data, 8u, 8u, checker),
                                  .width = 32.0f,
                                  .height = 32.0f};
    }

    cf_app_run(
        &(cf_app_config){.build = build, .backend = backend, .theme = theme, .fonts = &body});

    /* The shell borrowed the backend and has returned, so the upload the page
     * made and the window the backend opened are the host's to hand back. A
     * window that is closed without this keeps its device bytes until the
     * process ends, which is what a long-lived host cannot afford.
     */
    if (backend.release_texture != NULL) {
        backend.release_texture(backend.user_data, logo.texture);
    }

    if (backend.destroy != NULL) {
        backend.destroy(backend.user_data);
    }
}
