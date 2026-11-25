#!/usr/bin/env python3
"""Port Yoga's checked-in fixtures into a cf_layout corpus.

Yoga renders HTML fixtures in a reference browser and writes what it measured
into generated gtest sources: engine calls in, expected border boxes in
`ASSERT_FLOAT_EQ` out. Those sources are the fixture data, so this script reads
them and writes `tests/conformance/yoga_cases_data.h` - one row per node with
the style `cf_layout` needs and the box the reference engine reported. No
browser is involved at test time, and the corpus stays reviewable as data.

Yoga's initial values are not CSS's: a container runs along the column, packs
its lines with align-content flex-start and has no margin. Every row states
them, so the corpus says what the reference engine was configured with rather
than what CSS would have inferred.

`YGNodeLayoutGetLeft` and `GetTop` are offsets from the parent node, while a
corpus row places its box in the root's coordinate space, so the two numbers
are carried down the tree before a row is written.

A case is left out when it needs something `cf_style` cannot state - a measure
callback, an intrinsic or stretch keyword, a template shorthand - and the
summary counts why, so the published percentage is the ported set over the
whole set.

Usage: python tools/gentest.py [yoga-dir] [out-file]
       yoga-dir defaults to .vendor/yoga, out-file to
       tests/conformance/yoga_cases_data.h. Format the result with clang-format
       - the corpus is C, and the gate that reads it says so.
"""

import pathlib
import re
import sys

MAX_NODES = 17
YOGA_COMMIT = "503bec3"

# Yoga's Edge::Start is the writing-direction start, so with only the LTR pass
# ported it lands on the left. Horizontal, Vertical and All are fallbacks that
# a physical or writing-direction edge overrides, which is what resolve reads.
EDGE_FALLBACKS = {
    "left": ["start", "left", "horizontal", "all"],
    "right": ["end", "right", "horizontal", "all"],
    "top": ["top", "vertical", "all"],
    "bottom": ["bottom", "vertical", "all"],
}

SPREAD_EDGES = {
    "all": ["left", "right", "top", "bottom"],
    "horizontal": ["left", "right"],
    "vertical": ["top", "bottom"],
}

BOX_DEFAULTS = {
    "margin": "CF_PX(0.0f)",
    "padding": "CF_PX(0.0f)",
    "border": "CF_PX(0.0f)",
    "inset": "CF_AUTO",
}

SIZES = ("width", "height", "min_width", "max_width", "min_height",
         "max_height", "flex_basis")
BOX_FIELDS = ("margin", "padding", "border", "inset")

# Fields in the order a reader checks them: the box, its edges, the flex
# numbers, then the keywords.
FIELD_ORDER = SIZES + BOX_FIELDS + ("column_gap", "row_gap", "flex_grow",
                                    "flex_shrink", "position", "display",
                                    "box_sizing", "overflow_x", "overflow_y",
                                    "direction", "wrap", "justify_content",
                                    "align_items", "align_self", "align_content")

# What Yoga starts a node with that cf_style's zero does not give. A margin of
# 0 is stated in box_declaration, where the other boxes are written. Yoga's
# default position type offsets a node by its insets, which is what CSS writes
# as `position: relative`; cf_style's zero ignores them.
YOGA_DEFAULTS = {
    "direction": "CF_DIRECTION_COLUMN",
    "align_content": "CF_ALIGN_CONTENT_START",
    "position": "CF_POSITION_RELATIVE",
}

# Setters whose argument is named after the enum type, not the property.
ENUM_SETTERS = {
    "FlexDirection": ("direction", "YGFlexDirection"),
    "FlexWrap": ("wrap", "YGWrap"),
    "JustifyContent": ("justify_content", "YGJustify"),
    "AlignItems": ("align_items", "YGAlign"),
    "AlignSelf": ("align_self", "YGAlign"),
    "AlignContent": ("align_content", "YGAlign"),
    "PositionType": ("position", "YGPositionType"),
}

