#ifndef CF_EXAMPLES_SITE_NAV_H
#define CF_EXAMPLES_SITE_NAV_H

/*
 * The site's whole structure, as data.
 *
 * One row per page: the group the page files itself under, the words the row
 * reads, the route that reaches it and the function that declares it. The list
 * down the left of every page, the foot under it, the address bar and the headless
 * tier all read this one table, so a page the site holds cannot be missing from the
 * tests and a page that is not in the table cannot be reached at all.
 *
 * Included by the driver and by the headless tier only - never by a page, which
 * has no business knowing the pages beside it - so the table is compiled once per
 * program and there is nothing here for a second translation unit to disagree
 * with.
 */

#include <string.h>

#include "pages.h"

/* The groups a row can be filed under live in pages.h, where the pages that carry
 * them as an eyebrow read them too. */

/* The page's whole route, and the address-bar text it arrives as. Kept in one
 * place because the shell writes what a press is matched against, and a route
 * spelled twice is two routes. */
#define SITE_PREFIX "/docs/"

/* How long the part of an address naming a section can be and still be copied out whole. The
 * longest slug the pages spell is a dozen-odd characters, so a fragment longer than this names
 * nothing the site holds. */
#define SITE_SLUG_BYTES 64u

/* One page: what the row says, where it lives, who declares it. */
typedef struct site_entry {
    const char *group;
    const char *label;
    const char *route;
    const char *description;
    cf_node (*page)(cf_ctx *ctx, const site_view *view);
} site_entry;

/*
 * The table. Every row is a page rather than a pointer to one, because the list,
 * the keyboard, the pager and the headless tier all want the same thing: the page
 * an address reaches, in the order the document walks it.
 *
 * The group is what decides *where* a row is offered rather than whether it exists:
 * the rows filed as documentation are the list beside the prose, the rows about the
 * project are the foot under it, and the showcase is a word in the masthead. One
 * table and three places to click, which is what lets a short list be a short list
 * without a page quietly becoming unreachable.
 */
static const site_entry site_nav[] = {
    {SITE_GROUP_DOCS, "Getting started", SITE_PREFIX "getting-started",
     "The three ways to take cform into a build, and the whole program each one runs.",
     site_page_getting_started},
    {SITE_GROUP_DOCS, "Concepts", SITE_PREFIX "concepts",
     "The words the source uses, and what happens between two frames.", site_page_concepts},
    {SITE_GROUP_DOCS, "API reference", SITE_PREFIX "api",
     "Every public symbol, by layer, with the contract one line long.", site_page_api},
    {SITE_GROUP_PROJECT, "Versioning", SITE_PREFIX "versioning",
     "What a version number promises, and what a CI gate enforces about it.", site_page_versioning},
    {SITE_GROUP_PROJECT, "Releases", SITE_PREFIX "releases",
     "How a tag becomes an archive, a set of notes and this site.", site_page_releases},
    {SITE_GROUP_PROJECT, "Principles", SITE_PREFIX "principles",
     "The rules the code is written to, and what each one buys.", site_page_principles},
    {SITE_GROUP_SHOWCASE, "Showcase", "/showcase",
     "Every gallery chapter, drawn with the framework it demonstrates.", site_page_showcase},
};

#define SITE_NAV_COUNT (uint32_t)(sizeof site_nav / sizeof site_nav[0])

/*
 * The rows filed under one group, in the order the table holds them, answered with
 * how many were taken. A group is a name the page carries as its own eyebrow, so the
 * site's two lists cannot disagree with the page about where it belongs: a row is
 * offered in exactly one of them, and a row filed under a group nobody asks for is a
 * row that renders nowhere - which the reachability case in the site's own tier is what
 * catches.
 */
static inline uint32_t site_group_rows(const char *group, const site_entry *rows[], uint32_t cap) {
    uint32_t taken = 0u;

    for (uint32_t i = 0u; i < SITE_NAV_COUNT && taken < cap; ++i) {
        if (strcmp(site_nav[i].group, group) == 0) {
            rows[taken++] = &site_nav[i];
        }
    }

    return taken;
}

/* One row by position, which is what the keyboard walk, the pager and the test
 * sweep all need and what no route has to know. */
static inline const site_entry *site_entry_at(uint32_t index) {
    return index < SITE_NAV_COUNT ? &site_nav[index] : NULL;
}

/*
 * Where a row sits in the table - the position the pager walks to reach the page
 * before this one and the page after it. Every lookup here answers a pointer into
 * the table, so identity is the comparison, and SITE_NAV_COUNT is the answer for a
 * row the table does not hold: one past the end, which no walk can land on.
 */
static inline uint32_t site_index_of(const site_entry *entry) {
    for (uint32_t i = 0u; i < SITE_NAV_COUNT; ++i) {
        if (&site_nav[i] == entry) {
            return i;
        }
    }

    return SITE_NAV_COUNT;
}

/*
 * The row a route names, or NULL for an address this site does not hold. Which page the reader
 * is on is decided up to the `#` that starts a section: a fragment says where in the page to
 * land, not which page the address is for, and a reader who pastes a link with one still gets
 * the page it was cut from.
 */
static inline const site_entry *site_entry_for_route(const char *route) {
    if (route == NULL) {
        return NULL;
    }

    for (uint32_t i = 0u; i < SITE_NAV_COUNT; ++i) {
        const size_t length = strlen(site_nav[i].route);

        if (strncmp(site_nav[i].route, route, length) == 0 &&
            (route[length] == '\0' || route[length] == '#')) {
            return &site_nav[i];
        }
    }

    return NULL;
}

/*
 * The section an address names, copied out in the words it spells it with - a slug is interned
 * by whoever reaches for it, and the address itself belongs to the shell. Answers false for an
 * address with no fragment, an empty one, and one too long to be a slug this site could hold.
 */
static inline bool site_section_of(const char *address, char *out, size_t cap) {
    const char *mark = address != NULL ? strchr(address, '#') : NULL;

    if (mark == NULL || mark[1] == '\0') {
        return false;
    }

    ++mark;

    const size_t length = strlen(mark);

    if (length >= cap) {
        return false;
    }

    memcpy(out, mark, length + 1u);

    return true;
}

/*
 * The row to show for an address bar that named nothing: the first page of the
 * table. This is the whole of what a build with no browser does - a native window
 * reads no hash, gets a route of nothing, and lands on the page a reader who ran
 * the program by hand would want first. Keeping the answer in the table rather
 * than in the shell is what lets the headless tier prove it without a platform.
 */
static inline const site_entry *site_entry_or_first(const char *route) {
    const site_entry *entry = site_entry_for_route(route);

    return entry != NULL ? entry : &site_nav[0];
}

#endif
