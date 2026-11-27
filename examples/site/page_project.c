/*
 * The three pages about the project rather than the library.
 *
 * Each is one paragraph and one way to the file that carries the policy. That is the
 * whole design of this group and it is deliberate: a versioning rule, a release
 * process and a set of principles written twice - once in markdown for the person
 * reading the repository, once in C for the person reading the site - are two rules,
 * and the second one stops being true the first time the first is edited without the
 * other. The site's job is to point at the file CI checks, not to compete with it.
 *
 * What each page does state is the sentence a reader needs to know *why* the file is
 * worth opening, because that is the part a table of contents does not carry.
 */
#include "pages.h"

/* One page about a document, in the shape all three use: what it decides, why that
 * is worth a minute, and the file itself. */
static cf_node site_document_page(cf_ctx *ctx, const site_view *view, const char *title,
                                  const char *slug, const char *lede, const char *body,
                                  const char *link_label, const char *path) {
    char address[SITE_URL_BYTES];
    snprintf(address, sizeof address, "%s%s", SITE_BLOB, path);

    const cf_node blocks[] = {
        site_p(ctx, view, body),
        site_note(ctx, view,
                  "The file is the rule and CI reads it. A page that repeated it would be a "
                  "second place to change, and a policy that lives in two places is a policy "
                  "that quietly lives in neither."),
        site_link(ctx, view, slug, link_label, address),
    };

    return site_page(ctx, view, SITE_GROUP_PROJECT, title, slug, lede, blocks, SITE_COUNT(blocks));
}

cf_node site_page_versioning(cf_ctx *ctx, const site_view *view) {
    return site_document_page(
        ctx, view, "Versioning", "versioning",
        "What a number promises, and what a gate enforces about the promise.",
        "Source and rebuild compatibility within a major, not binary compatibility across "
        "compilers: this is a C library that ships static archives, and saying otherwise would "
        "be a promise no toolchain matrix could keep. A minor only appends - new functions, new "
        "optional backend slots at the tail of the struct, new enum values at the end of their "
        "list - and a patch changes nothing an adopter would notice in a screenshot. The API "
        "surface is generated from the headers on every push and diffed against the last "
        "release, so a break that is not named by the version fails the build instead of being "
        "discovered by a consumer. Before one point zero the minor is where a break is allowed "
        "to live, and every one of them is listed in that release's notes with the call-site "
        "edit that migrates it.",
        "docs/versioning.md", "docs/versioning.md");
}

cf_node site_page_releases(cf_ctx *ctx, const site_view *view) {
    return site_document_page(
        ctx, view, "Releases", "releases",
        "How a tag becomes three archives, a set of notes and this site.",
        "A release is a push of a tag and nothing else. The workflow that watches it re-checks "
        "every gate, builds the package on three operating systems, installs it into a "
        "directory and makes an outside program find it through both CMake and pkg-config, "
        "packs the WebAssembly gallery, writes the notes from the commit messages between this "
        "tag and the last one, and publishes the site. There is no changelog file in the tree: "
        "the tags and the commits are the record, and a rendering of them committed to the "
        "repository is build output that goes stale.",
        "the release process", "docs/releases.md");
}

cf_node site_page_principles(cf_ctx *ctx, const site_view *view) {
    return site_document_page(
        ctx, view, "Principles", "principles",
        "The rules the code is written to, and what each one buys.",
        "Why the layer graph is a link graph and not a suggestion; why a page describes a "
        "frame instead of managing widgets; why there is no markup language, no declarative "
        "tree and no runtime stylesheet in this library; why a frame that saw nothing move "
        "draws nothing; why the numbers above the backend are points and the ones below are "
        "not; and why allocation is confined to one layer so that nothing else has to answer "
        "who owns a byte. Each rule is written with the failure it prevents, because a "
        "principle nobody can violate is a preference.",
        "docs/principles.md", "docs/principles.md");
}