# A variant that needs no word in the row reads as None: either cf_style's zero
# already gives it, or YOGA_DEFAULTS states it for every node the fixture stays
# quiet about.
ENUM_VARIANTS = {
    "direction": {
        "Column": None,
        "Row": "CF_DIRECTION_ROW",
        "RowReverse": "CF_DIRECTION_ROW_REVERSE",
        "ColumnReverse": "CF_DIRECTION_COLUMN_REVERSE",
    },
    "wrap": {
        "NoWrap": None,
        "Wrap": "CF_WRAP",
        "WrapReverse": "CF_WRAP_REVERSE",
    },
    "justify_content": {
        "FlexStart": None,
        "Center": "CF_JUSTIFY_CENTER",
        "FlexEnd": "CF_JUSTIFY_END",
        "SpaceBetween": "CF_JUSTIFY_SPACE_BETWEEN",
        "SpaceAround": "CF_JUSTIFY_SPACE_AROUND",
        "SpaceEvenly": "CF_JUSTIFY_SPACE_EVENLY",
    },
    "align_items": {
        "FlexStart": "CF_ALIGN_START",
        "FlexEnd": "CF_ALIGN_END",
        "Center": "CF_ALIGN_CENTER",
        "Baseline": "CF_ALIGN_BASELINE",
        "Stretch": None,
    },
    "align_self": {
        "Auto": None,
        "FlexStart": "CF_ALIGN_START",
        "FlexEnd": "CF_ALIGN_END",
        "Center": "CF_ALIGN_CENTER",
        "Baseline": "CF_ALIGN_BASELINE",
        "Stretch": "CF_ALIGN_STRETCH",
    },
    "align_content": {
        "FlexStart": None,
        "FlexEnd": "CF_ALIGN_CONTENT_END",
        "Center": "CF_ALIGN_CONTENT_CENTER",
        "Stretch": "CF_ALIGN_CONTENT_STRETCH",
        "SpaceBetween": "CF_ALIGN_CONTENT_SPACE_BETWEEN",
        "SpaceAround": "CF_ALIGN_CONTENT_SPACE_AROUND",
        "SpaceEvenly": "CF_ALIGN_CONTENT_SPACE_EVENLY",
    },
    "position": {
        "Relative": None,
        "Static": "CF_POSITION_STATIC",
        "Absolute": "CF_POSITION_ABSOLUTE",
    },
}

NUMBER_SETTERS = {"FlexGrow": "flex_grow", "FlexShrink": "flex_shrink"}

EDGE_SETTERS = {"Margin": "margin", "Padding": "padding", "Border": "border",
                "Position": "inset"}

# What cf_style has no word for. A fixture that needs one is counted, not
# guessed at. `display: none` is not here because cf_display names it; the
# Display setter handler rejects `display: contents` on its own, since that
# feature promotes a box's children into its parent's flow and cf_style does
# not model it. `box-sizing` is here for the same reason: `content-box` is a
# cf_box_sizing value the BoxSizing setter handler reads, and `border-box` is
# what cf_style's zero already gives.
UNSUPPORTED_SUFFIXES = ("AspectRatio",
                        "MaxContent", "MinContent", "FitContent", "Stretch",
                        "TemplateRows", "TemplateColumns")

DECL = re.compile(r"YGNodeRef (\w+) = YGNodeNew")
INSERT = re.compile(r"YGNodeInsertChild\((\w+), (\w+), (\d+)\)")
CALCULATE = re.compile(
    r"YGNodeCalculateLayout\((\w+), ([^,]+), ([^,]+), (YGDirection\w+)\)")
ASSERTION = re.compile(
    r"ASSERT_FLOAT_EQ\(\s*([-\d.eE+]+)\s*,\s*"
    r"YGNodeLayoutGet(Left|Top|Width|Height)\((\w+)\)\s*\)")
STYLE_CALL = re.compile(r"YGNodeStyleSet(\w+)\(((?:[^()]|\([^()]*\))*)\)")
EDGE_SUFFIX = re.compile(r"(Percent|Auto)$")


def measure(text):
    """A literal as a float, or None when it holds no number.

    The generated sources keep the `f` suffix a float literal needs in C++ and
    write whole numbers without a fraction, so both spellings land here.
    """
    try:
        return float(str(text).rstrip("fF"))
    except ValueError:
        return None


def number(value):
    """A measurement as the C float literal a corpus row writes."""
    measured = measure(value)
    return None if measured is None else "{:.1f}f".format(measured)


