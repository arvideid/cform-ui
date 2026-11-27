#include <float.h>

#include "cf/text.h"
#include "utf8.h"

/* The codepoints a walk reads as itself. */
#define SPACE 0x20u
#define HYPHEN 0x2Du
#define LINE_FEED 0x0Au
#define CARRIAGE_RETURN 0x0Du

static bool is_forced(uint32_t codepoint) {
    return codepoint == LINE_FEED || codepoint == CARRIAGE_RETURN;
}

/* A hyphen may end its line after itself, and so may an east-asian glyph:
 * CJK punctuation through unified ideographs, the compatibility ideographs,
 * the fullwidth forms. */
static bool breaks_after(uint32_t codepoint) {
    return codepoint == HYPHEN || (codepoint >= 0x2E80u && codepoint <= 0x9FFFu) ||
           (codepoint >= 0xF900u && codepoint <= 0xFAFFu) ||
           (codepoint >= 0xFF00u && codepoint <= 0xFF60u);
}

/* The walk carries the active font and the previous codepoint from glyph to
 * glyph; both lay down again where the font switches, since kerning across
 * a switch belongs to no font. */
typedef struct text_pen {
    const cf_font *font;
    uint32_t previous;
} text_pen;

/* The first font of the stack that draws the codepoint, or the primary when
 * none does: a glyph missing everywhere still advances the pen, by whatever
 * the primary says the hole costs. */
static const cf_font *font_of(const cf_text_stack *stack, uint32_t codepoint) {
    for (uint32_t at = 0u; at < stack->count; ++at) {
        if (stack->fonts[at].ops->has_glyph(stack->fonts[at].handle, codepoint)) {
            return &stack->fonts[at];
        }
    }
    return &stack->fonts[0];
}

static float step_pen(const cf_text_stack *stack, text_pen *pen, uint32_t codepoint) {
    const cf_font *font = font_of(stack, codepoint);
    const uint32_t previous = font == pen->font ? pen->previous : 0u;

    *pen = (text_pen){.font = font, .previous = codepoint};
    return font->ops->advance(font->handle, codepoint, previous);
}

/* One line's answer: where the following line starts, one past the last
 * byte that carries ink, and the width the line shows. */
typedef struct line_scan {
    uint32_t next;
    uint32_t end;
    float width;
} line_scan;

/*
 * One line's greedy fit from `start` within `max_width`: the line ends at
 * the last space that fit, after a hyphen or east-asian glyph that fit, at
 * the text's own break, or after the last glyph that fit when no break
 * offers itself. `next` is where the next line starts; `width` is the
 * distance the line shows - everything through its last glyph that carried
 * ink, so the spaces a break would leave behind are not counted and the
 * spaces between words are. A space wider than the line hangs instead of
 * wrapping: only a glyph that carries ink can overflow. A `nowrap` scan keeps
 * none of those choices but the text's own break.
 */
static line_scan scan_line(const cf_text_stack *stack, cf_string text, uint32_t start,
                           float max_width, bool nowrap) {
    float width = 0.0f;         /* everything the scan consumed, spaces included */
    float inked = 0.0f;         /* width through the last glyph that carried ink */
    uint32_t inked_end = start; /* one past the last byte that carried ink */
    uint32_t break_at = start;
    float break_width = 0.0f;
    uint32_t break_end = start;
    bool may_break = false;
    uint32_t at = start;
    uint32_t next = 0u;
    text_pen pen = {0};

    while (at < text.length) {
        const uint32_t codepoint = utf8_decode_at(text, at, &next);

        if (is_forced(codepoint)) {
            uint32_t after = next;
            if (codepoint == CARRIAGE_RETURN && next < text.length && text.chars[next] == '\n') {
                after++;
            }
            return (line_scan){.next = after, .end = inked_end, .width = inked};
        }

        if (codepoint == SPACE) {
            break_at = at;
            break_width = inked;
            break_end = inked_end;
            may_break = true;
            width += step_pen(stack, &pen, codepoint);
            at = next;
            continue;
        }

        const float advance = step_pen(stack, &pen, codepoint);

        if (!nowrap && width + advance > max_width) {
            if (may_break) {
                return (line_scan){.next = break_at, .end = break_end, .width = break_width};
            }
            if (inked > 0.0f) {
                return (line_scan){.next = at, .end = inked_end, .width = inked};
            }
            /* Nothing fits and nothing offers a break: the overflowing glyph
             * opens the line it is too wide for. */
            return (line_scan){.next = next, .end = next, .width = advance};
        }

        width += advance;
        inked = width;
        inked_end = next;
        if (breaks_after(codepoint)) {
            /* The breaking glyph rides on the line it ends, spaces inside
             * it included. */
            break_at = next;
            break_width = width;
            break_end = next;
            may_break = true;
        }
        at = next;
    }

    return (line_scan){.next = text.length, .end = inked_end, .width = inked};
}

