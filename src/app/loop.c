#include "cf/app.h"

#include "internal.h"

/*
 * The arena sizes the shell's context starts with. The frame side holds the
 * view, the rects and the paint list of the one frame in flight; the
 * persistent side holds the state blocks, scroll offsets and interned names
 * a page accrues. A page that outgrows either degrades in its own frame -
 * declarations the frame arena cannot serve paint nothing - rather than
 * failing the run, which is the no-op the shell's one failure mode allows.
 */
#define APP_FRAME_BYTES (1024u * 1024u)
#define APP_PERSISTENT_BYTES (256u * 1024u)

/* The toggle lives in the same keyed memory a build's state does, so the
 * shell keeps no globals of its own. */
#define INSPECTOR_KEY "cf/inspector"

void cf_app_run(const cf_app_config *config) {
    if (config == NULL || config->build == NULL || config->backend.submit == NULL) {
        return;
    }

    /* A backend that reads a different command layout draws wrong rather than
     * failing, so the mismatch is settled before anything is submitted. */
    if (config->backend.abi_version != CF_BACKEND_ABI_VERSION) {
        cf_logf(CF_LOG_BACKEND, "app: the backend reads stream %u, this build emits %u",
                config->backend.abi_version, CF_BACKEND_ABI_VERSION);
        return;
    }

    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, APP_FRAME_BYTES, APP_PERSISTENT_BYTES)) {
        return;
    }

    ctx.theme = config->theme;
    const cf_name inspector_key = cf_intern_text(&ctx.names, INSPECTOR_KEY);
    bool painted = false;
    cf_size offer = {0.0f, 0.0f};

    for (;;) {
        bool woke = false;

        cf_event event;

        while (config->backend.poll_event != NULL &&
               config->backend.poll_event(config->backend.user_data, &event)) {
            if (event.type == CF_EVENT_QUIT) {
                cf_ctx_free(&ctx);
                return;
            }

            if (event.type == CF_EVENT_KEY_DOWN && event.key == CF_KEY_F12) {
                bool *shown = cf_state(&ctx, inspector_key, sizeof *shown);

                if (shown != NULL) {
                    *shown = !*shown;
                }
            } else {
                cf_input(&ctx, event);
            }

            woke = true;
        }

        const cf_size now = config->backend.surface != NULL
                                ? config->backend.surface(config->backend.user_data)
                                : (cf_size){0.0f, 0.0f};

        /* An undamaged frame redraws nothing: no event moved the tree and the
         * surface did not move under it. A build whose per-frame state moves
         * without events - a tween it steps, content that landed - is the one
         * thing this rule cannot see; the shell asks for frames again when a
         * build needs that door. */
        if (painted && !woke && now.width == offer.width && now.height == offer.height) {
            config->backend.present(config->backend.user_data);
            continue;
        }

        offer = now;
        cf_command_list list = {0};
        cf_frame_run(&ctx, config->build, offer, &list);
        painted = true;

        const bool *shown = cf_state(&ctx, inspector_key, sizeof *shown);

        if (shown != NULL && *shown) {
            app_inspector_append(&ctx, config->fonts, list.count, &list);
        }

        config->backend.submit(config->backend.user_data, list.commands, list.count);
        config->backend.present(config->backend.user_data);
    }
}
