# Flexbox conformance

Published numbers from `tests/conformance/yoga_cases.c`. The gate this tier exists
to meet is at least 95% of the flexbox suite, so the score lives here and moves in
the commit that moves it.

```sh
ctest --preset dev              # unit, conformance and golden tiers
python tools/gentest.py         # rewrite the corpus from .vendor/yoga
clang-format -i tests/conformance/yoga_cases_data.h
```

## Where the expectations come from

[facebook/yoga](https://github.com/facebook/yoga) at `503bec3` (MIT, Copyright (c)
Facebook, Inc. and its affiliates) ships `tests/generated/*.cpp`: the output of its
`gentest` driver, which loads each HTML fixture in headless Chrome at
device-scale-factor 1 and records the calls that rebuild the tree in Yoga's API
beside the box the browser measured per node. The fixtures are plain CSS flexbox,
and the template styles every `div` with the defaults Yoga's own API starts from —
`display: flex`, `position: relative`, `flex-direction: column`,
`box-sizing: border-box`, `flex-shrink: 0`, `align-content: flex-start` — so those
are the values a ported row states rather than the ones CSS would infer.

Nothing at test time reads Yoga or a browser: `tools/gentest.py` turns the recorded
calls into `cf_style` rows, the corpus is checked-in C data, and regenerating plus
running `clang-format` replays the same bytes. Because
`YGNodeLayoutGetLeft`/`GetTop` answer relative to the parent while a corpus row
places its box in the root's space, the generator carries the pair down the tree
before writing a row — without it, a fixture whose ancestors are not at the origin
asks for a box no absolute rect can hold.

## What is ported

The reference's number is only a target when `cf_style` can state the request. 485
of 543 fixtures can; the rest are counted by what they need, so the denominator
stays the whole suite rather than the convenient part of it.

| skipped | needs |
|---------|-------|
| 23 | `box-sizing: content-box`, which `cf_style` has no field for |
| 15 | `display: none` |
| 12 | a measure callback, so the number depends on text metrics |
| 2 | `overflow` |
| 1 | `aspect-ratio` |
| 5 | an intrinsic or stretch keyword (`max-content`, `stretch`) |

## Score

473 of the 485 ported fixtures match, 97% of what was ported and 87% of the whole
corpus. Measured at `4bae6fc`. The table is the tier's own output, so a run is the
check and this page is the record of it.

| suite | ported | match | miss |
|-------|--------|-------|------|
| absoluteposition | 33 | 33 | 0 |
| aligncontent | 64 | 64 | 0 |
| alignitems | 29 | 29 | 0 |
| alignself | 5 | 5 | 0 |
| androidnewsfeed | 1 | 1 | 0 |
| auto | 5 | 5 | 0 |
| border | 5 | 5 | 0 |
| boxsizing | 23 | 23 | 0 |
| dimension | 2 | 2 | 0 |
| flex | 10 | 10 | 0 |
| flexbasisfitcontent | 5 | 5 | 0 |
| flexdirection | 53 | 53 | 0 |
| flexwrap | 23 | 23 | 0 |
| gap | 32 | 31 | 1 |
| justifycontent | 28 | 28 | 0 |
| margin | 34 | 34 | 0 |
| minmaxdimension | 24 | 24 | 0 |
| padding | 7 | 7 | 0 |
| percentage | 24 | 24 | 0 |
| rounding | 13 | 3 | 10 |
| sizeoverflow | 3 | 3 | 0 |
| staticposition | 62 | 61 | 1 |
| **total** | **485** | **473** | **12** |

## What the misses need

One class, not twelve. The reference snapped every recorded edge to the device grid
before it was written down; `cf_layout` returns exact fractions and leaves rounding
to whoever paints, so the grade is half a pixel and a correct box can sit that far
from a snapped edge. Rounding a box means rounding both edges, so its size can land
a whole pixel from the exact one — `33.333` wide starting at `33.333` becomes `34`
when the far edge snaps to `67` — which is more than the grade forgives. The 10
`rounding` cases are that divergence stated outright, and the surviving `gap`
stretch and `staticposition` centre cases meet it the same way: 22 node assertions
across 12 cases, every one of them the grade rather than a missing rule. Snapping is
a renderer decision `cf_layout` deliberately does not make, so this stays a
published divergence.

## The floor

`CONFORMANCE_FLOOR` in `tests/conformance/yoga_cases.c` is 473 and only moves up.
The tier prints every mismatch and asserts the floor rather than a verdict, so a
fixture that regresses fails `ctest` while the ones that have never passed stay
visible here instead of being deleted to make a run green. Closing a gap means
raising the floor, the table and the score in one commit, naming the fixture that
was fixed.
