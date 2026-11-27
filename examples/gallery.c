/*
 * One page that reads top to bottom: a rule of the framework, the words that
 * say what it does, and the box underneath that lets a hand check it. The
 * document is a single scroller, so the wheel is the only navigation.
 *
 * What is here is only the program around that page: the font the configure
 * step fetched, the window, the image the backend reads, and the shell that
 * runs the frames. The page itself lives in gallery_page.h, shared as source
 * with the headless tier so the two drivers cannot hold a page each, and what
 * the page reads is the one block this program keeps on its own stack.
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

    /* The faces are opened here and named once, so the page is declared from what its
     * host holds rather than from anything the file keeps between runs. */
    gallery_host host;
    host.theme = cf_theme_default();
    gallery_host_open(&host, cf_font_sdl(CFORM_GALLERY_FONT, BODY_POINT),
                      cf_font_sdl(CFORM_GALLERY_FONT, TITLE_POINT));

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
        if (!backend.load_image(backend.user_data, argv[1], &host.logo)) {
            printf("gallery: %s is not an image this backend reads\n", argv[1]);
        }
    } else {
        host.logo = (cf_image_content){
            .texture = backend.create_texture(backend.user_data, 8u, 8u, host.checker),
            .width = 32.0f,
            .height = 32.0f};
    }

    /* The desk's clipboard, named for the page: the field in chapter two copies,
     * cuts and pastes through it, and a host that named none would leave those keys
     * asking for nothing. The shell borrows the pair, so it lives as long as the run.
     */
    const cf_clipboard clipboard = cf_clipboard_sdl();

    cf_app_run(&(cf_app_config){.build = build,
                                .build_user_data = &host,
                                .backend = backend,
                                .theme = host.theme,
                                .fonts = &host.body,
                                .clipboard = &clipboard});

    /* The shell borrowed the backend and has returned, so the upload the page
     * made and the window the backend opened are the host's to hand back. A
     * window that is closed without this keeps its device bytes until the
     * process ends, which is what a long-lived host cannot afford.
     */
    if (backend.release_texture != NULL) {
        backend.release_texture(backend.user_data, host.logo.texture);
    }

    /* The fonts go before the window: both were opened on the same platform, and
     * a teardown that stops it leaves a font with nothing to close through. */
    cf_font_sdl_close(&host.body_font);
    cf_font_sdl_close(&host.title_font);

    if (backend.destroy != NULL) {
        backend.destroy(backend.user_data);
    }
}
