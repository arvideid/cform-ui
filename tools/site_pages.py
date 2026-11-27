#!/usr/bin/env python3
"""The parts of the site a canvas cannot be, generated at deploy time.

The documentation site is drawn: text a person reads lives in WebGL commands, which a
crawler cannot index, a browser cannot search with `Ctrl-F`, and a screen reader cannot
walk. That trade is made deliberately and argued for in docs/maintenance.md - the price
is paid here, in the three files a text browser can still open.

  releases/index.html   every published version, its date, its backend ABI number and
                        the features its commits name, read out of releases.json
  repo-docs/index.html  the repository's markdown, listed by each file's own heading,
                        so the prose has a page to arrive on rather than only a URL
                        somebody typed out of a source tree
  sitemap.xml           the URLs that exist as URLs. A route inside a canvas is not a
                        URL, so it is not here; the pages a hash reaches are listed once,
                        as the page that carries them

Nothing in it is committed. A generated file in the tree is a file that disagrees with
its generator, and the release notes already prove that pattern does not survive contact
with a second release.

    python3 tools/site_pages.py --bundle public --site-url https://example.dev/cform-ui/
"""

import argparse
import html
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

# The documents a person arrives to read, in the order a reader wants them: what it is,
# how it is built, how to use it, how to change it.
DOC_ORDER = [
    "principles.md",
    "getting-started.md",
    "backend-abi.md",
    "conformance.md",
    "bench.md",
    "versioning.md",
    "releases.md",
    "maintenance.md",
    "backlog.md",
]

HEADING = re.compile(r"^# +(.+)$", re.M)


def page(title, description, base, body):
    """One HTML document, styled by nothing and readable everywhere."""
    return f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{html.escape(title)}</title>
