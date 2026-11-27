# Does the layout match the real thing?

**Conformance** means "agrees with the specification, as someone else
implemented it". Our own tests can only prove we match what we already believe.
To find out whether we're actually *right*, we run the same screens through
[Facebook Yoga](https://github.com/facebook/yoga) — the layout engine React
Native uses — and compare the numbers it published with the numbers `cf_layout`
produces.

Yoga is MIT-licensed. Its repository checks in the output of rendering each of
its test screens in a real browser: what styles were set on every node, and what
box the browser measured for it. Those recorded files are the answer key.

Run it:

```sh
ctest --preset dev                # unit, conformance and golden tiers
python tools/gentest.py           # rebuild the answer key from .vendor/yoga
clang-format -i tests/conformance/yoga_cases_data.h
```

## Where the answer key comes from

Yoga renders each HTML fixture in headless Chrome at 1× scale and checks in what
it did: the style calls on every node, beside the box the browser measured.
`tools/gentest.py` translates those calls into `cf_style` rows and commits the
result as ordinary C data in `tests/conformance/yoga_cases_data.h`, so nothing
reads Yoga or a browser at test time. The fixtures come from
[facebook/yoga](https://github.com/facebook/yoga) at `503bec3`, MIT-licensed,
Copyright (c) Facebook, Inc. and its affiliates.

Two traps if you touch the generator. Yoga's `GetLeft` and `GetTop` answer
*relative to the parent*, but a corpus row places its box in the root's
coordinates, so the generator carries each parent's position down the tree — skip
that and any fixture whose ancestors aren't at the origin asks for a box no
absolute position can satisfy. And Yoga doesn't start where CSS starts: its
template styles every `div` with `display: flex`, `position: relative`,
`flex-direction: column`, `box-sizing: border-box`, `flex-shrink: 0` and
`align-content: flex-start`, so those are values a row has to *state* rather than
ones a browser would infer.

## The score

**510 of 522 ported fixtures match** — 98% of what could be ported, 94% of Yoga's
whole suite. The tier prints a per-suite table on every run, so `ctest -R yoga` is
the check and this file is only the record.

*Recorded at* `0.2.0`, commit `1de38b7`, on a Windows desktop with MSYS2 UCRT64 GCC.
A score with no version beside it is a number that can outlive the code that printed
it, so step 3 of [`releases.md`](releases.md) re-runs the tier on a release build and
updates the line above it.

Of Yoga's 543 fixtures, 522 can be expressed in `cf_style` at all. The other 21
need a feature the struct has no field for, and they are counted rather than
quietly dropped, so the denominator stays the whole suite:

| fixtures skipped | because they need |
|---|---|
| 12 | a measure callback, so the answer depends on font metrics |
| 9 | `display: contents` |

## The 12 misses are one bug, and it isn't a layout bug

Chrome snapped every edge to a whole screen pixel before recording it.
`cf_layout` returns exact fractions and leaves rounding to whoever draws, because
rounding is a rendering decision and not a layout one.

The test allows half a pixel of difference, which a correct box can exceed purely
from rounding. A box 33.333 wide starting at 33.333 ends at 66.666; snap both edges
and you get 33 to 67 — a whole 34 wide, a pixel from the exact answer. So the
grade, not the maths, is what fails.

Ten of the twelve are Yoga's own `rounding` suite, which is that divergence stated
outright. The other two, a `gap` stretch and a `staticposition` centring case, hit
it the same way: 22 individual box assertions across 12 fixtures, every one of them
off by the grade rather than missing a rule. This stays a published difference.

## The known differences

`tests/conformance/yoga_cases.c` names the twelve fixtures the corpus is known
not to match. A fixture outside the list that starts to miss fails `ctest`, and one
the list still carries that starts to pass fails too — so a fix can no longer cancel
a break, which an aggregate score alone would hide.

Beside the names, the same program holds one floor per category: 64 of 64
`aligncontent`, 46 of 46 `boxsizing`, 3 of 13 `rounding`. Identity-pinning catches a
single fixture moving; the floors catch the corpus being re-sorted or a suite losing
ground in a way a reviewer reading one file's diff would never see, and they make a
new suite in the corpus an edit to this file rather than a passing test run.

Closing a gap means deleting that fixture's name from the list, raising its suite's
floor, the published score above, and — where it changes what could be ported at all
— the skip table, all in one commit.
