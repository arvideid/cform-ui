# Maintaining cform-ui

Whoever lands a change here, and whoever reports something broken, reads this file.
It is the human half of keeping the promises in [`versioning.md`](versioning.md): the
judgement a script cannot make, written down so it is applied the same way twice.

The machine half is a list of commands that all run on every push. Everything in it is
runnable on your own machine before you open the pull request, and a gate that cannot be
run locally is a gate that will be argued with.

## The gates, and what each one is for

| gate | run it | it fails when |
| --- | --- | --- |
| Format | `clang-format --dry-run --Werror` over the files you touched | the style in [`.clang-format`](../.clang-format) is not what arrived |
| Layer graph | `python3 tools/check_includes.py` | a source or a public header includes a layer it may not link, reaches into `src/` internals, or lets a raw value do a sentinel's job |
| Version consistency | `cmake -P cmake/cform_version.cmake --check` | `CMakeLists.txt` and [`include/cf/version.h`](../include/cf/version.h) state different numbers, which is a release that lies |
| API surface | `python3 tools/check_api.py --baseline tests/api/0.1.0.txt` | the surface differs from the last release by more than the version allows. `--probe` checks the classifier itself, so a rule nobody can trigger does not survive |
| Documentation drift | `python3 tools/check_docs.py` | a sentence names a slot count or an ABI number the header disagrees with; a link lands nowhere; a header is named by no document; the API page and the headers disagree in either direction; a site route is spelled differently in its two homes |
| Site style discipline | the same `check_docs.py` | a page states a colour, a length or a radius that `tokens.h` was supposed to own |
| Tests | `cmake --preset dev && cmake --build --preset dev && ctest --preset dev` | anything, including a page of the docs site that renders no heading |
| Sanitisers | the `sanitize` job: ASan and UBSan over the whole suite | a frame writes outside the arena it was handed |
| Installed consumer | the `package` job | a tree that builds here does not configure, link and run after `cmake --install` — or accepts a package from another minor line |
| Conformance | `./build/dev/bin/yoga_cases` | a layout answer moved away from the reference implementation, or a category's pass count fell under its floor |
| Benchmarks | the `build` job archives `bench-<os>-<type>.log` | never red. A number that got worse is a judgement; a number that got better by an implausible margin is usually a measurement that stopped measuring |
| Public reachability | `python3 tools/check_reachability.py` | not yet. It prints what it finds today, and turns red one release after it lands — see below |

Every rule above has been broken on purpose and named once: one edited word or one added
line, the gate's own sentence read back, the file put back. A gate nobody has watched fail
is a gate nobody can trust, and each failure message is written to say what to do.

Two of these are worth arguing for, because both could have been left as review comments.

**Documentation drift.** This repository has already shipped a README claiming a number
the header disagreed with, twice, in two files. A document that drifts is worse than a
missing one: it is read as fact by the person it misleads. So the counts are not written
down a second time, and where a sentence has to state one, the gate reads the header and
refuses the sentence.

**Site style discipline.** The docs site is themed through `tokens.h`, which is the only
file allowed to name a tone, a length or a radius. A copied hex is the single most likely
edit a well-meaning contributor makes, and once one is in, the rule is a suggestion.

## Triage

A report is worth the maintainer's time in proportion to how reproducible it is, so the
rules below are about turning a report into something reproducible rather than about
being fast.

**A reproducible headless failure gets a regression test before a fix.** Reproduce it in
`tests/unit/` against the recorder or the golden tier, watch the new test fail, and only
then change the library. A fix written against a bug nobody can re-fail is a fix that
gets reverted by the next refactor.

**A rendering complaint needs a named device and a screenshot in the issue.** Not in the
repository: committed screenshots are binaries, and a paint bug's evidence is worth one
conversation and not forever. Ask for the GPU or the browser and the picture, fix against
the golden command stream, and the artifact stays in the issue.

**A layout disagreement with another engine is a conformance question.** Check it against
the corpus first (`./build/dev/bin/yoga_cases`) and against
[`conformance.md`](conformance.md). A named difference already recorded there is
documented behaviour and the fix is to the documentation; a new one goes in the known
list only with a sentence saying why cform's answer is the better one.

