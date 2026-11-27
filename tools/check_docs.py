#!/usr/bin/env python3
"""Fail a document that disagrees with the code it describes.

A doc that drifts is worse than no doc: it is read as fact by the person it misleads.
The two places this repository has already been wrong are exactly the two this script
now refuses - a sentence naming how many functions a backend has to fill, and a header
whose name appears in no document at all - so the check is derived from the source every
run rather than remembered by a reviewer.

Four rules, each printed as a sentence a stranger can act on:

  the numbers    the count of `cf_backend`'s slots and CF_BACKEND_ABI_VERSION, as stated
                 anywhere in README.md and docs/*.md
  the links      every relative link a markdown file carries points at something
  the coverage   every public header is named by at least one document
  the pages      the site's prose agrees with the headers it lists, its route table
                 agrees with its navigation table, and no page states a colour,
                 a length or a radius that tokens.h has not been asked to own
"""

# the one header parser; a second opinion on what a header exports is drift
import api_surface
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))


ROOT = pathlib.Path(__file__).resolve().parent.parent
INCLUDE = ROOT / "include" / "cf"
DOCS = ROOT / "docs"
SITE = ROOT / "examples" / "site"
SHELL = ROOT / "examples" / "web" / "site_shell.html"
API_PAGE = SITE / "page_api.c"
NAV = SITE / "nav.h"

PROSE = [ROOT / "README.md"] + sorted(DOCS.glob("*.md"))

errors = []


def fail(where, message):
    errors.append(f"{where}: {message}")


# ---------------------------------------------------------------- the numbers

SLOT = re.compile(
    r"^\s*[A-Za-z_][A-Za-z0-9_ \*]*\(\s*\*\s*([a-z_]+)\s*\)\s*\(", re.M)
ABI = re.compile(r"#define\s+CF_BACKEND_ABI_VERSION\s+(\d+)u?")

# Every number in a header between these two markers counts as a claim about the
# struct's slots; anything else in the file is documentation prose.
STRUCT = re.compile(r"typedef struct cf_backend \{(.*?)\n\} cf_backend;", re.S)

WORD_NUMBERS = {
    "two": 2, "three": 3, "four": 4, "five": 5, "six": 6, "seven": 7, "eight": 8,
    "nine": 9, "ten": 10, "eleven": 11, "twelve": 12,
}

NUMBER_WORD = {number: word for word, number in WORD_NUMBERS.items()}

# "ten functions", "8 slots", "ABI 11" - the shapes a stale sentence takes.
FUNCTION_CLAIM = re.compile(
    r"\b(" + "|".join(WORD_NUMBERS) + r"|\d+)\s+(?:public\s+|backend\s+)?functions?\b")
ABI_CLAIM = re.compile(r"\bABI\s+(\d+)\b")


def backend_facts():
    """(slot names in declaration order, the ABI number) read from the header."""
    text = (INCLUDE / "backend.h").read_text(encoding="utf-8")

    body = STRUCT.search(text)
    if body is None:
        fail("include/cf/backend.h", "no cf_backend struct to count slots in")
        return [], 0

    slots = [name for name in SLOT.findall(body.group(1))]
    version = ABI.search(text)

    if version is None:
        fail("include/cf/backend.h",
             "no CF_BACKEND_ABI_VERSION to compare documents with")
        return slots, 0

    return slots, int(version.group(1))


def check_numbers(slots, abi):
    count = len(slots)
    honest = NUMBER_WORD.get(count, str(count))

    for path in PROSE:
        if not path.exists():
            continue

        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            where = f"{path.relative_to(ROOT)}:{number}"

            for claim in FUNCTION_CLAIM.finditer(line):
                stated = claim.group(1)
                stated = WORD_NUMBERS.get(stated.lower(), None) or int(stated)

                if stated != count:
                    fail(where, f"says a backend fills {claim.group(1)} functions; the header "
                         f"declares {honest} ({', '.join(slots)})")

            for claim in ABI_CLAIM.finditer(line):
                if int(claim.group(1)) != abi:
                    fail(where, f"says the backend contract is at ABI {claim.group(1)}; "
                         f"the header says {abi}")


# ---------------------------------------------------------------- the links

LINK = re.compile(r"\[[^\]]*\]\(([^)\s]+)")


def check_links():
    for path in PROSE + [SHELL]:
        if not path.exists():
            continue

        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            for target in LINK.findall(line):
                if target.startswith(("http://", "https://", "#", "mailto:")):
                    continue

                # A document links to a file in the tree or to a page of the site it
                # is published on. The site's own pages are generated at deploy time,
                # so only a path into the repository can be checked from here.
                if "://" in target or target.startswith("/"):
                    continue

                resolved = (path.parent / target.split("#")[0]).resolve()

                if not resolved.exists():
                    fail(f"{path.relative_to(ROOT)}:{number}",
                         f"links to {target}, which is not in the tree")


# ------------------------------------------------------------- header coverage

def check_headers(slots):
    """Every public header named by at least one document.

    cf.h is the umbrella and names nothing a document has to explain, so it is
    exempt by statement rather than by silence.
    """
    said = "\n".join(path.read_text(encoding="utf-8")
                     for path in PROSE if path.exists())
    said += str(slots)

    for header in sorted(INCLUDE.glob("*.h")):
        if header.name == "cf.h":
            continue

        if header.name not in said:
            fail("docs", f"no document names {header.name}; a header nobody describes "
                 "is a header a reader cannot find")


