#!/usr/bin/env python3
"""The public C surface of cform-ui, printed as data.

Every declaration in include/cf/*.h becomes one sorted line, `kind|name|shape`,
so a change to what a caller can reach reads as a diff rather than as a re-read
of thirteen headers. tools/check_api.py classifies that diff against a committed
baseline and names the smallest version bump that would be legal.

Field and member order are in the shape, and an enum member carries its number,
because those two are the parts of a header a caller can compile against and
still break: a struct whose fields swapped keeps building and writes the wrong
thing, and an enum whose member was renumbered keeps its name and changes the
number a stored style carries.

The parse is deliberately narrow. It reads what the headers actually use -
typedef'd structs and enums, plain function declarations, object-like and
function-like macros - and stops with a message on a construct it cannot
classify rather than guessing and reporting a clean surface.
"""

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
INCLUDE = ROOT / "include" / "cf"

IDENT = r"[A-Za-z_][A-Za-z_0-9]*"
KEYWORDS = ("struct", "enum", "union")
# The operators an enum member or a macro value is written with here: bit ors,
# shifts, the odd arithmetic. Nothing else parses, and an expression outside
# this set is reported rather than guessed at.
OPERATORS = ("|", "^", "&", "<<", ">>", "+", "-", "*", "/")
TOKEN = re.compile(r"0[xX][0-9A-Fa-f]+|\d+|<<|>>|[|&^+\-*/~()]")


def normalize(text):
    """Whitespace squeezed to single spaces, so a reflow is not an API change."""
    return " ".join(text.split())


def readable(text):
    """Comments and line continuations gone: close to what the compiler sees."""
    text = text.replace("\\\n", " ")
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def block_class(header):
    """Which class keyword a block header states, or None for a function body.

    A brace block after a header naming no class is a definition: its body is
    not part of what a caller can reach, and it is followed by no semicolon.
    """
    words = re.findall(IDENT, normalize(header))

    for keyword in KEYWORDS:
        if keyword in words:
            return keyword

    return None


def statements(text):
    """The top-level pieces of a translation unit.

    Yields ("directive", line) for a preprocessor line, ("block", header, body,
    trailer) for `struct X { ... } X` and ("stmt", line) for anything else that
    ends at a semicolon outside braces.
    """
    found = []
    depth = 0
    buffer = []
    header = None
    body = []
    cursor = 0

    while cursor < len(text):
        char = text[cursor]

        if char == "#" and depth == 0 and not "".join(buffer).strip():
            end = text.find("\n", cursor)
            end = len(text) if end == -1 else end
            directive = text[cursor:end].strip()
            if directive:
                found.append(("directive", directive))
            buffer = []
            cursor = end
            continue

        if char == "{":
            if depth == 0:
                header = "".join(buffer).strip()
                body = []
            else:
                body.append(char)
            buffer = []
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                buffer = []
                block_body = "".join(body).strip()
                if block_class(header) is None:
                    found.append(("block", header, block_body, ""))
                else:
                    # The name after a closing brace arrives with the semicolon, so
                    # the block waits until it does rather than assuming a typedef.
                    found.append(("open", header, block_body))
            else:
                body.append(char)
        elif char == ";" and depth == 0:
            tail = "".join(buffer).strip()
            if found and found[-1][0] == "open":
                _, block_header, block_body = found.pop()
                found.append(("block", block_header, block_body, tail))
            elif tail:
                found.append(("stmt", tail))
            buffer = []
        elif depth == 0:
            buffer.append(char)
        else:
            body.append(char)

        cursor += 1

    for piece in found:
        if piece[0] == "open":
            raise ValueError("a brace block that never reaches its semicolon: " +
                             str(piece[1]))

    return found


def split_top(text, separator):
    """Split on a separator that is not nested in parentheses."""
    parts = []
    depth = 0
    buffer = []

    for char in text:
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1

        if char == separator and depth == 0:
            parts.append("".join(buffer))
            buffer = []
        else:
            buffer.append(char)

    parts.append("".join(buffer))
    return [normalize(part) for part in parts if normalize(part)]