/* The walk's last line redrawn to make room for the ellipsis: the glyphs
 * that fit beside it, then the ellipsis itself. */
static line_scan truncated_scan(const cf_text_content *content, uint32_t start, float max_width) {
    const cf_text_stack *stack = &content->stack;
    const cf_font *ellipsis = font_of(stack, CF_ELLIPSIS);
    const float ellipsis_width = ellipsis->ops->advance(ellipsis->handle, CF_ELLIPSIS, 0u);
    const float budget = max_width > ellipsis_width ? max_width - ellipsis_width : 0.0f;
    const line_scan scan = scan_line(stack, content->text, start, budget, false);
    return (line_scan){.next = scan.next, .end = scan.end, .width = scan.width + ellipsis_width};
}

/* The lines a walk lays out: how many remain, the widest of them, and
 * whether the cap closed the last one with the ellipsis. */
typedef struct text_extent {
    uint32_t lines;
    float widest;
    bool ellipsized;
} text_extent;

static text_extent walk_lines(const cf_text_content *content, float max_width, cf_text_line *out,
                              uint32_t capacity) {
    const cf_string text = content->text;
    const cf_text_stack *stack = &content->stack;
    const uint32_t limit = content->max_lines.present && content->max_lines.value < UINT32_MAX
                               ? content->max_lines.value
                               : UINT32_MAX;

    text_extent extent = {0u, 0.0f, false};
    float settled = 0.0f; /* the widest of the lines before the one pending */
    float pending = 0.0f; /* the last line's width, held until the walk knows what follows it */
    uint32_t offset = 0u;
    uint32_t last_start = 0u;

    while (offset < text.length) {
        /* The spaces a break left behind open no line. */
        uint32_t next = 0u;
        while (offset < text.length && utf8_decode_at(text, offset, &next) == SPACE) {
            offset = next;
        }
        if (offset >= text.length) {
            break;
        }

        if (extent.lines == limit) {
            /* The cap is reached and the text continues: the line just laid
             * out is the last one, and it closes with the ellipsis inside
             * its own width. A cap of no lines leaves nothing to close. */
            if (content->ellipsis && extent.lines > 0u) {
                const line_scan closed = truncated_scan(content, last_start, max_width);
                pending = closed.width;
                extent.ellipsized = true;

                if (out != NULL && extent.lines - 1u < capacity) {
                    out[extent.lines - 1u] =
                        (cf_text_line){.text = {text.chars + last_start, closed.end - last_start},
                                       .width = closed.width,
                                       .ellipsis = true};
                }
            }
            break;
        }

        last_start = offset;
        settled = settled > pending ? settled : pending;
        const line_scan scan = scan_line(stack, text, offset, max_width, content->nowrap);
        pending = scan.width;
        offset = scan.next;
        extent.lines++;

        if (out != NULL && extent.lines - 1u < capacity) {
            out[extent.lines - 1u] =
                (cf_text_line){.text = {text.chars + last_start, scan.end - last_start},
                               .width = scan.width,
                               .ellipsis = false};
        }
    }

    settled = settled > pending ? settled : pending;
    return (text_extent){.lines = extent.lines, .widest = settled, .ellipsized = extent.ellipsized};
}