# ---------------------------------------------------------------- the site

# The literals a page has no business stating. Percentages and `auto` are structural -
# "fill the column" is not a tone, a radius or a rhythm - so they are not in this set.
PALETTE = re.compile(r"\bcf_rgb\s*\(|\bcf_color\s*\(|#[0-9a-fA-F]{3,8}\b")
MEASURE = re.compile(r"\bCF_(?:PT|PX|FR)\s*\(\s*[1-9]")

# A character or string literal in C: text a page displays rather than a value it lays
# out with, so it is removed before the literal search runs.
STRING_LITERAL = re.compile(r"\"(?:[^\"\\]|\\.)*\"|'(?:[^'\\]|\\.)*'")

# A call site may still name zero: it is how "no border on this edge" is written, and it
# carries no value that belongs to tokens.h.


def code_only(line):
    """A source line with its string literals blanked.

    A code panel's content is prose about another file: the install commands a page
    quotes are allowed to name a length or a tone because nothing there is a value the
    site's layout reads. Only the page's own code is held to tokens.h.
    """
    return STRING_LITERAL.sub("", line)


def check_pages():
    for path in sorted(SITE.glob("page_*.c")):
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            stripped = line.strip()

            if stripped.startswith(("*", "/*", "//")):
                continue

            code = code_only(line.split("*/")[0] if "*/" in line else line)
            where = f"{path.relative_to(ROOT)}:{number}"

            if PALETTE.search(code):
                fail(where, "states a colour in a page; tokens.h owns the tones "
                            f"and cf_theme owns the values: {code.strip()}")

            if MEASURE.search(code):
                fail(where, "states a length in a page; tokens.h owns the rhythm "
                            f"and style.h owns the units: {code.strip()}")


TABLE_ROW = re.compile(r'"((?:cf_[a-z0-9_]+)|(?:CF_[A-Z0-9_]+))"')


def surface_names():
    """(what a header exports as a callable, every name a header states at all).

    Both halves come from the parser the version gate uses, so the page cannot pass a
    drift check by disagreeing with the check that guards the version. A type's name is
    not a function, and a regex broad enough to catch `cf_node (*fn)(void)` catches it
    too - which would report half the API as missing from the page that is supposed to
    be the API's mirror.
    """
    callables = set()
    stated = set()

    for header in sorted(INCLUDE.glob("*.h")):
        for kind, name, _ in api_surface.surface_of(header):
            stated.add(name)
            if kind in ("func", "macro-fn"):
                callables.add(name)

    return callables, stated


def check_api_page():
    """The API page is a rendering of the headers, so it has to agree both ways."""
    if not API_PAGE.exists():
        fail("examples/site/page_api.c",
             "the page the drift check exists for is not here")
        return

    listed = set(TABLE_ROW.findall(API_PAGE.read_text(encoding="utf-8")))
    callables, stated = surface_names()

    missing = sorted(name for name in callables if name not in listed)
    stale = sorted(name for name in listed if name not in stated)

    for name in missing:
        fail("examples/site/page_api.c", f"{name} is exported by a header and the page "
             "does not name it")

    for name in stale:
        fail("examples/site/page_api.c", f"the page lists {name}, which no header states "
             "any more")


def check_routes():
    """One navigation table, spelled twice on purpose, checked to agree.

    A canvas paints headings and a host reads none, so the shell HTML carries the same
    routes with the words a tab strip and a link preview need. That duplication is only
    safe while something compares them.
    """
    if not SHELL.exists():
        fail("examples/web/site_shell.html",
             "the page a browser opens is not here")
        return

    table = NAV.read_text(encoding="utf-8")
    shell = SHELL.read_text(encoding="utf-8")

    # A row spells its address either as the prefix beside a slug or as a whole route of
    # its own, and the prefix line is a definition rather than a page.
    prefix = re.search(r'#define\s+SITE_PREFIX\s+"([^"]+)"', table)
    prefix = prefix.group(1) if prefix else "/docs/"

    absolute = {
        prefix + slug for slug in re.findall(r'SITE_PREFIX\s*"([a-z0-9-]+)"', table)}
    absolute |= {route for route in re.findall(r'"(/[a-z0-9][a-z0-9/-]*)"', table)
                 if route != prefix}

    mapped = set(re.findall(r"'(/[^']+)':\s*\{", shell))

    for route in sorted(absolute - mapped):
        fail("examples/web/site_shell.html", f"the navigation table reaches {route} and "
             "no title is stamped for it")

    for route in sorted(mapped - absolute):
        fail("examples/web/site_shell.html", f"it stamps a title for {route}, which no "
             "navigation row reaches")


def main():
    slots, abi = backend_facts()

    check_numbers(slots, abi)
    check_links()
    check_headers(slots)
    check_pages()
    check_api_page()
    check_routes()

    if errors:
        print(f"docs drift: {len(errors)} problem(s)\n")

        for error in errors:
            print(f"  {error}")

        print("\nA document that disagrees with the header it describes is the bug; "
              "the header is the truth.")
        return 1

    print(f"docs drift: clear - a backend fills {NUMBER_WORD.get(len(slots), len(slots))} slots, "
          f"the contract is at "
          f"ABI {abi}, every document link lands, every header is named, and no page "
          "states a value tokens.h does not own")
    return 0


if __name__ == "__main__":
    sys.exit(main())
