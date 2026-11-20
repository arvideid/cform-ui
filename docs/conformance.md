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

341 of the 485 ported fixtures match, 70% of what was ported and 63% of the
whole corpus. Measured at `f6a8d76`. The table is the tier's own output, so a
run is the check and this page is the record of it.

The grade is half a pixel. The reference snapped every fixture edge to the
device grid before it was recorded; `cf_layout` returns exact fractions and
leaves rounding to whoever paints, so a correct box can sit at most half a
pixel from a snapped edge. A finer grade would score that divergence instead of
the rules.

| suite | ported | match | miss |
|-------|--------|-------|------|
| absoluteposition | 33 | 18 | 15 |
| aligncontent | 64 | 50 | 14 |
| alignitems | 29 | 11 | 18 |
| alignself | 5 | 4 | 1 |
| androidnewsfeed | 1 | 1 | 0 |
| auto | 5 | 5 | 0 |
| border | 5 | 5 | 0 |
| boxsizing | 23 | 16 | 7 |
| dimension | 2 | 2 | 0 |
| flex | 10 | 9 | 1 |
| flexbasisfitcontent | 5 | 4 | 1 |
| flexdirection | 53 | 32 | 21 |
| flexwrap | 23 | 5 | 18 |
| gap | 32 | 24 | 8 |
| justifycontent | 28 | 22 | 6 |
| margin | 34 | 29 | 5 |
| minmaxdimension | 24 | 23 | 1 |
| padding | 7 | 7 | 0 |
| percentage | 24 | 20 | 4 |
| rounding | 13 | 3 | 10 |
| sizeoverflow | 3 | 3 | 0 |
| staticposition | 62 | 48 | 14 |
| **total** | **485** | **341** | **144** |

315 node assertions fail across those 144 cases. Only 12 of them differ from
the reference by two pixels or less everywhere; 125 differ by more than eight.
The grade is not what is missing - the rules are.

## What the misses need

- **Line packing when the container wraps.** Multiple lines have to be sized,
  then distributed by `align-content`, and `stretch` has to give the leftover
  cross space to the lines. `flexwrap` misses 18 of 23 and `aligncontent` 14 of
  64; 8 of `gap` are `*_wrapping` cases where a percentage row gap resolves
  against a line box that has not been packed, so wrapping is worth about 41
  cases in total - the largest single rule left.
- **The static position a container's alignment gives.** 29
  cases, `staticposition`'s 14 amalgamations and 15 of `absoluteposition`, all
  asking for the same thing: the static position is not the content origin but
  wherever `justify-content` and `align-items` would have put the child had it
  stayed in the flow - `(container - child) / 2` for a centred container,
  `container - child - margin` for an end-packed one. The engine places it at
  the origin plus its own margin and stops there; the containing block itself
  is the nearest positioned ancestor's padding box now, which is what lifted
  `staticposition` from 23 to 48.
- **Reverse directions and their edges.** `row-reverse` and `column-reverse`
  flip which edge a margin, border or padding offsets from, and every one of
  `flexdirection`'s 21 remaining misses names a start or end edge of one of
  those three on a reversed container.
- **Negative free space.** When the children do not fit, `justify-content`
  has no slack to distribute and min, max and auto margins all resolve
  differently: 6 of `justifycontent`, 5 of `margin` (`margin_auto_*` with a
  child bigger than the parent), 7 of `alignitems` and 1 of `minmaxdimension`.
- **`align-items: baseline`** and the self variant, which need a baseline the
  engine can only get from a measure callback. 11 cases.
- **`box-sizing: content-box`.** `cf_style` has no field for it, so the 7
  `box_sizing_border_box_*` cases that mix the two arrive already misstated -
  as do the 23 fixtures the port skips for want of it.
- **Edge rounding.** Rounding a box means rounding both its edges, so its size
  can land a whole pixel from the exact one - `33.333` wide starting at `33.333`
  becomes `34` when the far edge snaps to `67`. That is more than the grade
  forgives, and it is why 10 of the 13 `rounding` cases still miss: the snap is
  a renderer decision that `cf_layout` deliberately does not make, so this gap
  stays a published divergence rather than a rule to implement.

## The floor

`CONFORMANCE_FLOOR` in `tests/conformance/yoga_cases.c` is 341 and only moves
up. The tier prints every mismatch and asserts the floor rather than a verdict,
so a fixture that regresses fails `ctest` while the ones that have never passed
stay visible here instead of being deleted to make the run green. Closing a gap
means raising the floor, the table and the score in one diff, with a failing
fixture named in the commit as the thing that was fixed.