<meta name="description" content="{html.escape(description)}">
<base href="{html.escape(base)}">
<style>
body {{ max-width: 46rem; margin: 3rem auto; padding: 0 1.25rem;
        font: 16px/1.6 ui-sans-serif, system-ui, sans-serif; color: #1f2328; }}
h1 {{ font-size: 1.9rem; margin-bottom: .25rem; }}
nav a, a {{ color: #1f2328; }}
small {{ color: #57606a; }}
ul {{ padding-left: 1.1rem; }}
li {{ margin: .2rem 0; }}
</style>
</head>
<body>
{body}
</body>
</html>
"""


def releases_page(releases, base):
    """Every version, newest first, as text."""
    rows = []

    for entry in releases:
        tag = entry.get("tag") or f"v{entry['version']}"
        link = entry.get(
            "url") or f"https://github.com/arvideid/cform-ui/releases/tag/{tag}"
        marks = []

        if entry.get("prerelease"):
            marks.append("prerelease")
        if entry.get("abi"):
            marks.append(f"backend ABI {entry['abi']}")
        if entry.get("date"):
            marks.append(entry["date"])

        # Each mark is escaped on its own and the separator is markup rather than text,
        # so the join does not arrive in the page as &amp;middot;.
        stamped = " &middot; ".join(html.escape(one) for one in marks)

        rows.append(
            f"<h2><a href=\"{html.escape(link)}\">{html.escape(tag)}</a></h2>")
        rows.append(f"<p><small>{stamped}</small></p>")

        highlights = entry.get("highlights") or []
        if highlights:
            rows.append("<ul>")
            rows += [f"<li>{html.escape(one)}</li>" for one in highlights]
            rows.append("</ul>")

    body = "\n".join(rows) or "<p>No version has been published yet.</p>"

    return page(
        "cform-ui - releases",
        "Every published version of the cform-ui C UI framework, with its date, its "
        "backend ABI number and what it brought.",
        base,
        "<p><a href=\"./\"><strong>cform-ui</strong></a> <small>documentation</small></p>\n"
        "<h1>Releases</h1>\n<p><small>The numbers mean what "
        "<a href=\"repo-docs/versioning.md\">docs/versioning.md</a> says they mean. "
        "This page is text; the site you land on is drawn.</small></p>\n" + body,
    )


def heading_of(path):
    """The document's own H1, so the list restates nothing it links to."""
    text = path.read_text(encoding="utf-8", errors="replace")
    match = HEADING.search(text)

    return match.group(1).strip() if match else path.stem


def docs_page(documents, base):
    """The markdown mirrors, listed by the heading each one carries."""
    rows = []

    for document in documents:
        name = document.name
        rows.append(
            f"<li><a href=\"repo-docs/{html.escape(name)}\">{html.escape(name)}</a> "
            f"&mdash; {html.escape(heading_of(document))}</li>"
        )

    return page(
        "cform-ui - documentation in text",
        "The cform-ui documentation as markdown: the design rules, the backend contract, "
        "the conformance record and the release process.",
        base,
        "<p><a href=\"./\"><strong>cform-ui</strong></a> <small>documentation</small></p>\n"
        "<h1>The documents, as text</h1>\n"
        "<p><small>The site is drawn in cform itself, which is the point and also the "
        "limit: a canvas is not selectable, searchable or crawled. Everything it links "
        "to lives here instead, as the files the repository carries.</small></p>\n"
        "<ul>\n" + "\n".join(rows) + "\n</ul>",
    )


def sitemap(documents, base):
    """The URLs that exist as URLs.

    A route inside the drawn page is a hash, and a hash is not a page a crawler can be
    sent to, so the site's own navigation is not listed - the one document that carries
    it is.
    """
    urls = ["", "releases/", "repo-docs/", "playground/gallery.html"]
    urls += [f"repo-docs/{document.name}" for document in documents]

    entries = "\n".join(
        f"  <url>\n    <loc>{html.escape(base.rstrip('/') + '/' + one)}</loc>\n  </url>"
        for one in urls
    )

    return ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<urlset xmlns=\"http://www.sitemaps.org/schemas/sitemap/0.9\">\n"
            f"{entries}\n"
            "</urlset>\n")


def ordered(bundle):
    """The mirrors that arrived, in the order a reader wants them, extras after."""
    present = sorted(bundle.glob("*.md"))
    ranked = [one for name in DOC_ORDER for one in present if one.name == name]

    return ranked + [one for one in present if one.name not in DOC_ORDER]


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--bundle", default="public",
                        help="the Pages bundle the workflow assembled")
    parser.add_argument("--releases", default=None,
                        help="releases.json; <bundle>/releases.json by default")
    parser.add_argument("--site-url", default="https://arvideid.github.io/cform-ui/",
                        help="the address the bundle will be served from")
    arguments = parser.parse_args()

    bundle = pathlib.Path(arguments.bundle)
    mirror = bundle / "repo-docs"

    if not bundle.is_dir():
        print(f"site pages: no bundle at {bundle}", file=sys.stderr)
        return 1

    base = arguments.site_url if arguments.site_url.endswith(
        "/") else arguments.site_url + "/"
    index = pathlib.Path(
        arguments.releases) if arguments.releases else bundle / "releases.json"

    # An absent or unreadable index is a page that says so rather than a deploy that
    # stopped: the release notes are already published at this point, and a missing
    # text mirror is a worse outcome than an empty one.
    try:
        releases = json.loads(index.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        print(
            f"site pages: {index}: {error}; publishing an empty release list", file=sys.stderr)
        releases = []

    documents = ordered(mirror) if mirror.is_dir() else []

    if documents:
        (mirror / "index.html").write_text(docs_page(documents, base), encoding="utf-8",
                                           newline="\n")
    else:
        print("site pages: no markdown under repo-docs/, so no text index",
              file=sys.stderr)

    (bundle / "releases").mkdir(parents=True, exist_ok=True)
    (bundle / "releases" / "index.html").write_text(releases_page(releases, base),
                                                    encoding="utf-8", newline="\n")
    (bundle / "sitemap.xml").write_text(sitemap(documents, base), encoding="utf-8",
                                        newline="\n")

    print(f"site pages: {len(releases)} release rows, {len(documents)} document mirrors, "
          f"{len(sitemap(documents, base).splitlines()) - 3} sitemap URLs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
