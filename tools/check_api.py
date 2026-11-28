#!/usr/bin/env python3
"""Say what a change to the public surface costs, and refuse what it cannot.

Compares the surface tools/api_surface.py prints for the tree against a
committed baseline under tests/api/, then against the version the header states,
and reports the smallest bump the diff would be legal under. docs/versioning.md
owns the words; this owns the check that they are kept.

The classes are not a judgement about how the code looks. They are about what a
program written against the baseline does when it is rebuilt against the tree:
source that compiles and runs unchanged, source that compiles and runs changed,
and source that keeps compiling and stores the wrong number.

Run it
    python tools/check_api.py                   # gate the tree against the last release
    python tools/check_api.py --baseline tests/api/0.1.0.txt   # against an older one
    python tools/check_api.py --probe           # check the classifier itself
"""

import argparse
import pathlib
import re
import sys

import api_surface

ROOT = pathlib.Path(__file__).resolve().parent.parent
INCLUDE = ROOT / "include" / "cf"
DOCS = ROOT / "docs"
API = ROOT / "tests" / "api"

# Ordered by what a caller owes, with RENUMBER past a MAJOR: a MAJOR is a break
# someone can read about in release notes and migrate, and a renumbered member is
# not even that - the old number still compiles and means something else.
LEVELS = ("NONE", "PATCH", "MINOR", "MAJOR", "RENUMBER")
ABI_MACRO = "CF_BACKEND_ABI_VERSION"


def read_surface():
    """The tree's own surface, or None with a message when a header is unreadable."""
    paths = sorted(INCLUDE.glob("*.h"))
    text = api_surface.print_surface(paths)

    if text is None:
        print("check api: the surface could not be read; the message above is the reason",
              file=sys.stderr)

    return text


def items_of(text):
    """`kind|name|shape` lines keyed by (kind, name), which is an item's identity.

    Two items sharing an identity is itself a bug in the surface tool rather than
    something a caller can have caused, so it stops here loudly.
    """
    keyed = {}

    for line in text.splitlines():
        if not line.strip():
            continue
        parts = line.split("|", 2)  # the shape may itself contain a pipe
        if len(parts) != 3:
            raise ValueError(
                "a baseline line that is not kind|name|shape: " + line)
        key = (parts[0], parts[1])
        if key in keyed and keyed[key] != parts[2]:
            raise ValueError(
                "one name stated twice with two shapes: " + parts[1])
        keyed[key] = parts[2]

    return keyed


def enum_of(name):
    return name.split(".")[0]


def number(shape):
    return int(shape) if shape.lstrip("-").isdigit() else None


def classify(baseline, current):
    """The diff as (level, notes): the smallest bump it allows and what it found.

    A renumbered enum member is not a level, it is a refusal: nothing a caller
    compiled against the baseline can be made to work by a version number.
    """
    notes = []
    level = "NONE"

    def raise_to(wanted):
        nonlocal level
        if LEVELS.index(wanted) > LEVELS.index(level):
            level = wanted

    for key in sorted(set(baseline) & set(current)):
        kind, name = key
        if baseline[key] == current[key]:
            continue

        if kind == "value":
            notes.append("%s changed number, %s to %s" %
                         (name, baseline[key], current[key]))
            raise_to("RENUMBER")
            continue

        notes.append("%s %s is not what it was: %s to %s" %
                     (kind, name, baseline[key], current[key]))
        raise_to("MAJOR")

    for key in sorted(set(baseline) - set(current)):
        kind, name = key
        notes.append(
            "%s %s is gone, which a caller cannot compile past" % (kind, name))
        raise_to("MAJOR")

    added_by_enum = {}

    for key in sorted(set(current) - set(baseline)):
        kind, name = key
        if kind == "value":
            added_by_enum.setdefault(
                enum_of(name), []).append(number(current[key]))
            continue
        notes.append("%s %s added" % (kind, name))
        raise_to("MINOR")

    for enum, numbers in sorted(added_by_enum.items()):
        existing = [number(shape) for (kind, name), shape in baseline.items()
                    if kind == "value" and enum_of(name) == enum and number(shape) is not None]
        highest = max(existing) if existing else None

        for value in sorted(numbers):
            if highest is None:
                notes.append("enum %s added with its members" % enum)
                raise_to("MINOR")
                continue
            if value < highest:
                notes.append("%s.%s takes number %d, below %d already in use - a stored "
                             "number from the baseline now names a different member" %
                             (enum, value, value, highest))
                raise_to("RENUMBER")
            else:
                notes.append("%s.%d appended" % (enum, value))
                raise_to("MINOR")

    return level, notes