def integers(expression):
    """The integer an expression states, over the operators this file names.

    A recursive walk rather than an evaluation of the text: the shapes worth
    accepting are literals and the arithmetic between them, and anything that is
    not those is an error a maintainer reads rather than a value made up.
    """
    tokens = TOKEN.findall(expression)
    if "".join(tokens) != normalize(expression).replace(" ", ""):
        raise ValueError("cannot read the value of: " + expression)

    position = 0

    def peek():
        return tokens[position] if position < len(tokens) else None

    def take(expected=None):
        nonlocal position
        token = tokens[position]
        if expected is not None and token != expected:
            raise ValueError("cannot read the value of: " + expression)
        position += 1
        return token

    def primary():
        token = take()
        if token == "(":
            inner = bit_or()
            take(")")
            return inner
        if token.startswith("0x") or token.startswith("0X"):
            return int(token, 16)
        return int(token)

    def unary():
        token = peek()
        if token == "-":
            take()
            return -unary()
        if token == "+":
            take()
            return unary()
        if token == "~":
            take()
            return ~unary()
        return primary()

    def multiply():
        left = unary()
        while peek() in ("*", "/"):
            operator = take()
            right = unary()
            left = left * right if operator == "*" else left // right
        return left

    def add():
        left = multiply()
        while peek() in ("+", "-"):
            operator = take()
            right = multiply()
            left = left + right if operator == "+" else left - right
        return left

    def shift():
        left = add()
        while peek() in ("<<", ">>"):
            operator = take()
            right = add()
            left = left << right if operator == "<<" else left >> right
        return left

    def bit_and():
        left = shift()
        while peek() == "&":
            take()
            left &= shift()
        return left

    def bit_xor():
        left = bit_and()
        while peek() == "^":
            take()
            left ^= bit_and()
        return left

    def bit_or():
        left = bit_xor()
        while peek() == "|":
            take()
            left |= bit_xor()
        return left

    result = bit_or()
    if position != len(tokens):
        raise ValueError("cannot read the value of: " + expression)
    return result


def value(expression, previous):
    """The number an enum member carries, with C's implicit counting."""
    expression = normalize(expression)

    if not expression:
        return 0 if previous is None else previous + 1

    return integers(expression)


def strip_names(text):
    """The trailing argument list of a declarator, with its names blanked."""
    match = re.search(r"\(([^()]*)\)\s*$", text, flags=re.S)
    if not match:
        return normalize(text)

    inner = match.group(1)
    if normalize(inner) in ("", "void"):
        return normalize(text)

    return normalize(text[: match.start()] + "(" +
                     ", ".join(parameter_type(part)
                               for part in split_top(inner, ",")) + ")")


def declarator_name(text):
    """The identifier a declarator binds, and the type left behind.

    `cf_style style` is the type `cf_style`; `bool (*ok)(void)` is the type
    `bool (*)(void)` bound to `ok`; `float row[4]` is the type `float [4]`.
    """
    text = normalize(text)

    pointer = re.search(r"\(\s*\*\s*(" + IDENT + r")\s*\)", text)
    if pointer:
        return pointer.group(1), strip_names(text[: pointer.start()] + "(*)" +
                                             text[pointer.end():])

    array = re.match(r"^(.*?)\b(" + IDENT + r")\s*(\[[^\]]*\])$", text)
    if array and normalize(array.group(1)):
        return array.group(2), normalize(array.group(1) + " " + array.group(3))

    plain = re.match(r"^(.*?)\b(" + IDENT + r")\s*$", text)
    if plain and normalize(plain.group(1)) and plain.group(1).strip() not in KEYWORDS:
        return plain.group(2), normalize(plain.group(1))

    return None, text


def parameter_type(text):
    """A parameter as its type: the part a caller's source depends on."""
    text = normalize(text)
    if text in ("void", "..."):
        return text

    name, stripped = declarator_name(text)
    return stripped if name else text


def signature(parameters):
    """`type (type, type)`: a parameter list with every name blanked."""
    if normalize(parameters) in ("", "void"):
        return "(void)"

    return "(" + ", ".join(parameter_type(part)
                           for part in split_top(parameters, ",")) + ")"


def type_members(kind, header, body, trailer):
    """The type's own name plus one line per member, in declaration order."""
    declared = normalize(header)

    for keyword in KEYWORDS:
        declared = declared.replace("typedef " + keyword, keyword)

    if declared not in KEYWORDS and not any(declared.startswith(k + " ")
                                            for k in KEYWORDS):
        raise ValueError(
            "a brace block that names no class: " + normalize(header))

    words = declared.split()
    tag = words[1] if len(words) > 1 else ""
    name = trailer if trailer else tag
    items = [(kind, name, "tag=" + tag if tag and tag != name else "")]

    if kind == "struct":
        for index, field in enumerate(split_top(body, ";")):
            member, type_name = declarator_name(field)
            items.append(("field", name + "." + (member or normalize(field)),
                          "%d %s" % (index, type_name)))
        return name, items

    number = None

    for member in split_top(body, ","):
        match = re.match(
            r"^(" + IDENT + r")\s*(?:=\s*(.*))?$", normalize(member))
        if not match:
            raise ValueError("an enum member that cannot be read: " + member)
        number = value(match.group(2) or "", number)
        items.append(("value", name + "." + match.group(1), str(number)))

    return name, items