/* The intrinsic minimum: the widest run between break opportunities, and
 * the count of runs - wrapped at that width, every run stands alone. */
static text_extent runs_of(const cf_text_stack *stack, cf_string text) {
    text_extent extent = {0u, 0.0f, false};
    float run = 0.0f;
    bool open = false;
    uint32_t at = 0u;
    uint32_t next = 0u;
    text_pen pen = {0};

    while (at < text.length) {
        const uint32_t codepoint = utf8_decode_at(text, at, &next);

        if (is_forced(codepoint)) {
            if (open) {
                extent.lines++;
                open = false;
            }
            if (run > extent.widest) {
                extent.widest = run;
            }
            run = 0.0f;
            pen = (text_pen){0};
            at = next;
            if (codepoint == CARRIAGE_RETURN && at < text.length && text.chars[at] == '\n') {
                ++at;
            }
            continue;
        }

        if (codepoint == SPACE) {
            if (open) {
                extent.lines++;
                open = false;
            }
            if (run > extent.widest) {
                extent.widest = run;
            }
            run = 0.0f;
            pen = (text_pen){0};
            at = next;
            continue;
        }

        run += step_pen(stack, &pen, codepoint);
        if (breaks_after(codepoint)) {
            /* The glyph ends the run it belongs to: a hyphen stays on the
             * line it closes, an east-asian glyph the same. */
            if (run > extent.widest) {
                extent.widest = run;
            }
            extent.lines++;
            run = 0.0f;
            pen = (text_pen){0};
        } else {
            open = true;
        }
        at = next;
    }

    if (run > extent.widest) {
        extent.widest = run;
    }
    if (open) {
        extent.lines++;
    }
    return extent;
}

/* A walk needs a font table whose every font answers all three questions. */
static bool stack_usable(const cf_text_stack *stack) {
    if (stack->fonts == NULL || stack->count == 0u) {
        return false;
    }

    for (uint32_t at = 0u; at < stack->count; ++at) {
        const cf_text_ops *ops = stack->fonts[at].ops;
        if (ops == NULL || ops->metrics == NULL || ops->advance == NULL || ops->has_glyph == NULL) {
            return false;
        }
    }

    return true;
}

cf_size cf_text_measure(void *user_data, cf_available available, cf_intrinsic kind) {
    const cf_text_content *content = user_data;

    if (content == NULL || !stack_usable(&content->stack) || content->text.chars == NULL ||
        content->text.length == 0u) {
        return (cf_size){0};
    }

    const cf_font *primary = &content->stack.fonts[0];
    const float line_height = primary->ops->metrics(primary->handle).line_height;

    if (kind == CF_INTRINSIC_MIN_CONTENT) {
        /* A walk that refuses to wrap has no tighter answer than the whole of
         * its longest line: it will never break to fit a narrower track. */
        if (content->nowrap) {
            const text_extent whole = walk_lines(content, FLT_MAX, NULL, 0u);
            return (cf_size){.width = whole.widest, .height = (float)whole.lines * line_height};
        }
        const text_extent runs = runs_of(&content->stack, content->text);
        return (cf_size){.width = runs.widest, .height = (float)runs.lines * line_height};
    }

    const float max_width = available.width.kind == CF_LENGTH_PX ? available.width.value : FLT_MAX;
    const text_extent extent = walk_lines(content, max_width, NULL, 0u);
    return (cf_size){.width = extent.widest, .height = (float)extent.lines * line_height};
}

/* The lines a definite width wraps the text into, as the measure counted
 * them: paint reads this to place exactly what the height above promised. */
