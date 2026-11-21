#include "cf/backend.h"
#include "cform_test.h"

/*
 * The real font answers the same table the fakes in text_measure do - the
 * walk cannot tell a TrueType file from ten points per codepoint. It runs
 * only where the backend builds, and skips itself where no font file is
 * around to open.
 */
static void a_real_font_answers_the_table(void) {
    FILE *probe = fopen(CFORM_FONT_PATH, "rb");

    if (probe == NULL) {
        printf("      skipped: no font at " CFORM_FONT_PATH "\n");
        return;
    }
    fclose(probe);

    const cf_font font = cf_font_sdl(CFORM_FONT_PATH, 32.0f);
    CF_CHECK(font.ops != NULL, "the font opens");

    if (font.ops == NULL) {
        return;
    }

    const cf_text_metrics metrics = font.ops->metrics(font.handle);
    CF_CHECK(metrics.line_height > 0.0f, "a line advances by something");

    CF_CHECK(font.ops->has_glyph(font.handle, 'a'), "latin lowercase is drawn");
    CF_CHECK(font.ops->advance(font.handle, 'a', 0u) > 0.0f, "a glyph costs pen distance");

    const cf_text_stack stack = {.fonts = &font, .count = 1u};
    cf_text_content content = {
        .stack = stack,
        .text = cf_string_of("The quick brown fox"),
    };

    const cf_size natural = cf_text_measure(
        &content, (cf_available){.width = CF_AUTO, .height = CF_AUTO}, CF_INTRINSIC_MAX_CONTENT);
    CF_CHECK(natural.width > 0.0f && natural.height > 0.0f, "text measures to something");

    const cf_size packed = cf_text_measure(
        &content, (cf_available){.width = CF_AUTO, .height = CF_AUTO}, CF_INTRINSIC_MIN_CONTENT);
    CF_CHECK(packed.width > 0.0f && packed.width <= natural.width,
             "the narrowest the text packs is no wider than itself");

    cf_text_content sentence = {
        .stack = stack,
        .text = cf_string_of("one two three four five six seven eight"),
    };
    const cf_size wide =
        cf_text_measure(&sentence, (cf_available){.width = CF_PX(400.0f), .height = CF_AUTO},
                        CF_INTRINSIC_MAX_CONTENT);
    const cf_size narrow =
        cf_text_measure(&sentence, (cf_available){.width = CF_PX(80.0f), .height = CF_AUTO},
                        CF_INTRINSIC_MAX_CONTENT);
    CF_CHECK(narrow.height > wide.height, "a narrower box wraps into more lines");
}

int main(void) {
    printf("text sdl font\n");

    CF_RUN(a_real_font_answers_the_table);

    return CF_REPORT();
}
