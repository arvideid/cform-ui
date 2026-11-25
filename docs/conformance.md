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

**473 of 485 ported fixtures match** — 97% of what could be ported, 87% of Yoga's
whole suite. Measured at `4bae6fc`. The tier prints a per-suite table on every
run, so `ctest -R yoga` is the check and this file is only the record.

Of Yoga's 543 fixtures, 485 can be expressed in `cf_style` at all. The other 58
need a feature the struct has no field for, and they are counted rather than
quietly dropped, so the denominator stays the whole suite:

| fixtures skipped | because they need |
|---|---|
| 23 | `box-sizing: content-box` |
| 15 | `display: none` |
| 12 | a measure callback, so the answer depends on font metrics |
| 5 | an `auto`-sizing keyword (`max-content`, `stretch`) |
| 2 | `overflow` |
| 1 | `aspect-ratio` |

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
not to match rather than asserting a floor. A fixture outside the list that
starts to miss fails `ctest`, and one the list still carries that starts to pass
fails too — so a fix can no longer cancel a break, which an aggregate score
alone would hide. The tier still prints the whole per-suite table and the score
on every run, so `ctest -R yoga` is the check and this file is the record.

Closing a gap means deleting that fixture's name from the list, raising the
published score above, and — where it changes what could be ported at all — the
skip table, all in one commit.
