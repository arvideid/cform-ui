#include <stdio.h>
#include <string.h>

#include "cf/base.h"
#include "cform_test.h"

static void interning_equal_text_returns_the_same_name(void) {
    cf_interner interner;
    CF_CHECK(cf_interner_init(&interner, 8), "interner initializes");

    const char *first = "layout.fill.splits-remaining-space";
    char copy[] = "layout.fill.splits-remaining-space";

    const cf_name a = cf_intern_text(&interner, first);
    const cf_name b = cf_intern_text(&interner, copy);

    CF_CHECK(a != CF_NO_NAME, "the first intern lands");
    CF_CHECK(a == b, "a different buffer with the same bytes is the same name");

    cf_interner_free(&interner);
}

static void different_text_never_shares_a_name(void) {
    cf_interner interner;
    CF_CHECK(cf_interner_init(&interner, 8), "interner initializes");

    const cf_name button = cf_intern_text(&interner, "button");
    const cf_name label = cf_intern_text(&interner, "label");
    const cf_name prefixed = cf_intern_text(&interner, "xbutton");

    CF_CHECK(button != label && button != prefixed && label != prefixed, "three distinct names");
    CF_NEAR(cf_interned(&interner, label).length, 5);

    cf_interner_free(&interner);
}

static void names_match_by_value_after_the_caller_rebuilds_its_buffer(void) {
    cf_interner interner;
    CF_CHECK(cf_interner_init(&interner, 8), "interner initializes");

    static char built[16];
    memcpy(built, "gallery", 8);
    const cf_name first = cf_intern(&interner, cf_string_of(built));

    /* The caller reuses the same allocation for the same text, the way a
     * rebuilt label string does every frame. */
    memcpy(built, "gallery", 8);
    const cf_name again = cf_intern(&interner, cf_string_of(built));

    CF_CHECK(first == again, "rebuilding into the same buffer still hits by value");

    memcpy(built, "galley", 7);
    const cf_name other = cf_intern(&interner, cf_string_of(built));
    CF_CHECK(other != first, "a one-character difference is a different name");

    cf_interner_free(&interner);
}

static void interned_text_is_owned_by_the_table(void) {
    cf_interner interner;
    CF_CHECK(cf_interner_init(&interner, 8), "interner initializes");

    char source[8] = "theme";
    const cf_name name = cf_intern(&interner, cf_string_of(source));

    memset(source, 'X', sizeof source);

    const cf_string stored = cf_interned(&interner, name);
    CF_CHECK(stored.length == 5, "the copied length survives");
    CF_CHECK(memcmp(stored.chars, "theme", 5) == 0, "the table's bytes are its own");
    CF_CHECK(stored.chars != (const char *)source, "no borrowed pointer is kept");

    cf_interner_free(&interner);
}

static void names_survive_the_table_doubling(void) {
    cf_interner interner;
    CF_CHECK(cf_interner_init(&interner, 4), "interner starts with four slots");

    cf_name names[64];
    char text[24];
    for (size_t i = 0; i < 64; ++i) {
        snprintf(text, sizeof text, "slot-%zu", i);
        names[i] = cf_intern_text(&interner, text);
        CF_CHECK(names[i] != CF_NO_NAME, "each intern lands through the doublings");
    }

    CF_CHECK(interner.capacity >= 64, "the table grew by doubling, not by one");

    for (size_t i = 0; i < 64; ++i) {
        snprintf(text, sizeof text, "slot-%zu", i);
        CF_CHECK(cf_intern_text(&interner, text) == names[i], "an old text still maps to its name");
    }

    cf_interner_free(&interner);
}

static void cf_string_of_reads_a_nul_terminated_buffer(void) {
    const cf_string empty = cf_string_of(NULL);
    CF_CHECK(empty.chars == NULL && empty.length == 0u, "NULL text is an empty string");

    const cf_string text = cf_string_of("abcd");
    CF_NEAR(text.length, 4);

    const cf_string substring = (cf_string){.chars = "abcdef" + 2, .length = 2};
    CF_CHECK(memcmp(substring.chars, "cd", 2) == 0, "a length-carrying slice needs no terminator");
}

int main(void) {
    printf("intern\n");
    CF_RUN(interning_equal_text_returns_the_same_name);
    CF_RUN(different_text_never_shares_a_name);
    CF_RUN(names_match_by_value_after_the_caller_rebuilds_its_buffer);
    CF_RUN(interned_text_is_owned_by_the_table);
    CF_RUN(names_survive_the_table_doubling);
    CF_RUN(cf_string_of_reads_a_nul_terminated_buffer);
    return CF_REPORT();
}
