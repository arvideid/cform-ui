# Flexbox conformance

Published numbers from `tests/conformance/yoga_cases.c`. The gate this tier
exists to meet is at least 95% of the flexbox suite, so the score lives here
and moves in the diff that moves it: run the tier, compare against the table,
change the table in the same pull request.

```sh
ctest --preset dev              # unit and conformance tiers
python tools/gentest.py         # rewrite the corpus from .vendor/yoga
clang-format -i tests/conformance/yoga_cases_data.h
```

## Where the expectations come from

[facebook/yoga](https://github.com/facebook/yoga) at `503bec3` (MIT, Copyright
(c) Facebook, Inc. and its affiliates) ships `tests/generated/*.cpp`: the
output of its `gentest` driver, which loads each HTML fixture in headless
Chrome at device-scale-factor 1 and writes a test from what it measured - the
calls that rebuild the tree in Yoga's API, and per node the box the browser
gave it as an offset from its parent element. The fixtures themselves are plain
CSS flexbox, and `gentest/test-template.html` styles every `div` with the
defaults Yoga's own API starts from - `display: flex`, `position: relative`,
`flex-direction: column`, `box-sizing: border-box`, `flex-shrink: 0`,
`align-content: flex-start` - so those are the values a ported row states
rather than the ones CSS would infer.

Nothing at test time reads Yoga or a browser: `tools/gentest.py` turns the
recorded calls into `cf_style` rows and the corpus is checked-in C data. The
generated header is reproducible - regenerating it and running `clang-format`
replays the same bytes.

`YGNodeLayoutGetLeft` and `YGNodeLayoutGetTop` answer relative to the parent
node, and the recorded offsets are too, while a corpus row places its box in
the root's space - so the generator carries the pair down the tree before
writing a row. Without that step a fixture whose ancestors are not at the
origin asks for a box no absolute rect can hold: the parent-relative `-50`
where the CSS answer is `50`.

## What is ported

The reference's number is only a target when `cf_style` can state the request.
485 of 543 fixtures can; the rest are counted by what they need, so the
denominator stays the whole suite rather than the convenient part of it.

| skipped | needs |
|---------|-------|
| 23 | `box-sizing: content-box`, which `cf_style` has no field for |
| 15 | `display: none` |
| 12 | a measure callback, so the number depends on text metrics |
| 2 | `overflow` |
| 1 | `aspect-ratio` |
| 5 | an intrinsic or stretch keyword (`max-content`, `stretch`) |

## Score

300 of the 485 ported fixtures match, 62% of what was ported and 55% of the
whole corpus. Measured at `8d066b6`. The table is the tier's own output, so a
run is the check and this page is the record of it.

The grade is half a pixel. The reference snapped every fixture edge to the
device grid before it was recorded; `cf_layout` returns exact fractions and
leaves rounding to whoever paints, so a correct box can sit at most half a
pixel from a snapped edge. A finer grade would score that divergence instead of
the rules.

| suite | ported | match | miss |
|-------|--------|-------|------|
| absoluteposition | 33 | 17 | 16 |
| aligncontent | 64 | 50 | 14 |
| alignitems | 29 | 11 | 18 |
| alignself | 5 | 4 | 1 |
| androidnewsfeed | 1 | 1 | 0 |
| auto | 5 | 5 | 0 |
| border | 5 | 5 | 0 |
| boxsizing | 23 | 15 | 8 |
| dimension | 2 | 2 | 0 |
| flex | 10 | 9 | 1 |
| flexbasisfitcontent | 5 | 4 | 1 |
| flexdirection | 53 | 25 | 28 |
| flexwrap | 23 | 5 | 18 |
| gap | 32 | 24 | 8 |
| justifycontent | 28 | 22 | 6 |
| margin | 34 | 29 | 5 |
| minmaxdimension | 24 | 23 | 1 |
| padding | 7 | 7 | 0 |
| percentage | 24 | 18 | 6 |
| rounding | 13 | 2 | 11 |
| sizeoverflow | 3 | 3 | 0 |
| staticposition | 62 | 19 | 43 |
| **total** | **485** | **300** | **185** |

382 node assertions fail across those 185 cases. Only 12 of them differ from
the reference by two pixels or less everywhere; 169 differ by more than eight.
The grade is not what is missing - the rules are.

## What the misses need

- **`position: relative` insets.** A relative box moves by its own `inset` and
  carries its subtree along. Nothing outside `absolute.c` reads `style->inset`,
  and `cf_position` has no word for relative at all: the generator maps
  `YGPositionTypeRelative` onto static, which is right while the insets are
  zero and wrong wherever they are not. Most of `staticposition` (43 of 62)
  and part of `percentage`.
- **Which box an absolute child measures against.** CSS makes the padding box
  of the nearest *positioned* ancestor the containing block; the engine makes
  every direct parent one, which `cf_position`'s comment states as a
  simplification. That is the `*_relative_to_positioned_ancestor` family and
  most of the remaining `absoluteposition` misses (16 of 33). The same rule
  covers the static position an inset-less absolute child lands at, where the
  reference applies the container's `justify-content` and `align-items` to it.
- **Line packing when the container wraps.** Multiple lines have to be sized,
  then distributed by `align-content`, and `stretch` has to give the leftover
  cross space to the lines. `flexwrap` misses 18 of 23 and `aligncontent` 14 of
  64.
- **`align-items: baseline`** and the self variant, which need a baseline the
  engine can only get from a measure callback. 13 cases.
- **Reverse directions and their edges.** `row-reverse` and `column-reverse`
  flip which edge a margin, border or padding offsets from, and 28 of
  `flexdirection` disagree by more than a pixel.
- **Edge rounding.** Rounding a box means rounding both its edges, so its size
  can land a whole pixel from the exact one - `33.333` wide starting at `33.333`
  becomes `34` when the far edge snaps to `67`. That is more than the grade
  forgives, and it is why 11 of the 13 `rounding` cases still miss: the snap is
  a renderer decision that `cf_layout` deliberately does not make, so this gap
  stays a published divergence rather than a rule to implement.

## The floor

`CONFORMANCE_FLOOR` in `tests/conformance/yoga_cases.c` is 300 and only moves
up. The tier prints every mismatch and asserts the floor rather than a verdict,
so a fixture that regresses fails `ctest` while the ones that have never passed
stay visible here instead of being deleted to make the run green. Closing a gap
means raising the floor, the table and the score in one diff, with a failing
fixture named in the commit as the thing that was fixed.