**An API that is awkward to call is a design conversation**, not a patch. Propose the
append; the versioning rules say what is free to add and what is not, and `check_api.py`
will say it again in the pull request.

### Labels

Four, because scheduling is the only distinction that changes what happens next.

| label | means |
| --- | --- |
| `bug` | the library does not do what its documentation says. This is the only label that makes a fix urgent |
| `enhancement` | a thing that would be better. Deferred freely, and never a reason to break a caller |
| `needs-repro` | open question to the reporter. No work happens while it is set, and it closes on staleness rather than on judgement |
| `good-first-issue` | self-contained, and the comment says which test file the fix belongs in |

## Releasing

[`releases.md`](releases.md) is the procedure and the only place it is written. Two
things belong here rather than there, because they are policy and not steps:

**A tag is published by exactly one person**: the maintainer named in the repository.
Not because the mechanics are dangerous — `git tag` and `git push` are two commands — but
because a tag makes a promise on behalf of everyone who uses the library, and two people
who can cut one is two people who can disagree about what it promises.

**Nothing gets pushed from inside a feature branch's work.** A branch is reviewed,
rebased, merged with `--ff-only`, and the tag is cut on `main` afterwards.

## Reporting a problem privately

A security problem is not filed as an issue. Use GitHub's **Report a vulnerability** form
on the repository's Security tab, which reaches the maintainer without telling anyone
else. Ask for the affected version and the platform, and expect an answer within a week;
a pre-1.0 library fixes forward into the next release rather than maintaining an old one,
and the release notes for that version will name what was fixed. No embargo is promised,
and none is refused: the maintainer agrees one with the reporter.

The fix ships as an ordinary release with an ordinary version number. A security fix that
adds a symbol is a MINOR, one that only changes behaviour is a PATCH, and one that has to
break a caller to be fixed is a MAJOR — the same rules as any other change, because a
reporter deserves a fast answer and adopters deserve an honest number.

## Reachability: the rule, and the day it bites

Every public name should have a caller in the tree: in the library itself, in an example
a person can run, or in the consumer program that CI builds against an installed tree. An
API nothing calls has never had to work, and the first person to discover that it does not
is an adopter.

`tools/check_reachability.py` runs on every push and prints what it finds, exiting zero.
The list is empty today, and that is exactly why it waits a release: a rule that has only
ever seen a clean tree has not been tested by anything but its own probe. It becomes a
gate the release after the printed list is either empty or explained in the allow-list with
a reason — a symbol that exists for a host to call is legitimate, and a symbol nobody can
say why is a symbol to delete. Turning a rule red on the day it lands, against a list of
pre-existing rows, teaches people to silence it rather than to fix it.

## Dependencies

A dependency is added when it removes more code than it imports. Not "when it is
popular", not "when it saves an afternoon": the arithmetic includes the years of
carrying it, the pins in `CMakeLists.txt` that go stale, the toolchain matrix it drags
in, and the license.

That bar is why the tree has two: SDL3 for the window, SDL3_ttf for the faces. Layout is
not a dependency because the conformance corpus tests against the reference implementation
rather than linking it, and the amalgamated header exists so a caller can take one file
instead of a build system.

Every pin is named in `CMakeLists.txt` and in `.github/workflows`, and
[`deps.yml`](../.github/workflows/deps.yml) reports the gap between a pin and what upstream
published, weekly, without ever failing a build. A report nobody reads is noise, so it
prints one table and nothing else — and the reason it is never red is that a scheduled
job going red at three in the morning teaches no one anything except to disable it.

## What is deliberately not here

No `CHANGELOG.md`, because the tags and the commit messages are the source and a committed
rendering of them is committed build output. No `CONTRIBUTING.md`, because the rules are
this file and the CI that enforces them. No `SECURITY.md`, because the paragraph above is
the whole policy and a file is one more thing to keep from drifting. No roadmap file,
because [`backlog.md`](backlog.md) is a dated record of decisions and a wish list needs a
different owner than the person who has to answer issues about it.

If a rule in this file has a gate beside it, the gate is the rule and this file is the
commentary. If it does not, it is judgement, and a pull request is where it gets argued.
