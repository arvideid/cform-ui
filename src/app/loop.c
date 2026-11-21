#include "cf/app.h"

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

void cf_app_run(const cf_app_config *config) {
    if (config == NULL || config->build == NULL || config->backend.submit == NULL) {
        return;
    }

    cf_ctx ctx;

    if (!cf_ctx_init(&ctx, APP_FRAME_BYTES, APP_PERSISTENT_BYTES)) {
        return;
    }

    ctx.theme = config->theme;

    for (;;) {
        cf_event event;

        while (config->backend.poll_event != NULL &&
               config->backend.poll_event(config->backend.user_data, &event)) {
            if (event.type == CF_EVENT_QUIT) {
                cf_ctx_free(&ctx);
                return;
            }

            cf_input(&ctx, event);
        }

        const cf_size offer = config->backend.surface != NULL
                                  ? config->backend.surface(config->backend.user_data)
                                  : (cf_size){0.0f, 0.0f};
        cf_command_list list = {0};
        cf_frame_run(&ctx, config->build, offer, &list);
        config->backend.submit(config->backend.user_data, list.commands, list.count);
        config->backend.present(config->backend.user_data);
    }
}
