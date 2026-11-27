#include "cf/app.h"

#include "internal.h"

#include <time.h>

/*
 * The arena size the shell's context starts with. It holds the view, the rects
 * and the paint list of the one frame in flight; the state a page keeps across
 * frames is not the arena's business, so nothing accrues here between one frame
 * and the next. A page that outgrows it degrades in its own frame - declarations
 * the arena cannot serve paint nothing - rather than failing the run, which is
 * the no-op the shell's one failure mode allows.
 */
#define APP_FRAME_BYTES (1024u * 1024u)

/* The toggle lives in the same keyed memory a build's state does, so the
 * shell keeps no globals of its own. */
#define INSPECTOR_KEY "cf/inspector"

/*
 * The longest interval a frame may claim. A page that was left undrawn while
 * the window sat behind another one has nothing to catch up on: handed the whole
 * gap, every tween it holds would jump the distance rather than move.
 */
#define FRAME_SECONDS_CAP 0.1f

/*
 * The panel's frame time, in microseconds: what the engine spent producing
 * this frame. The draw calls and the wait for the device are not in it, so
 * the number moves when a page's build, layout or paint gets dearer and not
 * when the monitor changes.
 */
static uint32_t elapsed_us(const struct timespec *started, const struct timespec *finished) {
    const int64_t seconds = (int64_t)finished->tv_sec - (int64_t)started->tv_sec;
    const int64_t nanos = (int64_t)finished->tv_nsec - (int64_t)started->tv_nsec;
    const int64_t microseconds = seconds * 1000000 + nanos / 1000;

    return microseconds > 0 ? (uint32_t)microseconds : 0u;
}

/* The same reading in the unit a build moves by: seconds, and capped for the
 * reason FRAME_SECONDS_CAP states. */
static float frame_interval(const struct timespec *last, const struct timespec *now) {
    const float seconds = (float)elapsed_us(last, now) / 1000000.0f;

    return seconds > FRAME_SECONDS_CAP ? FRAME_SECONDS_CAP : seconds;
}

/*
 * Whether the shell's last answer is this frame's. Two floats are compared rather than
 * the structs handed around, because a rect the page restated with the same four numbers
 * is the same place to stand a window and costs no call.
 */
