#!/usr/bin/env python3
"""Check that the 1.0 layers only reach the neighbours their target links.

The CMake target graph already blocks a link across a layer boundary; this
catches the other half — an #include that assumes an edge the build does not
grant, in a source file or in the public header that names the layer, a
platform header above the backend, or a direct allocation in a layer that is
supposed to allocate from an arena.

The public headers are checked because every one of them sits in the same
directory on the include path: cf/layout.h reaching cf/text.h compiles and only
shows at link time, in whichever program happened to use the declaration.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "src"
INCLUDE = ROOT / "include" / "cf"

LAYER_OF_HEADER = {
    "cf.h": "umbrella",
    "types.h": "base",
    "base.h": "base",
    "style.h": "style",
    "layout.h": "layout",
    "text.h": "text",
    "paint.h": "paint",
    "event.h": "event",
    "node.h": "node",
    "build.h": "node",
    "theme.h": "theme",
    "widget.h": "widget",
    "backend.h": "backend",
    "app.h": "app",
}

ALLOWED = {
    "base": set(),
    "event": {"base"},
    "style": {"base"},
    "layout": {"base", "style"},
    "text": {"base"},
    "paint": {"base", "layout", "style", "text"},
    "node": {"base", "event", "layout", "paint", "style", "text"},
    "theme": {"base", "style"},
    "widget": {"base", "node", "style", "theme", "layout", "paint", "text"},
    "backend": {"base", "event", "paint", "text", "style", "layout"},
    "app": {"base", "widget", "node", "backend", "paint", "layout", "style", "text", "theme"},
}

# The umbrella names every header — that is its only job — so it reaches every
# layer. Nothing else in the tree is an umbrella.
ALLOWED["umbrella"] = set(ALLOWED)

CF_INCLUDE = re.compile(r'^\s*#\s*include\s+"cf/([a-z0-9_/]+\.h)"')
SDL_INCLUDE = re.compile(r'^\s*#\s*include\s+[<"]SDL3')
ALLOCATION = re.compile(r"\b(malloc|calloc|realloc|free)\s*\(")
PLATFORM_HEADERS = re.compile(
    r'^\s*#\s*include\s+[<"](windows\.h|unistd\.h|pthread\.h)')
MAGIC_ZERO = re.compile(r"\b0 (means|is)\b|\bmeans 0\b|\bunset\b")

errors = []


def layer_of(path):
    relative = path.relative_to(SRC)
    return relative.parts[0] if len(relative.parts) > 1 else None


def check_source(path):
    layer = layer_of(path)
    if layer is None:
        return

    may_allocate = layer == "base"
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        where = f"{path.relative_to(ROOT)}:{number}"

        header = CF_INCLUDE.match(line)
        if header:
            wanted = header.group(1)
            owner = LAYER_OF_HEADER.get(wanted)
            if owner is None:
                errors.append(
                    f"{where}: includes unknown public header cf/{wanted}")
            elif owner not in ALLOWED[layer] and owner != layer:
                errors.append(
                    f"{where}: layer '{layer}' includes cf/{wanted} owned by '{owner}'")

        if SDL_INCLUDE.match(line) and layer not in ("backend", "app"):
            errors.append(f"{where}: SDL belongs to src/backend or src/app")

        if PLATFORM_HEADERS.search(line) and layer not in ("backend", "app"):
            errors.append(
                f"{where}: platform header outside src/backend or src/app")

        if ALLOCATION.search(line) and not may_allocate:
            stripped = line.split("//")[0]
            if not stripped.lstrip().startswith(("*", "/*")):
                errors.append(
                    f"{where}: allocation outside src/base — use cf_alloc or cf_store")


def check_public_header(path):
    owner = LAYER_OF_HEADER.get(path.name)
    if owner is None:
        errors.append(f"{path.relative_to(ROOT)}: a public header no layer owns")
        return

    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        where = f"{path.relative_to(ROOT)}:{number}"

        header = CF_INCLUDE.match(line)
        if header:
            reached = header.group(1)
            neighbour = LAYER_OF_HEADER.get(reached)
            if neighbour is None:
                errors.append(
                    f"{where}: includes unknown public header cf/{reached}")
            elif neighbour != owner and neighbour not in ALLOWED[owner]:
                errors.append(
                    f"{where}: layer '{owner}' includes cf/{reached} owned by '{neighbour}'")

        if re.match(r'^\s*#\s*include\s+"(?!cf/)', line):
            errors.append(
                f"{where}: public header reaches into src/ internals")
        if SDL_INCLUDE.match(line):
            errors.append(f"{where}: public header includes SDL")
        if MAGIC_ZERO.search(line):
            errors.append(
                f"{where}: a raw value doing a sentinel's job — use a tagged value")


def main():
    for path in sorted(SRC.rglob("*.[ch]")):
        check_source(path)

    for path in sorted(INCLUDE.glob("*.h")):
        check_public_header(path)

    for error in errors:
        print(f"include graph: {error}", file=sys.stderr)

    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
