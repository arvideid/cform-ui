#!/usr/bin/env python3
"""Write a release's notes from the commits, and the index the site reads.

The tree keeps no CHANGELOG.md: the tags and the Conventional Commits between
them are the source, and a committed rendering of them is build output that
rots the day someone amends a message. This generates the two things a release
needs at the moment it is published - the body of the GitHub Release, and the
JSON a /releases page and any outside tool reads.

Run it
    python3 tools/release_notes.py --tag v0.2.0 \
        --notes release.md --index releases.json --repo <repository url>

With no tag to describe against yet - the first release, or a dry run over the
commits sitting unpushed on main - name the range yourself: `--range origin/main..`.

What it takes seriously:

- a commit whose footer names a call-site edit is a break, and the Break section
  quotes the edit rather than the type name, because a reader wants the line to
  change and not the category it fell in;
- the backend ABI number is reported when it moved in the range, since a driver
  author reads that line before any other;
- the conformance and bench records are named when their files changed, so a
  number that moved can be traced to the commit that moved it.
"""

import argparse
import json
import pathlib
import re
import subprocess
import sys

RECORDS = {
    "docs/conformance.md": "the conformance score",
    "docs/bench.md": "the bench numbers",
    "docs/backend-abi.md": "the backend slot contract",
    "docs/versioning.md": "the versioning contract",
}

# The type set the commit convention uses, in the order a reader wants them:
# what someone gets, what broke, what got faster, what got cleaned.
SECTIONS = (
    ("feat", "New"),
    ("fix", "Fixed"),
    ("perf", "Faster"),
    ("refactor", "Changed internally"),
    ("build", "Build"),
    ("ci", "CI"),
    ("test", "Tests"),
    ("docs", "Documentation"),
    ("chore", "Housekeeping"),
)

COMMIT = re.compile(r"^(?P<type>[a-z]+)(?:\((?P<scope>[^)]+)\))?(?P<bang>!)?: "
                    r"(?P<subject>.+)$")
BREAK_FOOTER = re.compile(r"^BREAKING(?:[ -]CHANGE)?(?::|\s*!\s*:?)[ \t]*(?P<text>.+)",
                          re.MULTILINE)
ABI_DEFINE = re.compile(r"#define\s+CF_BACKEND_ABI_VERSION\s+(\d+)")
VERSION_TAG = re.compile(r"^v\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?$")

FIELD, RECORD = "\x1e", "\x1f"


def git(*arguments):
    """One git call, or None when git itself refuses."""
    result = subprocess.run(("git",) + arguments, capture_output=True, text=True,
                            encoding="utf-8", errors="replace")
    return result.stdout if result.returncode == 0 else None


def resolves(revision):
    """Whether git knows this revision at all, which a first tag does not."""
    return git("rev-parse", "--verify", "--quiet", revision + "^{commit}") is not None


def range_of(tag):
    """The commits this release adds, as (previous, revision range)."""
    described = git("describe", "--tags", "--abbrev=0", tag + "^@")

    if described and described.strip():
        previous = described.strip()
        return previous, "%s..%s" % (previous, tag)

    return None, tag


def commits(revision_range):
    """Parsed Conventional Commits, oldest first, with their full messages."""
    # A commit body is free text of any line count, so records are cut on
    # control characters a message cannot carry rather than on newlines.
    printed = git("log", "--reverse", revision_range, "--no-merges",
                  "--pretty=format:%H" + FIELD + "%s" + FIELD + "%B" + RECORD)
    if printed is None:
        return []

    parsed = []

    for chunk in printed.split(RECORD):
        parts = chunk.strip("\n").split(FIELD)
        if len(parts) < 3:
            continue

        digest, subject, body = parts[0].strip(), parts[1].strip(), parts[2].strip()
        match = COMMIT.match(subject)
        break_line = BREAK_FOOTER.search(body or subject)

        parsed.append({
            "hash": digest,
            "type": match.group("type") if match else "other",
            "scope": match.group("scope") if match else None,
            "subject": match.group("subject") if match else subject,
            "breaking": bool(match and match.group("bang")) or break_line is not None,
            "change": break_line.group("text").strip() if break_line else None,
            "short": digest[:8],
        })

    return parsed


def abi_at(revision):
    """The backend ABI number a revision states, or None if it predates it."""
    text = git("show", "%s:include/cf/backend.h" % revision)
    if text is None:
        return None
    match = ABI_DEFINE.search(text)
    return match.group(1) if match else None


def touched(revision_range, wanted):
    """Whether a named file changed in the range.

    `git log` rather than `git diff`: a range with no left side - the first tag, whose
    notes cover the whole history - has no base to diff against, while the commits it
    walks are exactly the ones whose files moved.
    """
    listing = git("log", "--name-only", "--pretty=format:", revision_range, "--", wanted)
    return bool(listing and listing.strip())


