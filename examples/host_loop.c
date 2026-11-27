/*
 * The framework without the framework's loop: a program that keeps its own context, fills the
 * backend struct with its own callbacks and decides when a frame is worth running. This is the
 * route a renderer that already has a main loop takes, and it is the second proof of the
 * agreement - the first being cf_app_run, which is these same calls with a window, a clock and
 * an inspector in front of them.
 *
 * The session is scripted rather than driven by a device, so the program runs wherever the
 * library builds: no window, no font file, no display. What it prints is what the host saw of
 * its own frames, which is the part a person cannot read off a window that is not there. The
 * page and the loop live in host_loop.h, shared with the headless tier that asserts over the
 * same code.
 */
#include <stdio.h>

#include "host_loop.h"

/* The interval a turn reports. A real host measures the gap between frames and hands that here;
 * a script has no gap worth timing and states the number it wants the page to move by. */
#define SCRIPT_SECONDS (1.0f / 60.0f)

static void report(const host_app *host, const char *turned) {
    printf("%-22s builds=%u presents=%u commands=%u text=%u fill=%u cursor=%d said=\"%s\"\n",
           turned, host->builds, host->presents, host->commands, host->texts, host->fills,
           (int)host->cursor, host->said);
}

int main(void) {
    host_app host;

    if (!host_app_open(&host, (cf_size){HOST_WIDTH, HOST_HEIGHT})) {
        printf("host loop: the context could not start\n");

        return 1;
    }

    /* The first turn has nothing to compare against, so it lays the page out: a heading, two
     * controls and the line that says nothing has been asked of them yet. */
    host_app_frame(&host, SCRIPT_SECONDS);
    report(&host, "first frame");

    /* Two turns with nothing happening cost no build. That is the idle rule, and on this route
     * it belongs to the host: nobody else knows whether its events moved the tree. */
    const uint32_t built = host.builds;

    host_app_frame(&host, SCRIPT_SECONDS);
    host_app_frame(&host, SCRIPT_SECONDS);

    printf("idle turns             builds=%u (was %u), presents=%u\n", host.builds, built,
           host.presents);

    /* The keyboard first, so the pointer has touched nothing: Tab walks the ring to the control
     * that declared itself, and the unmodified Enter asks that box to do its job. A page reached
     * this way never asks the host for a pointer shape, which is what the cursor count shows. */
    host_app_send(&host, (cf_event){.type = CF_EVENT_KEY_DOWN, .key = CF_KEY_TAB});
    host_app_frame(&host, SCRIPT_SECONDS);
    host_app_send(&host, (cf_event){.type = CF_EVENT_KEY_DOWN, .key = CF_KEY_ENTER});
    host_app_frame(&host, SCRIPT_SECONDS);
    report(&host, "after the key");

    /* A click on the box the build keyed "count". The host finds it by that name and asks the
     * frame where it landed, because a script has no pointer of its own to aim. */
    host_app_click(&host, "count");
    host_app_frame(&host, SCRIPT_SECONDS);
    report(&host, "after the click");

    /* The output changed shape under the page. No event arrives for that, which is why the
     * surface is read every turn rather than remembered once. */
    host_app_resize(&host, (cf_size){HOST_WIDTH * 2.0f, HOST_HEIGHT});
    host_app_frame(&host, SCRIPT_SECONDS);
    report(&host, "after the resize");

    /* The box that takes the keyboard. A press books the focus and the shape that goes with it, a
     * run an input method is holding moves the candidate box the host is told about, and the
     * letters the device settled on land in the buffer this run owns - so the bytes are readable
     * here, after the frame that edited them, and not only where the field drew them. */
    host_app_click(&host, "type");
    host_app_frame(&host, SCRIPT_SECONDS);
    host_app_compose(&host, "ni");
    host_app_frame(&host, SCRIPT_SECONDS);
    host_app_compose(&host, "");
    host_app_type(&host, "nihao");
    host_app_frame(&host, SCRIPT_SECONDS);
    report(&host, "after the typing");

    /* The buffer is the field's to fill and the run's to own, and it carries no promise of a
     * terminator, so the length the edit reports is what bounds the print. */
    printf("asks                 cursor=%u text=%u candidate=%u presses=%u typed=\"%.*s\"\n",
           host.cursor_asks, host.text_asks, host.candidate_asks, host.presses,
           (int)host.typed.length, host.typed_bytes);

    /* The exit a control asks for. The host reads the ask after the frame that answered the
     * press, so the click the reader made is the one they see land - the order the shell uses. */
    host_app_click(&host, "done");
    host_app_frame(&host, SCRIPT_SECONDS);
    report(&host, "after the ask");

    const bool asked_to_end = host_app_should_quit(&host);

    printf("quit                 the page asked: %s\n", asked_to_end ? "yes" : "no");

    /* The teardown this route owns: the context the host opened and the output it built. */
    host_app_close(&host);
    printf("closed               destroys=%u\n", host.destroys);

    return host.presses == 2u && host.typed.length == 5u && asked_to_end ? 0 : 1;
}
