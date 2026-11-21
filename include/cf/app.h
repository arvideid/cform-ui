#ifndef CF_APP_H
#define CF_APP_H

#include "cf/backend.h"
#include "cf/widget.h"

/*
 * The shell a whole program fits in: the build that declares each frame's
 * page, the backend that reads its events and draws its stream, and the
 * theme the resolver styles the view with. The backend is a value, built
 * before the run starts; the theme is borrowed and must outlive it.
 */
typedef struct cf_app_config {
    cf_build_fn build;
    cf_backend backend;
    const cf_theme *theme;
} cf_app_config;

/*
 * Runs the loop until the backend reports quit: every frame drains the
 * backend's events into the dispatch, runs the build's page through the
 * frame pipeline against the surface the backend offers now, and submits
 * the commands it painted. A config without a build, a backend that cannot
 * submit, or a context the shell's arenas cannot start is a silent no-op.
 */
void cf_app_run(const cf_app_config *config);

#endif
