/*
 * One arena, from the base layer alone. A package that installs headers and
 * libraries is only useful if a program outside the source tree can find them
 * through either discovery route a downstream project picks - CMake's
 * find_package or pkg-config - and this is the smallest program that would fail
 * if either of them named a build-tree path or a configure-time prefix.
 */
#include "cf/base.h"

int main(void) {
    cf_arena arena;

    if (!cf_arena_init(&arena, 1024u)) {
        return 1;
    }

    cf_arena_free(&arena);
    return 0;
}