def function_item(declared):
    """`return (type, type)` for a declaration or definition written as text."""
    call = re.match(r"^(.*?\b" + IDENT + r")\s*\((.*)\)$", normalize(declared),
                    flags=re.S)
    if not call:
        raise ValueError("a declaration this gate cannot read: " + declared)

    head = call.group(1)
    name = re.search(r"(" + IDENT + r")\s*$", head).group(1)
    return_type = normalize(head[: head.rfind(name)])

    return ("func", name, normalize(return_type + " " + name +
                                    signature(call.group(2))))


def merge(items):
    """One line per (kind, name), even where the preprocessor states several.

    `CF_DEPRECATED` is spelled differently per compiler and all the spellings sit
    in one header. Keeping them as one item with each alternative in it means the
    surface still changes when any branch changes, without a name appearing twice
    under two shapes - which no reader of a diff could tell from a real change.
    """
    shapes = {}

    for kind, name, shape in items:
        stated = shapes.setdefault((kind, name), [])
        if shape not in stated:
            stated.append(shape)

    def render(stated):
        if len(stated) == 1:
            return stated[0]
        return " || ".join(shape if shape else "<empty>" for shape in stated)

    return sorted((kind, name, render(stated)) for (kind, name), stated in shapes.items())


def surface_of(path):
    """Every public item one header states, as (kind, name, shape) triples."""
    items = []

    for piece in statements(readable(path.read_text(encoding="utf-8"))):
        if piece[0] == "directive":
            directive = normalize(piece[1])

            if re.match(r"^#\s*(?:if|ifdef|ifndef|else|elif|endif|pragma|error|"
                        r"include|undef)\b", directive):
                continue

            macro = re.match(r"^#\s*define\s+(" + IDENT + r")(\(.*?\))?(?:\s+(.*))?$",
                             directive)
            if not macro:
                raise ValueError(path.name + ": a directive that cannot be read: " +
                                 directive)
            kind = "macro-fn" if macro.group(2) else "macro"
            # The include guard is a name with no body beside it. It is not part of
            # what a caller may reach, and renaming it is not a break.
            if kind == "macro" and not macro.group(3) and macro.group(1).endswith("_H"):
                continue
            items.append((kind, macro.group(1),
                          normalize((macro.group(2) or "") + " " +
                                    normalize(macro.group(3) or ""))))
            continue

        if piece[0] == "block":
            _, header, body, trailer = piece
            declared = normalize(header)
            kind = block_class(declared)

            if kind is None:
                items.append(function_item(declared))
                continue

            if kind == "union":
                raise ValueError(path.name + ": a union in the public surface: " +
                                 declared)

            items.extend(type_members(kind, header, body, trailer)[1])
            continue

        declared = normalize(piece[1])

        if declared.startswith("typedef "):
            body = declared[len("typedef "):]
            name, stripped = declarator_name(body)

            if not name:
                raise ValueError(path.name + ": a typedef that names nothing: " +
                                 declared)
            if stripped.split()[0] in KEYWORDS:
                items.append(("handle", name, stripped))
            else:
                items.append(("typedef", name, stripped))
            continue

        call = re.match(r"^(.*?\b" + IDENT + r")\s*\((.*)\)$",
                        declared, flags=re.S)
        if not call:
            raise ValueError(path.name + ": a declaration this gate cannot read: " +
                             declared)

        items.append(function_item(declared))

    return merge(items)


def print_surface(paths):
    lines = []

    for path in paths:
        try:
            for kind, name, shape in surface_of(path):
                lines.append("%s|%s|%s" % (kind, name, shape))
        except ValueError as error:
            print("api surface: " + str(error), file=sys.stderr)
            return None

    return "\n".join(sorted(lines)) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("-o", "--output", help="write the surface to a file instead "
                                               "of stdout, which is how a baseline "
                                               "is refreshed")
    parser.add_argument("headers", nargs="*", help="the headers to read; every one "
                                                   "under include/cf by default")
    arguments = parser.parse_args()

    paths = [pathlib.Path(one) for one in arguments.headers] if arguments.headers \
        else sorted(INCLUDE.glob("*.h"))

    if not paths:
        print("api surface: no headers under " + str(INCLUDE), file=sys.stderr)
        return 1

    surface = print_surface(paths)
    if surface is None:
        return 1

    if arguments.output:
        destination = pathlib.Path(arguments.output)
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(surface, encoding="utf-8", newline="\n")
        print("api surface: %d items written to %s" %
              (len(surface.splitlines()), destination))
    else:
        sys.stdout.write(surface)

    return 0


if __name__ == "__main__":
    sys.exit(main())