static bool rect_is(cf_rect a, cf_rect b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

void cf_app_run(const cf_app_config *config) {
    if (config == NULL || config->build == NULL || config->backend.submit == NULL) {
        return;
    }

    /* The shell presents every frame it paints, so a backend that can submit
     * but not present is an incomplete shell rather than a working page. A
     * host that only renders drives cf_frame_run itself and never comes here,
     * so the loop is entered only when both halves of showing a frame exist. */
    if (config->backend.present == NULL) {
        cf_logf(CF_LOG_BACKEND, "app: the backend has no present, so the shell shows nothing");
        return;
    }

    /* A backend that reads a different command layout draws wrong rather than
     * failing, so the mismatch is settled before anything is submitted. The size
     * is the second half of that question: a struct built against another revision
     * of the header carries the slots at other offsets whatever its number claims. */
    if (config->backend.abi_version != CF_BACKEND_ABI_VERSION) {
        cf_logf(CF_LOG_BACKEND, "app: the backend reads stream %u, this build emits %u",
                config->backend.abi_version, CF_BACKEND_ABI_VERSION);
        return;
    }

    if (config->backend.size != sizeof(cf_backend)) {
        cf_logf(CF_LOG_BACKEND, "app: the backend's struct is %u bytes, this build reads %u",
                config->backend.size, (uint32_t)sizeof(cf_backend));
        return;
    }

    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, APP_FRAME_BYTES)) {
        return;
    }

    ctx.theme = config->theme;
    /* The clipboard is the host's, not the backend's: a shell that draws through
     * one window can still be asked to move bytes somewhere else entirely, and a
     * host that names nowhere hands NULL. */
    ctx.clipboard = config->clipboard;
    const cf_name inspector_key = cf_intern_text(&ctx.names, INSPECTOR_KEY);
    bool painted = false;
    /* The shape the backend was last told to show. A window opens as an arrow, so
     * that is what the shell believes it has until a frame says otherwise. */
    cf_cursor applied_cursor = CF_CURSOR_DEFAULT;
    /* Whether the device has been told about written text yet, and what it was
     * told. There is no guess at the window's starting state here: a shell that
     * assumed it could leave a page that opens focused on a field with no way to
     * type, on the one platform where the guess costs a keystroke. */
    bool told_text = false;
    bool asked_text = false;
    /* The box the backend was last told to hold its candidates against, and whether it
     * has been told at all: a window opens with no input method standing anywhere, so
     * there is nothing to assume about the first answer. */
    bool told_candidates = false;
    cf_rect asked_candidates = {0.0f, 0.0f, 0.0f, 0.0f};
    cf_size offer = {0.0f, 0.0f};
    struct timespec last;
    timespec_get(&last, TIME_UTC);

    for (;;) {
        bool woke = false;

        cf_event event;

        while (config->backend.poll_event != NULL &&
               config->backend.poll_event(config->backend.user_data, &event)) {
            if (event.type == CF_EVENT_QUIT) {
                cf_ctx_free(&ctx);
                return;
            }

            /* The in-app inspector is a desktop affordance. On a browser tab
             * F12 belongs to the dev tools, which already lay the frame's
             * numbers over the page better than this one can. */
#ifndef __EMSCRIPTEN__
            if (event.type == CF_EVENT_KEY_DOWN && event.key == CF_KEY_F12) {
                bool *shown = cf_state(&ctx, inspector_key, sizeof *shown);

                if (shown != NULL) {
                    *shown = !*shown;
                }
            } else
#endif
            {
                cf_input(&ctx, event);
            }

            woke = true;
        }

        const cf_size now = config->backend.surface != NULL
                                ? config->backend.surface(config->backend.user_data)
                                : (cf_size){0.0f, 0.0f};

        /* An undamaged frame redraws nothing: no event moved the tree, the
         * surface did not move under it, and the frame that ran did not ask for
         * another. The ask is the door a build steps through when its own state
         * is what is moving - a tween it runs, content that landed - and it is
         * re-asked every frame the motion lasts, so a page at rest still costs
         * one present and no build. */
        if (painted && !woke && !cf_frame_requested(&ctx) && now.width == offer.width &&
            now.height == offer.height) {
            config->backend.present(config->backend.user_data);
            continue;
        }

        offer = now;
        cf_command_list list = {0};
        struct timespec started;
        struct timespec finished;
        timespec_get(&started, TIME_UTC);
        /* What passed since the frame before this one is the page's to spend:
         * a build that steps a tween moves by the wall time the shell waited
         * rather than by a guess at the display's rate. */
        const float seconds = frame_interval(&last, &started);
        last = started;
        cf_frame_run(&ctx, config->build, config->build_user_data, offer, seconds, &list);
        timespec_get(&finished, TIME_UTC);
        const uint32_t frame_us = elapsed_us(&started, &finished);
        painted = true;

        const bool *shown = cf_state(&ctx, inspector_key, sizeof *shown);

        if (shown != NULL && *shown) {
            app_inspector_append(&ctx, config->fonts, list.count, frame_us, &list);
        }

        /* What the hand is over is the frame's answer, and the shell is the one
         * place that reads it every frame: a host rendering through cf_frame_run
         * itself asks in its own loop. Only a moved answer is worth a call, so a
         * page the pointer is resting on costs the backend nothing. */
        if (config->backend.set_cursor != NULL) {
            const cf_cursor asked = cf_frame_cursor(&ctx);

            if (asked != applied_cursor) {
                applied_cursor = asked;
                config->backend.set_cursor(config->backend.user_data, asked);
            }
        }

        /* Whether the page has a box that takes written text is the frame's other
         * answer, read and sent the same way: a device that shows a keyboard or an
         * input method should show it for the field this frame placed the keyboard
         * on, and hide it when the keyboard has moved somewhere text cannot go. */
        if (config->backend.set_text_input != NULL) {
            const bool wants_text = cf_frame_takes_text(&ctx);

            if (!told_text || wants_text != asked_text) {
                told_text = true;
                asked_text = wants_text;
                config->backend.set_text_input(config->backend.user_data, wants_text);
            }
        }

        /* And the same for where the device should stand its answers while a run is
         * being decided: an input method with nowhere to put its list is the reason a
         * page draws the preedit itself, so an output that cannot be told is no worse off
         * than one that never asked. A rect of no extent travels the same door to say the
         * page has finished deciding. */
        if (config->backend.set_candidate_rect != NULL) {
            const cf_rect box = cf_frame_composition_rect(&ctx);

            if (!told_candidates || !rect_is(box, asked_candidates)) {
                told_candidates = true;
                asked_candidates = box;
                config->backend.set_candidate_rect(config->backend.user_data, box);
            }
        }

        config->backend.submit(config->backend.user_data, list.commands, list.count);
        config->backend.present(config->backend.user_data);

        /* The other way a run ends: the window's close button reports an event, and
         * a control inside the page has no event to send, so it asks here. After the
         * present, so the press that asked is drawn before the loop is gone. */
        if (cf_quit_requested(&ctx)) {
            cf_ctx_free(&ctx);
            return;
        }
    }
}