uint32_t cf_text_lines(const cf_text_content *content, float max_width, cf_text_line *out,
                       uint32_t capacity) {
    if (content == NULL || !stack_usable(&content->stack) || content->text.chars == NULL ||
        content->text.length == 0u) {
        return 0u;
    }

    return walk_lines(content, max_width, out, capacity).lines;
}

/*
 * A laid line's own bytes and where they sit in the content. The line points
 * into the content's text rather than numbering itself, so the offsets an editor
 * keeps and the offsets a walk laid are the same numbers - and a line from
 * somewhere else is refused here rather than read as a huge offset.
 */
static bool line_bounds(const cf_text_content *content, const cf_text_line *line, uint32_t *first,
                        uint32_t *last) {
    if (content == NULL || line == NULL || !stack_usable(&content->stack) ||
        content->text.chars == NULL || line->text.chars == NULL ||
        line->text.chars < content->text.chars) {
        return false;
    }

    const size_t start = (size_t)(line->text.chars - content->text.chars);

    if (start > content->text.length) {
        return false;
    }

    *first = (uint32_t)start;
    *last = *first + line->text.length;

    return true;
}

/* The distance the pen travels across a line's first `at` bytes, with the same
 * font switching and kerning the walk that laid the line used. */
static float run_to(const cf_text_content *content, cf_string line, uint32_t at) {
    const cf_text_stack *stack = &content->stack;
    text_pen pen = {0};
    float x = 0.0f;
    uint32_t offset = 0u;

    while (offset < at) {
        uint32_t next = 0u;
        const uint32_t codepoint = utf8_decode_at(line, offset, &next);

        x += step_pen(stack, &pen, codepoint);
        offset = next;
    }

    return x;
}

/* The run a line is being cut into: the font it stands in, where its bytes began, how far the
 * runs before it moved the pen, and how wide it has come out. */
typedef struct open_run {
    const cf_font *font;
    uint32_t start;
    float x;
    float width;
} open_run;

static void flush_run(void (*visit)(void *, const cf_text_run *), void *user, cf_string bytes,
                      const open_run *run, uint32_t end, bool ellipsis) {
    visit(user, &(cf_text_run){.font = *run->font,
                               .text = {bytes.chars + run->start, end - run->start},
                               .x = run->x,
                               .width = run->width,
                               .ellipsis = ellipsis});
}

void cf_text_each_run(const cf_text_content *content, const cf_text_line *line,
                      void (*visit)(void *user_data, const cf_text_run *run), void *user_data) {
    uint32_t first = 0u;
    uint32_t last = 0u;

    if (visit == NULL || !line_bounds(content, line, &first, &last)) {
        return;
    }

    const cf_text_stack *stack = &content->stack;
    const cf_string bytes = {line->text.chars, last - first};
    open_run run = {0};
    text_pen pen = {0};
    uint32_t offset = 0u;

    while (offset < bytes.length) {
        uint32_t next = 0u;
        const uint32_t codepoint = utf8_decode_at(bytes, offset, &next);
        const cf_font *took = font_of(stack, codepoint);

        if (run.font == NULL) {
            run = (open_run){.font = took, .start = offset, .x = 0.0f, .width = 0.0f};
        } else if (took != run.font) {
            /* The stack changed hands, so the stretch the old font drew closes where the
             * new one's first letter stands. */
            flush_run(visit, user_data, bytes, &run, offset, false);
            run = (open_run){.font = took, .start = offset, .x = run.x + run.width, .width = 0.0f};
        }

        /* The same pen the walk that laid the line carried: a run opens without the kerning
         * of a letter its font never drew, which is what makes the widths add up. */
        const uint32_t previous = took == pen.font ? pen.previous : 0u;
        run.width += took->ops->advance(took->handle, codepoint, previous);
        pen = (text_pen){.font = took, .previous = codepoint};
        offset = next;
    }

    if (run.font == NULL) {
        return; /* the line held no codepoint for a font to draw */
    }

    if (!line->ellipsis) {
        flush_run(visit, user_data, bytes, &run, offset, false);
        return;
    }

    const cf_font *closing = font_of(stack, CF_ELLIPSIS);
    const float closing_width = closing->ops->advance(closing->handle, CF_ELLIPSIS, 0u);

    if (closing == run.font) {
        /* One font drew the letters and draws the sign that closes them, so the cap belongs
         * to the stretch it follows: one command carries both, exactly as a stack that never
         * falls back does. */
        run.width += closing_width;
        flush_run(visit, user_data, bytes, &run, offset, true);
        return;
    }

    /* The closing sign's own font is a third party to the line: it stands in a run of no
     * bytes, because the glyph is not in the text and the room it measures is. */
    flush_run(visit, user_data, bytes, &run, offset, false);
    visit(user_data, &(cf_text_run){.font = *closing,
                                    .text = {bytes.chars + offset, 0u},
                                    .x = run.x + run.width,
                                    .width = closing_width,
                                    .ellipsis = true});
}