def length(kind, text):
    if kind == "percent":
        return "CF_PERCENT({})".format(text)
    value = number(text)
    return "CF_PX({})".format(value) if value is not None else None


def resolve(box, physical, default):
    for edge in EDGE_FALLBACKS[physical]:
        if edge in box:
            return box[edge]
    return default


def targets(edge):
    return SPREAD_EDGES.get(edge, [edge])


def setter_stem(field):
    """`min_width` as the Yoga setter spells it: `MinWidth`."""
    return "".join(part.title() for part in field.split("_"))


class Fixture:
    """One TEST block: its nodes, the style each carries, the box each gets."""

    def __init__(self, suite, name, body):
        self.suite = suite
        self.name = name
        self.slots = {}
        self.styles = []
        self.parents = []
        self.expected = []
        self.available = None
        self.reason = None
        self._read(body)

    def _read(self, body):
        if "YGNodeSetMeasureFunc" in body:
            self._reject("needs a measure callback")

        for match in DECL.finditer(body):
            if match.group(1) in self.slots:
                continue
            self.slots[match.group(1)] = len(self.parents)
            self.styles.append({})
            self.parents.append(None)
            self.expected.append({})

        for parent, child, _ in INSERT.findall(body):
            if parent in self.slots and child in self.slots:
                self.parents[self.slots[child]] = self.slots[parent]
            else:
                self._reject("a node outside the fixture")

        for match in STYLE_CALL.finditer(body):
            self._style(match.group(1),
                        [p.strip() for p in match.group(2).split(",")])

        self._first_pass(body)
        self._absolute_boxes()

    def _reject(self, reason):
        if self.reason is None:
            self.reason = reason

    def _style(self, setter, arguments):
        for unsupported in UNSUPPORTED_SUFFIXES:
            if setter.endswith(unsupported):
                self._reject("needs " + setter)
                return

        if setter == "Flex":
            self._reject("needs the flex shorthand")
            return

        slot = self.slots.get(arguments[0]) if arguments else None
        if slot is None:
            self._reject("a node outside the fixture")
            return

        if len(arguments) == 1 and setter.endswith("Auto"):
            arguments = arguments + [""]
        if len(arguments) < 2:
            self._reject("a setter this script does not read: " + setter)
            return

        style = self.styles[slot]

        if setter == "Display":
            # Yoga's initial display is flex, which reads as cf_style's zero,
            # so only a hidden box earns a word. `display: contents` is a
            # different feature and is counted, not guessed at.
            variant = arguments[1]
            if variant == "YGDisplayNone":
                style["display"] = "CF_DISPLAY_NONE"
            elif variant != "YGDisplayFlex":
                self._reject("needs " + variant)
            return

        if setter == "BoxSizing":
            # Yoga's template styles every div as border-box, which is what
            # cf_style's zero already gives, so only content-box earns a word.
            variant = arguments[1]
            if variant == "YGBoxSizingContentBox":
                style["box_sizing"] = "CF_BOX_CONTENT"
            elif variant != "YGBoxSizingBorderBox":
                self._reject("needs " + variant)
            return

        if setter == "Overflow":
            # cf_style names both axes; Yoga only has one word. Any non-visible
            # value writes both axes so cf_style_clips_content reads true, and
            # visible is what cf_style's zero already gives.
            variant = arguments[1]
            if variant == "YGOverflowVisible":
                return
            if variant in ("YGOverflowHidden", "YGOverflowScroll",
                           "YGOverflowClip"):
                name = variant[len("YGOverflow"):].upper()
                value = "CF_OVERFLOW_" + name
                style["overflow_x"] = value
                style["overflow_y"] = value
            else:
                self._reject("needs " + variant)
            return

        if setter in NUMBER_SETTERS:
            value = number(arguments[1])
            if value is None:
                self._reject("a flex number that is not a number")
            else:
                style[NUMBER_SETTERS[setter]] = value
        elif setter in ENUM_SETTERS:
            self._enum(style, setter, arguments[1])
        elif setter.startswith("Gap"):
            self._gap(style, setter, arguments[1:])
        elif EDGE_SUFFIX.sub("", setter) in EDGE_SETTERS:
            self._edge(style, setter, arguments[1:])
        else:
            self._size(style, setter, arguments[1])

    def _value(self, setter, text):
        """The literal a setter carries, read through its name's suffix."""
        if setter.endswith("Auto"):
            return "CF_AUTO"
        if setter.endswith("Percent"):
            return length("percent", text)
        return length("px", text)

    def _size(self, style, setter, argument):
        for field in SIZES:
            stem = setter_stem(field)
            if setter not in (stem, stem + "Percent", stem + "Auto"):
                continue
            value = self._value(setter, argument)
            if value is None:
                self._reject("a size this script cannot read: " + setter)
            else:
                style[field] = value
            return

        self._reject("a setter this script does not read: " + setter)

    def _enum(self, style, setter, argument):
        field, prefix = ENUM_SETTERS[setter]
        variant = argument[len(prefix):]
        table = ENUM_VARIANTS[field]
        if variant not in table:
            self._reject("an enum this script does not read: " + argument)
        elif table[variant] is not None:
            style[field] = table[variant]

    def _gap(self, style, setter, arguments):
        value = self._value(setter, arguments[1])
        if value is None:
            self._reject("a gap this script cannot read: " + setter)
            return
        gutter = arguments[0].replace("YGGutter", "").lower()
        if gutter in ("column", "all"):
            style["column_gap"] = value
        if gutter in ("row", "all"):
            style["row_gap"] = value

    def _edge(self, style, setter, arguments):
        # An auto edge carries no literal: `SetMarginAuto(node, edge)` names
        # the edge and nothing else, and `_value` reads the suffix.
        text = arguments[1] if len(arguments) > 1 else ""
        value = self._value(setter, text)
        if value is None:
            self._reject("an edge this script cannot read: " + setter)
            return
        declared = style.setdefault(
            EDGE_SETTERS[EDGE_SUFFIX.sub("", setter)], {})
        edge = arguments[0].replace("YGEdge", "").lower()
        for physical in targets(edge):
            declared[physical] = value

    def _first_pass(self, body):
        passes = list(CALCULATE.finditer(body))
        if not passes:
            self._reject("no layout pass")
            return
        if len(passes) > 2:
            self._reject("more than one relayout")
            return
        if passes[0].group(4) != "YGDirectionLTR":
            self._reject("no LTR pass")
            return

        width = number(passes[0].group(2))
        height = number(passes[0].group(3))
        self.available = "AVAILABLE({}, {})".format(
            "CF_PX({})".format(width) if width else "CF_AUTO",
            "CF_PX({})".format(height) if height else "CF_AUTO")

        end = passes[1].start() if len(passes) > 1 else len(body)
        for value, kind, reference in ASSERTION.findall(
                body[passes[0].end():end]):
            if reference in self.slots:
                self.expected[self.slots[reference]][kind] = number(value)

    def _absolute_boxes(self):
        """Rewrite each parent-relative offset as a box in the root's space.

        A fixture whose ancestors sit at the origin already reads correctly,
        which is why the distinction survives unnoticed until a node two
        levels deep is placed by a margin at the level above it.
        """
        children = [[] for _ in self.parents]
        for slot, parent in enumerate(self.parents):
            if parent is not None:
                children[parent].append(slot)

        pending = [(self.slots["root"], 0.0, 0.0)]
        placed = set()
        while pending:
            slot, x, y = pending.pop()
            if slot in placed:
                continue
            placed.add(slot)

            left = measure(self.expected[slot].get("Left"))
            top = measure(self.expected[slot].get("Top"))
            if left is not None and top is not None:
                x, y = x + left, y + top
                self.expected[slot]["Left"] = number(x)
                self.expected[slot]["Top"] = number(y)

            pending.extend((child, x, y) for child in children[slot])


