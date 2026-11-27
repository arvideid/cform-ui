#ifndef CF_APP_H
#define CF_APP_H

#include "cf/backend.h"
#include "cf/widget.h"

/*
 * The shell a whole program fits in: the build that declares each frame's
 * page, the backend that reads its events and draws its stream, and the
 * theme the resolver styles the view with. The backend is a value, built
 * before the run starts; the theme, the fonts and the clipboard are borrowed and
 * must outlive the run. The fonts are the inspector's printing press: without
 * them the F12 panel draws bare.
 */
typedef struct cf_app_config {
    cf_build_fn build;
    /* Handed to the build every frame, unread and unchanged, beside the context.
     * This is where a driver keeps what the run owns but the frame does not - the
     * faces it opened, the asset tables, the state a page reads that is not the
     * keyed memory cf_state serves - so an adoption example declares from what it
     * was handed instead of from a file-scope variable. NULL is what a build that
     * needs nothing outside its context gets, and the backend keeps its own
     * pointer: the two are different hosts' data and never the same one. */
    void *build_user_data;
    cf_backend backend;
    const cf_theme *theme;
    const cf_text_stack *fonts;
    /* What the page's copy, cut and paste reach. NULL leaves the shortcuts asking
     * for nothing and getting nothing, which is what a host that has not decided
     * where its bytes go should get rather than a silent desktop write. */
    const cf_clipboard *clipboard;
    /* Bytes the frame arena starts with: the storage one frame's page is cut from - the
     * view's arrays, the interned names, the commands - and emptied again at every frame
     * break. 0 takes the shell's own capacity, which is what a host that has never
     * measured its page wants. A page that needs more than the arena holds is not painted
     * whole: the frame hands over the commands that fitted, which may be none, so the host
     * of a big page raises this number rather than watching a page arrive with pieces
     * missing. */
    size_t frame_bytes;
} cf_app_config;

/*
 * Runs the loop until the run is over - the backend reported quit, or the page
 * asked for the end through cf_request_quit: every frame drains the
 * backend's events into the dispatch, runs the build's page through the
 * frame pipeline against the surface the backend offers now, and submits
 * the commands it painted. A config without a build or a backend that cannot
 * submit runs no frames and says nothing; a backend that cannot present, or whose
 * stated stream and struct size this build cannot read, runs no frames and logs
 * why. A context the shell's arenas cannot start is a silent no-op too - which
 * is not what a config of no frame capacity asks for, since 0 there means the
 * shell's own. The build is handed build_user_data on every frame, which is the
 * only way a driver's own data reaches a page from here.
 */
void cf_app_run(const cf_app_config *config);

#endif