def notes_for(tag, previous, revision_range, url):
    """The release body: what a reader would ask in the order they would ask it."""
    listed = commits(revision_range)
    breaking = [one for one in listed if one["breaking"]]
    # A dry run over commits no tag points at yet has no tagged tree to read, so the
    # range's own tip stands in for the release commit.
    after = tag if resolves(tag) else (git("rev-list", "-1", revision_range)
                                       or "HEAD").strip()
    # Compare the ABI against what the notes actually start from: the previous release
    # when there is one, the left side of a hand-named range otherwise, and the tagged
    # commit's parent when the range runs from the beginning of the history.
    left = revision_range.split("..", 1)[0].strip() if ".." in revision_range else ""
    abi_before, abi_after = abi_at(previous or left or after + "^"), abi_at(after)

    lines = ["cform-ui %s" % tag.lstrip("v"), ""]

    if url:
        lines += ["Built from the tree at [`%s`](%s)." % (tag, url), ""]

    if breaking:
        lines += ["## Breaking", "",
                  "Each one names the edit at the call site. A break is only a break "
                  "if nobody wrote down how to cross it.", ""]
        for one in breaking:
            detail = one["change"] or one["subject"]
            lines.append("- **%s** — %s (`%s`)" % (one["scope"] or "general", detail,
                                                   one["short"]))
        lines.append("")

    for kind, heading in SECTIONS:
        group = [one for one in listed if one["type"] == kind]
        if not group:
            continue
        lines += ["## " + heading, ""]
        for one in group:
            where = "%s: " % one["scope"] if one["scope"] else ""
            lines.append("- %s%s (`%s`)" % (where, one["subject"], one["short"]))
        lines.append("")

    kinds = {kind for kind, _ in SECTIONS}
    # Anything the convention does not name still belongs in the notes: a
    # silently dropped commit is a change nobody reading the page can find.
    loose = [one for one in listed if one["type"] not in kinds]
    if loose:
        lines += ["## Other", ""]
        lines += ["- %s (`%s`)" % (one["subject"], one["short"]) for one in loose]
        lines.append("")

    if abi_before and abi_after and abi_before != abi_after:
        lines += ["## The backend ABI moved", "",
                  "`CF_BACKEND_ABI_VERSION` went from %s to %s. A driver built against "
                  "%s is refused before frame one rather than drawing something wrong; "
                  "rebuild it, and read what the added slots mean in docs/backend-abi.md."
                  % (abi_before, abi_after, abi_before), ""]

    records = [what for path, what in RECORDS.items() if touched(revision_range, path)]
    if records:
        lines += ["## Records that moved", "",
                  "These files changed in the range these notes cover, so a number that "
                  "moved can be traced to the commit that moved it:", ""]
        lines += ["- " + what for what in records]
        lines.append("")

    if not listed:
        lines += ["No commits were found in this range.", ""]

    return "\n".join(lines).rstrip() + "\n", abi_after or ""


def index(url_base):
    """Every tag the repository has, newest first, as the site reads it."""
    listed = git("tag", "--sort=-v:refname") or ""
    entries = []

    for tag in [one for one in listed.splitlines() if VERSION_TAG.match(one)]:
        stamp = git("log", "-1", "--format=%cF", tag)
        # Highlights are what the release brought, which means the range since the
        # tag before it. A release's own commit is one commit on purpose, and a
        # page built from it would show one line for a whole minor.
        previous, revision_range = range_of(tag)
        highlights = [one["subject"] for one in commits(revision_range)
                      if one["type"] == "feat"][:6]
        entries.append({
            "version": tag.lstrip("v").split("-")[0],
            "tag": tag,
            "date": stamp.strip() if stamp else None,
            "url": (url_base.rstrip("/") + "/releases/tag/" + tag) if url_base else None,
            "abi": abi_at(tag),
            "prerelease": "-" in tag.lstrip("v"),
            "highlights": highlights,
        })

    return entries


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--tag", required=True, help="the tag being released, e.g. v0.2.0")
    parser.add_argument("--notes", help="where to write the release body")
    parser.add_argument("--index", help="where to write the JSON list of every release")
    parser.add_argument("--url", default="", help="this tag's page, for the body's link")
    parser.add_argument("--repo", default="", help="the repository root, for the index links")
    parser.add_argument("--range", dest="revision_range", help=""
                        "the commit range to read instead of the one the tags describe")
    arguments = parser.parse_args()

    if not VERSION_TAG.match(arguments.tag):
        print("release notes: %s is not a version tag" % arguments.tag, file=sys.stderr)
        return 1

    previous = None
    if arguments.revision_range:
        revision_range = arguments.revision_range
    elif resolves(arguments.tag):
        previous, revision_range = range_of(arguments.tag)
    else:
        print("release notes: %s is not in this repository, and no --range was given"
              % arguments.tag, file=sys.stderr)
        return 1

    body, _ = notes_for(arguments.tag, previous, revision_range, arguments.url)

    if arguments.notes:
        destination = pathlib.Path(arguments.notes)
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(body, encoding="utf-8", newline="\n")

    sys.stdout.write(body)

    if arguments.index:
        destination = pathlib.Path(arguments.index)
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(json.dumps(index(arguments.repo), indent=2) + "\n",
                               encoding="utf-8", newline="\n")

    return 0


if __name__ == "__main__":
    sys.exit(main())