def box_declaration(field, declared):
    """One box as the row writes it, or None when it needs no word."""
    if declared is None:
        # Yoga's initial margin is 0, not the auto that absorbs leftover
        # space, so every row states it. Padding, border and inset read the
        # same whether stated or left at cf_style's zero.
        return ".margin = ZERO_BOX" if field == "margin" else None
    sides = ", ".join(
        resolve(declared, physical, BOX_DEFAULTS[field])
        for physical in ("left", "top", "right", "bottom"))
    return ".{} = SIDES({})".format(field, sides)


def render_style(style):
    """One node as the reference engine had it: its declarations, plus the
    initial values Yoga picks and cf_style's zero does not give."""
    declarations = []

    for field in FIELD_ORDER:
        if field in BOX_FIELDS:
            text = box_declaration(field, style.get(field))
        else:
            value = style.get(field, YOGA_DEFAULTS.get(field))
            text = None if value is None else ".{} = {}".format(field, value)
        if text:
            declarations.append(text)

    return "YOGA_STYLE({})".format(", ".join(declarations))


def render_rect(expected):
    values = [expected.get(kind)
              for kind in ("Left", "Top", "Width", "Height")]
    if any(value is None for value in values):
        return None
    return "RECT({})".format(", ".join(values))


def render(fixture):
    """The corpus row, or the reason the fixture cannot become one."""
    if fixture.reason:
        return None, fixture.reason
    if "root" not in fixture.slots:
        return None, "no root node"
    if len(fixture.slots) > MAX_NODES:
        return None, "more nodes than the corpus holds"

    rows = []
    for slot in range(len(fixture.slots)):
        rect = render_rect(fixture.expected[slot])
        if rect is None:
            return None, "a node without four expected numbers"
        if slot == fixture.slots["root"]:
            parent = "ROOT"
        elif fixture.parents[slot] is None:
            return None, "a node nobody inserts"
        else:
            parent = "{}u".format(fixture.parents[slot])
        rows.append("{{{style}, {parent}, {rect}}}".format(
            style=render_style(fixture.styles[slot]), parent=parent, rect=rect))

    return (
        "    {{.name = \"{suite} {name}\",\n"
        "     .available = {available},\n"
        "     .node_count = {count}u,\n"
        "     .nodes = {{\n{body}\n     }}}},\n"
    ).format(suite=fixture.suite, name=fixture.name, available=fixture.available,
             count=len(rows), body=",\n".join(rows)), None