def version_of(path):
    """(major, minor, patch) a version string states, or None."""
    match = re.match(r"^(\d+)\.(\d+)\.(\d+)", path.strip())
    return tuple(int(one) for one in match.groups()) if match else None


def header_version():
    for kind, name, shape in api_surface.surface_of(INCLUDE / "version.h"):
        if kind == "macro" and name in ("CF_VERSION_MAJOR", "CF_VERSION_MINOR",
                                        "CF_VERSION_PATCH"):
            yield int(shape)


def bump_between(before, after):
    """The level of the change between two version triples."""
    if before == after:
        return "NONE"
    if before[0] != after[0]:
        return "MAJOR"
    if before[1] != after[1]:
        return "MINOR"
    if before[2] != after[2]:
        return "PATCH"
    return "NONE"


def ceiling(before, after):
    """The most severe diff the bump from `before` to `after` is allowed to carry.

    docs/versioning.md: while the major number is `0`, the MINOR is where a break still
    lives, so below 1.0 a MINOR bump carries what would need a MAJOR above it. A PATCH
    carries fixes at either side of that line, and a version that did not move carries
    nothing at all.
    """
    allowed = bump_between(before, after)

    if allowed == "MINOR" and after[0] == 0:
        return "MAJOR"

    return allowed


def abi_change(baseline, current):
    """(old, new) for the backend ABI number, when it moved at all."""
    old = baseline.get(("macro", ABI_MACRO))
    new = current.get(("macro", ABI_MACRO))
    return (old, new) if old != new else None


def abi_is_documented(new):
    """Whether docs/backend-abi.md names what the new number added."""
    path = DOCS / "backend-abi.md"
    if not path.exists():
        return False
    return re.search(r"\b%s\b" % re.escape(str(new).rstrip("u")),
                     path.read_text(encoding="utf-8")) is not None


def latest_baseline(stated):
    """The committed surface this tree ships on top of, or None when there is none.

    The newest baseline the header does not exceed. A higher one describes a version this
    tree does not claim, and the newest at or below it is the last release that shipped -
    docs/releases.md commits a release's own baseline in the same branch that prepares it,
    which is why a release branch names the previous tag by hand instead.
    """
    shipped = [(version_of(path.stem), path) for path in API.glob("*.txt")]
    at_or_below = [one for one in shipped if one[0] is not None and one[0] <= stated]

    return max(at_or_below, default=(None, None))[1]


def gate(baseline_path=None):
    """0 when the tree may ship under the version it states, 1 when it may not."""
    stated = tuple(header_version())
    if len(stated) != 3:
        print(
            "check api: include/cf/version.h does not state three numbers", file=sys.stderr)
        return 1

    if baseline_path is None:
        baseline_path = latest_baseline(stated)
        if baseline_path is None:
            print("check api: no committed baseline at or below %d.%d.%d under tests/api/, so "
                  "there is nothing to measure this surface against.\n"
                  "    python tools/api_surface.py -o tests/api/<last-release>.txt\n"
                  "docs/releases.md writes that file when it cuts a tag." % stated,
                  file=sys.stderr)
            return 1

    if not baseline_path.exists():
        print("check api: no baseline at %s. Refresh it with\n"
              "    python tools/api_surface.py -o tests/api/<last-release>.txt\n"
              "and review the file as data, the way tests/golden is reviewed." %
              baseline_path, file=sys.stderr)
        return 1

    current = read_surface()
    if current is None:
        return 1

    try:
        before = items_of(baseline_path.read_text(encoding="utf-8"))
        after = items_of(current)
    except ValueError as error:
        print("check api: " + str(error), file=sys.stderr)
        return 1

    moved = abi_change(before, after)
    if moved and not abi_is_documented(moved[1]):
        print("check api: %s moved from %s to %s and docs/backend-abi.md names nothing about "
              "%s. The table is where a backend author learns what to implement." %
              (ABI_MACRO, moved[0], moved[1], moved[1]), file=sys.stderr)
        return 1

    level, notes = classify(before, after)

    baseline_version = version_of(baseline_path.stem)
    if baseline_version is None:
        print("check api: %s does not name a version, so there is no bump to read the "
              "diff against." % baseline_path.name, file=sys.stderr)
        return 1

    if level == "RENUMBER":
        print("check api: the public surface renumbers something a caller has already "
              "stored.", file=sys.stderr)
        for note in notes:
            print("    " + note, file=sys.stderr)
        print("check api: an enum member's number is the ABI. Append, or move nothing - no "
              "version bump buys this back.", file=sys.stderr)
        return 1

    allowed = ceiling(baseline_version, stated)

    if level != "NONE" and allowed == "NONE":
        print("check api: the surface moved by %s and include/cf/version.h still says the "
              "baseline's number. Bump the header." % level, file=sys.stderr)
        for note in notes:
            print("    " + note, file=sys.stderr)
        return 1

    if LEVELS.index(level) > LEVELS.index(allowed):
        print("check api: this diff needs a %s bump; the header says %d.%d.%d over the "
              "baseline %s." % (
                  level, stated[0], stated[1], stated[2], baseline_path.stem),
              file=sys.stderr)
        for note in notes:
            print("    " + note, file=sys.stderr)
        print(
            "check api: docs/versioning.md says what each bump promises.", file=sys.stderr)
        return 1

    print("check api: %d item(s) over %s, %d.%d.%d carries it (%s)." %
          (len(notes), baseline_path.stem, stated[0], stated[1], stated[2], level))
    if moved:
        print("check api: the backend ABI moved to %s and docs/backend-abi.md names it." %
              moved[1])
    return 0


