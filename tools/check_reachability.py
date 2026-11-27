#!/usr/bin/env python3
"""Report a public symbol nothing outside a test calls.

"An API the shell cannot service is worse than a missing one" is about exactly this
failure: a declaration whose signature was never exercised by a real caller, whose
behaviour no example demonstrates, and which is therefore discovered to be wrong by the
first adopter who needs it. Every public name should be reachable from the library's own
code, from an example, or from the consumer program that a CI job builds against an
installed tree.

Informational for one release, then gating. A rule that fires on ninety pre-existing rows
the day it lands teaches people to silence it rather than to fix it, so the default exit
code is zero and `--strict` is the day the list has been worked through.

The allow-list is not an escape hatch, it is the argument: every entry names why a symbol
has no caller in the tree, and an entry that cannot say is a symbol to delete.
"""

import api_surface  # the parser the version gate uses, so the two cannot disagree
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))


ROOT = pathlib.Path(__file__).resolve().parent.parent
INCLUDE = ROOT / "include" / "cf"

# Where a caller counts as existing. `src/` is the library using itself, `examples/` is a
# program a person can run, and `tests/consumer/` is the outside view a packager gets -
# a unit test is deliberately not here, because a symbol only a test calls has still
# never had to work in an application.
CALLERS = [ROOT / "src", ROOT / "examples", ROOT / "tests" / "consumer"]

# A definition or a prototype, as this tree writes one: the statement begins the line at
# column zero with a return type before the name, and its argument list is followed by a
# body or a semicolon. clang-format keeps top-level definitions flush left and indents
# every call, which is what makes the two tellable apart without a full parser.
TYPE_BEFORE = re.compile(r"^[A-Za-z_][A-Za-z0-9_ \*]*$")

# A public name, spelled either way: lowercase for a function, uppercase for the macro
# that saves a caller writing the same thing twice. Both halves are in the surface, so
# both halves have to be looked for.
NAME = r"(?:cf_[a-z0-9_]+|CF_[A-Z0-9_]+)"
CALL_OF = re.compile(r"\b(" + NAME + r")\s*\(")
ADDRESS_OF = re.compile(r"&\s*(" + NAME + r")\b")

# Symbols the host or the build calls, so no caller in this tree can exist.
ALLOW = {
    # A port owns these: the frame driver hands them to the platform, the platform answers
    # back, and nothing inside the library has a reason to call a program's own functions.
    "cf_backend_sdl": "the entry point a host compiles its backend against",
    "cf_backend_recorder": "the headless backend a test harness installs, never the library",
    "cf_font_sdl": "a caller opens a face; the library only reads one",
    "cf_font_sdl_close": "the pair of cf_font_sdl, and the caller owns the handle",
    "cf_clipboard_sdl": "a host decides whether text leaves the window at all",
}

# Declared for a caller that lives in another language or another repository.
SKIP_PREFIXES = ("cf_command_",)


def body_end(text, opener):
    """The index just past the argument list that starts at `opener`."""
    depth = 0
    index = opener

    while index < len(text):
        if text[index] == "(":
            depth += 1
        elif text[index] == ")":
            depth -= 1
            if depth == 0:
                return index
        elif text[index] == ";":
            # A prototype, not a body: the statement ends before any brace could.
            return -1
        index += 1

    return -1


def declared():
    """Every public function name, and the header that names it."""
    found = {}

    for header in sorted(INCLUDE.glob("*.h")):
        for kind, name, _ in api_surface.surface_of(header):
            if kind in ("func", "macro-fn") and name not in found:
                found[name] = header.name

    return found


COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)


def sources():
    for root in CALLERS:
        if not root.exists():
            continue

        for path in sorted(root.rglob("*.[ch]")):
            # Comments come out before the search: a sentence that names a function is not
            # a call to it, and letting prose excuse an unreachable API is how this gate
            # would quietly stop meaning anything.
            text = path.read_text(encoding="utf-8", errors="replace")
            yield path, COMMENT.sub(" ", text)


def used_names():
    """Every public name a caller reaches, its own definition excepted.

    A function body written in `src/` is the symbol existing, not the symbol being used:
    counting it would mark the whole library reachable and turn this into a report no
    one has to read. A name taken as a value (`&cf_x`) counts, because something outside
    the declaration is holding on to it.
    """
    used = set()

    for _, text in sources():
        used.update(match.group(1) for match in ADDRESS_OF.finditer(text))

        for match in CALL_OF.finditer(text):
            begin = text.rfind("\n", 0, match.start()) + 1
            leading = text[begin:match.start()]
            closed = body_end(text, match.end() - 1)
            opens_a_body = closed != - \
                1 and text[closed + 1:closed + 9].lstrip().startswith("{")
            flush_left = leading == leading.lstrip() and TYPE_BEFORE.match(leading) is not None

            # Flush left with a type before the name, and either a body or a `;` after the
            # arguments: this is where the symbol is written down, not where it is used.
            if flush_left and (opens_a_body or closed == -1):
                continue

            used.add(match.group(1))

    return used


def main(argv):
    strict = "--strict" in argv
    names = declared()
    used = used_names()

    orphans = []

    for name, header in sorted(names.items()):
        if name in used or name in ALLOW or name.startswith(SKIP_PREFIXES):
            continue

        orphans.append((name, header))

    print(f"public reachability: {len(names)} exported functions, "
          f"{len(orphans)} without a caller in src/, examples/ or tests/consumer/")

    for name, header in orphans:
        excuse = "  <-- name it in the allow-list with a reason, or reach it"
        print(f"  {name:<34} declared in include/cf/{header}{excuse}")

    if orphans:
        print("\nAn API nothing in the tree calls has never had to work. Either a caller "
              "exists, or the allow-list says who outside the tree calls it, or the "
              "symbol goes.")

        if strict:
            return 1

        print("Not gating yet: the rule turns red one release after it lands, once the "
              "list above is either explained or emptied.")

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
