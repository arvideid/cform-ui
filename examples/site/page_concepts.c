/*
 * Concepts: the words this repository uses for its own parts, and what happens
 * between two frames.
 *
 * Both halves are transcriptions of README, which is where they are canonical: the
 * table is README's glossary and the four steps are its pipeline diagram. That is
 * deliberate - a page that invents a second definition of `state` is a page that
 * will be wrong the next time the word is tightened - and it is why this page links
 * back to the file the words came from instead of arguing with it.
 */
#include "pages.h"

/* The words, and what each one means here rather than in general. */
static const char *const words[] = {
    "node", "ctx",         "arena",  "state", "backend", "ABI",
    "unit", "conformance", "golden", "bench", "point",   "frame",
};

static const char *const meanings[] = {
    "One box in the tree. An integer index rather than a pointer, so a handle that "
    "went out of date fails safely instead of reading freed memory.",
    "Everything one frame is allowed to keep: its memory, its named state, its "
    "events, its theme. It is the only thing handed to your build function.",
    "Memory handed out in one block and thrown away in one block, so a frame cannot "
    "leak and no allocation in a frame needs an owner.",
    "What a named box keeps across frames - a click count, a scroll offset, a typed "
    "buffer. It lives in the context and not in your node, because your node is "
    "rebuilt every frame.",
    "The only part that knows about windows, GPUs and pixels. Everything above it "
    "counts logical points and asks the backend how many of them fit.",
    "The exact list of functions two separately compiled pieces of code agree on, "
    "and the number of the agreement. For this library that is the cf_backend "
    "struct, versioned apart from the API it sits under.",
    "A check of one layer, run with no window and no display, in milliseconds.",
    "Checking the flexbox against somebody else's answer rather than against our "
    "own: the fixtures come from Yoga and the boxes from a real browser.",
    "One frame's whole command list saved as text, so a change that moves a shape "
    "shows up as a diff a person reviews instead of a screenshot nobody compared.",
    "Timing a large screen so a slowdown cannot arrive unnoticed. Reported, never "
    "asserted on a shared runner.",
    "A logical unit of layout. A point divided by the surface's density is the pixel "
    "a backend draws, and nothing above the backend does that division.",
    "One pass of describe, measure, emit and draw. The shell runs one only when "
    "something moved, which is why an idle page costs nothing.",
};

/* The four steps, each with the call that performs it. */
static const char *const describe[] = {
    "static cf_node build(cf_ctx *ctx) {",
    "    cf_style row = cf_style_default();",
    "    row.direction = CF_DIRECTION_ROW;",
    "    row.column_gap = CF_PT(8.0f);",
    "",
    "    return cf_row(ctx, row, children, count);",
    "}",
};

static const char *const measure[] = {
    "cf_layout(scratch, tree, root, available, origin, rects);",
    "",
    "/* cf_tree is parallel arrays indexed by slot: styles, measures,",
    "   child_start, child_slots. Pure arithmetic - it allocates from",
    "   the scratch arena and reads no clock, no window and no global. */",
};

static const char *const emit[] = {
    "cf_paint(scratch, tree, rects, root, viewport, &list);",
    "",
    "/* Fills, borders, text and images, back to front, each one",
    "   carrying the rect it may not be seen outside. Not",
    "   incremental: an unchanged screen emits the same list. */",
};

static const char *const draw[] = {
    "backend.submit(user_data, list.commands, list.count);",
    "",
    "/* A backend turns the list onto a surface, reports clicks and",
    "   keystrokes back, answers how big the surface is and puts the",
    "   cursor under the box the frame asked for. */",
};

cf_node site_page_concepts(cf_ctx *ctx, const site_view *view) {
    const cf_node steps[] = {
        site_step(ctx, view, "01", "Describe", "describe",
                  "Your build function answers a tree of boxes, each carrying a "
                  "cf_style: the same vocabulary as CSS - width, margin, flex_grow, "
                  "justify_content, gap - stated as fields of a struct rather than as "
                  "a string parsed at load time.",
                  describe, SITE_COUNT(describe)),
        site_step(ctx, view, "02", "Measure", "measure",
                  "The layout pass is arithmetic. Give it the tree and the room "
                  "available and it writes one position and size per box, implementing "
                  "flexbox as the specification defines it plus a useful subset of "
                  "grid - and asking a measure function for the size of text it will "
                  "never draw itself.",
                  measure, SITE_COUNT(measure)),
        site_step(ctx, view, "03", "Emit", "emit",
                  "The paint walk turns the laid-out tree into a list of shapes: "
                  "ordered, each one clipped to the box it must stay inside. That is "
                  "how a scrolled list stays in its window without a single "
                  "coordinate being transformed by hand.",
                  emit, SITE_COUNT(emit)),
        site_step(ctx, view, "04", "Draw", "draw",
                  "A backend puts the list on a real surface and reports what happened "
                  "back. One over SDL3 ships here, a headless recorder exists for the "
                  "tests, and writing your own means filling in a struct of ten "
                  "functions whose version the shell checks before the first frame.",
                  draw, SITE_COUNT(draw)),
    };

    const cf_node reading[] = {
        site_link(ctx, view, "to-readme-words", "The README glossary", SITE_BLOB "README.md"),
        site_link(ctx, view, "to-backend-docs", "Writing a backend",
                  SITE_BLOB "docs/backend-abi.md"),
    };

    const cf_node blocks[] = {
        site_section(ctx, view, "The words", "words",
                     (cf_node[]){
                         site_p(ctx, view,
                                "Names follow the code: functions and types are lowercase with a "
                                "cf_ prefix, macros and enum values are uppercase with the same "
                                "one, and no type carries a _t suffix because the prefix already "
                                "says where the name lives."),
                         site_pairs(ctx, view, words, meanings, SITE_COUNT(words), WORD_WIDTH),
                     },
                     2u),
        site_section(ctx, view, "How a frame happens", "a-frame",
                     (cf_node[]){
                         site_p(ctx, view,
                                "Four steps, and each one can be used on its own: the layout "
                                "engine is a target a program links by itself, the paint pass is "
                                "a function a host can call into its own renderer, and the shell "
                                "that runs all four is a loop you are allowed to not use."),
                         steps[0],
                         steps[1],
                         steps[2],
                         steps[3],
                         site_note(ctx, view,
                                   "The shell asks for a frame only when something moved: an "
                                   "event arrived, the surface changed size, or a page said out "
                                   "loud that its own state is mid-motion. A window nobody is "
                                   "touching draws nothing and costs nothing, which is the "
                                   "invariant the whole design is arranged around."),
                     },
                     6u),
        site_strip(ctx, reading, SITE_COUNT(reading)),
    };

    return site_page(ctx, view, SITE_GROUP_DOCS, "Concepts", "concepts",
                     "The vocabulary the source uses, and the four things that happen "
                     "between one frame and the next.",
                     blocks, SITE_COUNT(blocks));
}