float cf_text_caret_x(const cf_text_content *content, const cf_text_line *line, uint32_t offset) {
    uint32_t first = 0u;
    uint32_t last = 0u;

    if (!line_bounds(content, line, &first, &last)) {
        return 0.0f;
    }

    const uint32_t clamped = offset < first ? first : (offset > last ? last : offset);

    return run_to(content, (cf_string){line->text.chars, last - first}, clamped - first);
}

uint32_t cf_text_offset_at(const cf_text_content *content, const cf_text_line *line, float x) {
    uint32_t first = 0u;
    uint32_t last = 0u;

    if (!line_bounds(content, line, &first, &last)) {
        return 0u;
    }

    const cf_string run = {line->text.chars, last - first};
    const cf_text_stack *stack = &content->stack;
    text_pen pen = {0};
    float pen_x = 0.0f;
    uint32_t offset = 0u;

    while (offset < run.length) {
        uint32_t next = 0u;
        const uint32_t codepoint = utf8_decode_at(run, offset, &next);
        const float advance = step_pen(stack, &pen, codepoint);

        /* Half a glyph belongs to the side of it the pointer came from, which is
         * what lets a click on a wide letter land inside it instead of after it. */
        if (pen_x + advance * 0.5f > x) {
            return first + offset;
        }

        pen_x += advance;
        offset = next;
    }

    return first + run.length;
}

uint32_t cf_text_line_of(const cf_text_content *content, const cf_text_line *lines, uint32_t count,
                         uint32_t offset) {
    if (content == NULL || lines == NULL || count == 0u) {
        return 0u;
    }

    for (uint32_t i = 0u; i < count; ++i) {
        uint32_t first = 0u;
        uint32_t last = 0u;

        if (line_bounds(content, &lines[i], &first, &last) && last >= offset) {
            return i;
        }
    }

    /* Past every line the walk laid: the last one holds the caret at its end. */
    return count - 1u;
}

cf_point cf_text_scroll(const cf_text_content *content, const cf_text_line *lines, uint32_t count,
                        cf_size window) {
    if (content == NULL || lines == NULL || count == 0u || !content->caret.present ||
        !stack_usable(&content->stack)) {
        return (cf_point){0.0f, 0.0f};
    }

    const cf_font *primary = &content->stack.fonts[0];
    const float line_height = primary->ops->metrics(primary->handle).line_height;
    const uint32_t row = cf_text_line_of(content, lines, count, content->caret.value);
    const float caret_x = cf_text_caret_x(content, &lines[row], content->caret.value);
    const float caret_bottom = (float)row * line_height + line_height;
    cf_point slide = {0.0f, 0.0f};

    /* Only the overflow is paid for: a caret the window already holds moves
     * nothing, so a field that fits its value reads as a label does. */
    if (window.width > 0.0f && caret_x > window.width) {
        slide.x = caret_x - window.width;
    }

    if (window.height > 0.0f && caret_bottom > window.height) {
        slide.y = caret_bottom - window.height;
    }

    return slide;
}
