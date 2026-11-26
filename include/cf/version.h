#ifndef CF_VERSION_H
#define CF_VERSION_H

/*
 * The version the library answers with, and the only place it is written down.
 * CMake parses these macros before project() so a package cannot carry a
 * number a caller cannot test, and a release tag is checked against them.
 *
 * What a bump may contain is the contract in docs/versioning.md: MAJOR for
 * anything a caller has to edit for, MINOR for appends only, PATCH for fixes
 * that change nothing a program can observe.
 */
#define CF_VERSION_MAJOR 0
#define CF_VERSION_MINOR 2
#define CF_VERSION_PATCH 0

/* The three numbers as one string. Kept beside them rather than assembled by
 * the preprocessor so the literal a caller prints is the literal a release
 * names; cmake/cform_version.cmake fails a build where the two disagree. */
#define CF_VERSION_STRING "0.2.0"

/*
 * CF_VERSION_STRING as a call rather than a macro, for whoever needs the
 * number at run time: a diagnostics line, an installed package proving which
 * version it found. The storage is static and belongs to the library; a caller
 * that keeps the bytes copies them.
 */
const char *cf_version_string(void);

#endif