def parse(path):
    text = path.read_text(encoding="utf-8")
    suite = re.sub(r"^(YG)|Test$", "", path.stem).lower()
    fixtures = []
    for block in re.split(r"\nTEST\(YogaTest, ", text)[1:]:
        name, _, body = block.partition(") {")
        fixtures.append(Fixture(suite, name.strip(), body.split("\n}\n")[0]))
    return fixtures


HEADER = """/*
 * Generated by tools/gentest from facebook/yoga {commit}, tests/generated:
 * Yoga's fixtures as one browser measured them. Regenerate with `python
 * tools/gentest.py`, then clang-format the result, rather than editing by
 * hand. Yoga is MIT and the numbers are its engine's, pixel-snapped in a way
 * cf_layout is not. docs/conformance.md publishes which fixtures could not be
 * ported and how the rest scored.
 *
 * A row states the style Yoga was configured with rather than the one CSS
 * would infer: the column direction, the flex-start lines and the zero margins
 * are Yoga's initial values, not the engine's.
 */

"""


def main():
    root = pathlib.Path(__file__).resolve().parent.parent
    vendor = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else \
        root / ".vendor" / "yoga"
    out = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else \
        root / "tests" / "conformance" / "yoga_cases_data.h"

    sources = sorted((vendor / "tests" / "generated").glob("*.cpp"))
    if not sources:
        print("gentest: no generated fixtures under {}".format(vendor),
              file=sys.stderr)
        return 1

    reasons = {}
    rows = []
    total = 0

    for path in sources:
        for fixture in parse(path):
            total += 1
            rendered, reason = render(fixture)
            if rendered is None:
                reasons[reason] = reasons.get(reason, 0) + 1
            else:
                rows.append(rendered)

    out.parent.mkdir(parents=True, exist_ok=True)
    # LF whatever the host: the committed corpus is LF, and a corpus that only
    # differs in line endings reads as drift.
    out.write_text(HEADER.format(commit=YOGA_COMMIT) + "".join(rows),
                   encoding="utf-8", newline="\n")

    print("gentest: {} fixtures, {} ported".format(total, len(rows)))
    for reason, count in sorted(reasons.items(),
                                key=lambda item: (-item[1], item[0])):
        print("   {:>4} skipped: {}".format(count, reason))

    return 0


if __name__ == "__main__":
    sys.exit(main())