def probe():
    """The classifier and the bump rule against the diffs a reviewer cannot eyeball.

    The surface cases are built from the tree's own surface, so the probe is testing the
    classifier rather than a fixture someone kept in step by hand. The bump cases are
    plain version triples, because what they check is which bump may carry which break.
    """
    text = read_surface()
    if text is None:
        return 1

    base = items_of(text)
    failures = 0

    def expect(name, before, after, wanted):
        nonlocal failures
        level, notes = classify(before, after)
        if level != wanted:
            print("probe %s: wanted %s, got %s (%s)" % (name, wanted, level, "; ".join(notes)),
                  file=sys.stderr)
            failures += 1
        else:
            print("probe %s: %s, as the rule says" % (name, level))

    added = dict(base)
    added[("func", "cf_a_new_call")] = "void cf_a_new_call(void)"
    expect("a new function is a MINOR", base, added, "MINOR")

    renumbered = dict(base)
    length_kind = ("value", "cf_length_kind.CF_LENGTH_PERCENT")
    if length_kind not in renumbered:
        print("probe: cf_length_kind is not in the surface, so the renumber probe proves "
              "nothing", file=sys.stderr)
        return 1
    renumbered[length_kind] = "9"
    expect("a renumbered member is refused outright",
           base, renumbered, "RENUMBER")

    removed = dict(base)
    removed.pop(length_kind)
    expect("a removed member is a MAJOR", base, removed, "MAJOR")

    reshaped = dict(base)
    reshaped[("func", "cf_version_string")] = "int cf_version_string(void)"
    expect("a changed signature is a MAJOR", base, reshaped, "MAJOR")

    unchanged = dict(base)
    expect("nothing moved is nothing to ship", base, unchanged, "NONE")

    inserted = dict(base)
    inserted[("value", "cf_length_kind.CF_LENGTH_EARLY")] = "1"
    expect("a number taken from below the last is refused",
           base, inserted, "RENUMBER")

    def expect_ceiling(name, before, after, wanted):
        nonlocal failures
        got = ceiling(before, after)
        if got != wanted:
            print("probe %s: wanted %s, got %s" % (name, wanted, got), file=sys.stderr)
            failures += 1
        else:
            print("probe %s: %s, as the rule says" % (name, got))

    # The rule docs/versioning.md states and the gate has to enforce: below 1.0 the MINOR
    # is where a break lives, a PATCH never carries one, and a number that did not move
    # carries nothing.
    expect_ceiling("a 0.MINOR bump carries a break", (0, 1, 0), (0, 2, 0), "MAJOR")
    expect_ceiling("a 0.PATCH bump carries fixes only", (0, 2, 0), (0, 2, 1), "PATCH")
    expect_ceiling("a version that did not move carries nothing", (0, 2, 0), (0, 2, 0), "NONE")
    expect_ceiling("crossing into 1.0 is a MAJOR bump", (0, 2, 0), (1, 0, 0), "MAJOR")
    expect_ceiling("above 1.0 a break needs the major", (1, 2, 3), (1, 3, 0), "MINOR")

    return 1 if failures else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--baseline", default=None,
                        help="the committed surface to compare against (default: the newest "
                             "tests/api/<version>.txt the stated version does not exceed)")
    parser.add_argument("--probe", action="store_true",
                        help="check the classifier rather than the tree")
    arguments = parser.parse_args()

    if arguments.probe:
        return probe()

    return gate(pathlib.Path(arguments.baseline) if arguments.baseline else None)


if __name__ == "__main__":
    sys.exit(main())
