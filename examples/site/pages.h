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

/*
 * The groups, named here so the navigation table can file a page and the page can put
 * that filing above its own heading - one spelling, and the list, the foot and the bar
 * all read the filing rather than restating it.
 *
 * A group's word is printed once per screen, over the heading of the page filed under
 * it. The surfaces that offer rows are labelled by what they are rather than by the
 * group, because a label read beside the prose and a label read over it are the same
 * glance, and the same word twice in one glance reads as a drawing that repeated itself.
 *
 * The showcase's group is the gallery rather than the showcase, because the page's own
 * title is already the word the bar reads - a group named the same as the heading under
 * it would say the page's name twice and describe nothing.
 */
#define SITE_GROUP_DOCS "Documentation"
#define SITE_GROUP_PROJECT "The project"
#define SITE_GROUP_SHOWCASE "Gallery"

/* The words over the two lists: the pages beside the document, and the pages under it. */
#define SITE_SIDEBAR_LABEL "Contents"
#define SITE_FOOT_LABEL "More pages"

cf_node site_page_getting_started(cf_ctx *ctx, const site_view *view);
cf_node site_page_concepts(cf_ctx *ctx, const site_view *view);
cf_node site_page_api(cf_ctx *ctx, const site_view *view);
cf_node site_page_versioning(cf_ctx *ctx, const site_view *view);
cf_node site_page_releases(cf_ctx *ctx, const site_view *view);
cf_node site_page_principles(cf_ctx *ctx, const site_view *view);
cf_node site_page_showcase(cf_ctx *ctx, const site_view *view);

#endif
