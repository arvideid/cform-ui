/*
 * One arena, from the base layer alone, and the version the package answered
 * with. A package that installs headers and libraries is only useful if a
 * program outside the source tree can find them through either discovery route
 * a downstream project picks - CMake's find_package or pkg-config - and this is
 * the smallest program that would fail if either of them named a build-tree
 * path or a configure-time prefix.
 *
 * The version check is the other half of that proof: the number the installed
 * package file states, the number the installed header states and the number
 * the installed library returns have to be one number, or an adopter's bug
 * report is about a version nobody can name.
 */
#include <stdio.h>
#include <string.h>

#include "cf/base.h"
#include "cf/version.h"

int main(void) {
    cf_arena arena;

    if (!cf_arena_init(&arena, 1024u)) {
        return 1;
    }

    cf_arena_free(&arena);

    printf("cform-ui %s\n", cf_version_string());

#ifdef CFORM_UI_VERSION_AT_COMPILE
    if (strcmp(cf_version_string(), CFORM_UI_VERSION_AT_COMPILE) != 0) {
        printf("the package was found as %s, the library answers %s\n", CFORM_UI_VERSION_AT_COMPILE,
               cf_version_string());
        return 1;
    }
#endif

    /* The header's string and its three numbers are one fact, not two that
     * happen to be written down beside each other. */
    if (strcmp(cf_version_string(), CF_VERSION_STRING) != 0) {
        return 1;
    }

    return 0;
}
