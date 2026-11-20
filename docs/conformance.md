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
output of its `gentest` driver, which loads each HTML fixture in headless Chrome
at device-scale-factor 1 and records what the browser laid out, expressed as
engine calls in and border boxes out. The fixtures themselves are plain CSS
flexbox, and `gentest/test-template.html` styles every `div` with the defaults
Yoga's own API starts from - `display: flex`, `flex-direction: column`,
`box-sizing: border-box`, `flex-shrink: 0`, `align-content: flex-start` - so
those are the values a ported row states rather than the ones CSS would infer.

Nothing at test time reads Yoga or a browser: `tools/gentest.py` turns the
recorded calls into `cf_style` rows and the corpus is checked-in C data. The
generated header is reproducible - regenerating it and running `clang-format`
replays the same bytes.

## What is ported

Chrome's number is only a target when `cf_style` can state the request. 485 of
543 fixtures can; the rest are counted by what they need, so the denominator
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

277 of the 485 ported fixtures match, 57% of what was ported and 51% of the
whole corpus. Measured at `7df2820`. The table is the tier's own output, so a
run is the check and this page is the record of it.

The grade is half a pixel. Chrome snapped every fixture edge to the device
grid; `cf_layout` returns exact fractions and leaves rounding to whoever
paints, so a correct box can sit at most half a pixel from a snapped edge. A
finer grade would score that divergence instead of the rules.

| suite | ported | match | miss |
|-------|--------|-------|------|
| absoluteposition | 33 | 15 | 18 |
| aligncontent | 64 | 37 | 27 |
| alignitems | 29 | 11 | 18 |
| alignself | 5 | 4 | 1 |
| androidnewsfeed | 1 | 0 | 1 |
| auto | 5 | 5 | 0 |
| border | 5 | 5 | 0 |
| boxsizing | 23 | 15 | 8 |
| dimension | 2 | 2 | 0 |
| flex | 10 | 9 | 1 |
| flexbasisfitcontent | 5 | 2 | 3 |
| flexdirection | 53 | 25 | 28 |
| flexwrap | 23 | 5 | 18 |
| gap | 32 | 24 | 8 |
| justifycontent | 28 | 21 | 7 |
| margin | 34 | 29 | 5 |
| minmaxdimension | 24 | 23 | 1 |
| padding | 7 | 7 | 0 |
| percentage | 24 | 16 | 8 |
| rounding | 13 | 2 | 11 |
| sizeoverflow | 3 | 3 | 0 |
| staticposition | 62 | 17 | 45 |
| **total** | **485** | **277** | **208** |

444 node assertions fail across those 208 cases. Only 8 of them differ from
Chrome by two pixels or less everywhere; 197 differ by more than eight. The
grade is not what is missing - the rules are.

## What the misses need

- **`position: relative` insets.** A static box offsets by its own `inset`, and
  a positioned ancestor becomes the containing block its absolute children
  measure against. Nothing in `src/layout/` reads `style->inset` except
  `absolute.c`, which reads it for absolute children only. This is most of the
  `staticposition` suite (45 of 62) and part of `flexdirection` and
  `percentage`.
- **Where an absolute child's static position comes from.** With no insets to
  place it, the child sits at the container's content-box start; Chrome applies
  the container's `justify-content` and `align-items` to it, and measures
  percentages against the *padding* box, which is what CSS defines as the
  containing block for an absolutely positioned descendant. Most of
  `absoluteposition` (18 of 33) and the `*_inner_*` cases in `flexdirection`.
- **Line packing when the container wraps.** Multiple lines have to be sized,
  then distributed by `align-content`, and `stretch` has to give the leftover
  cross space to the lines. `flexwrap` misses 18 of 23 and `aligncontent` 27 of
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

`CONFORMANCE_FLOOR` in `tests/conformance/yoga_cases.c` is 277 and only moves
up. The tier prints every mismatch and asserts the floor rather than a verdict,
so a fixture that regresses fails `ctest` while the ones that have never passed
stay visible here instead of being deleted to make the run green. Closing a gap
means raising the floor, the table and the score in one diff, with a failing
fixture named in the commit as the thing that was fixed.
