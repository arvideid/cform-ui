#include <stdio.h>
#include <string.h>

#include "cf/version.h"
#include "cform_test.h"

static void the_string_is_the_three_numbers(void) {
    char expected[32];

    /* Rendered from the macros rather than compared to CF_VERSION_STRING, which
     * would be the string checking itself against itself. */
    snprintf(expected, sizeof(expected), "%d.%d.%d", CF_VERSION_MAJOR, CF_VERSION_MINOR,
             CF_VERSION_PATCH);

    CF_CHECK(strcmp(CF_VERSION_STRING, expected) == 0,
             "CF_VERSION_STRING carries the three macros beside it");
    CF_CHECK(strcmp(cf_version_string(), expected) == 0, "the call answers the same number");
}

static void the_call_answers_the_same_storage_every_time(void) {
    const char *first = cf_version_string();
    const char *second = cf_version_string();

    CF_CHECK(first == second, "a static string, not one built per call");
    CF_CHECK(first[0] != '\0', "the number is stated");
}

int main(void) {
    printf("version\n");
    CF_RUN(the_string_is_the_three_numbers);
    CF_RUN(the_call_answers_the_same_storage_every_time);
    return CF_REPORT();
}
