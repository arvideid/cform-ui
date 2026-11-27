#ifndef CF_EXAMPLES_SITE_PAGES_H
#define CF_EXAMPLES_SITE_PAGES_H

/*
 * What a page is: a function that takes a context and the table of values, and
 * answers the declaration of one document.
 *
 * This is the whole of what a page knows about the site. It is not in the
 * navigation table, it does not know the other pages, it cannot see the route the
 * reader arrived on and it has no handle on the window, the platform or the
 * address bar - the driver hands it a `site_view` and a `cf_ctx` and the page can
 * only say what a document says. That is what makes a page movable: the same file
 * compiles into the site, into a native build and into the headless tier, and in
 * the last one it is driven by a test rather than by a browser.
 *
 * The names are declared here rather than in nav.h so a page does not pull in the
 * table of every page beside it: `nav.h`'s rows are `static`, and a translation
 * unit that carries a table it never reads is a warning and a second copy of the
 * data in a program that meant to have one.
 */

#include "blocks.h"

/*
 * The addresses the site points at, stated once.
 *
 * A page that links to a markdown file and the bar that links to the repository are
 * naming the same place, and two spellings of an address are two addresses: the day
 * one of them moves, the other one is a link that goes somewhere the reader did not
 * mean. The gallery address is relative because the deployment puts the two pages
 * beside each other, and a host that serves them some other way changes one line.
 */
#define SITE_REPO "https://github.com/arvideid/cform-ui"
#define SITE_BLOB SITE_REPO "/blob/main/"
#define SITE_GALLERY "playground/gallery.html"

/* The page the version link lands on: static HTML generated at deploy time from
 * the tags, because a canvas cannot fetch and a link can. */
#define SITE_RELEASES "releases/"

/* The groups, named here so a page can put its own name under the heading it lives
 * beneath and the navigation table can file it there - one spelling, two uses. */
#define SITE_GROUP_DOCS "Documentation"
#define SITE_GROUP_PROJECT "The project"
#define SITE_GROUP_SHOWCASE "Showcase"

cf_node site_page_getting_started(cf_ctx *ctx, const site_view *view);
cf_node site_page_concepts(cf_ctx *ctx, const site_view *view);
cf_node site_page_api(cf_ctx *ctx, const site_view *view);
cf_node site_page_versioning(cf_ctx *ctx, const site_view *view);
cf_node site_page_releases(cf_ctx *ctx, const site_view *view);
cf_node site_page_principles(cf_ctx *ctx, const site_view *view);
cf_node site_page_showcase(cf_ctx *ctx, const site_view *view);

#endif
