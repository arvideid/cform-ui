# The version, read from the header that states it.
#
# include/cf/version.h is the single source: CMake includes this file before
# project() and passes what it parses, so the package version, the header a
# caller compares against and the tag a release is cut from cannot disagree by
# being edited in two places. A generated version.h.in could not do this - it
# would put the numbers in the build tree, where the source tree and the build
# tree are free to hold different ones.
#
# Three ways to run it:
#   include(cmake/cform_version.cmake)     sets CFORM_VERSION and friends
#   cmake -P cmake/cform_version.cmake     prints the two numbers as assignments,
#                                          which is what a CI step appends to
#                                          its environment
#   cmake -P cmake/cform_version.cmake --check
#                                          additionally fails a tree that no
#                                          longer agrees with the header, and
#                                          compares CFORM_EXPECTED_VERSION when
#                                          a release passes a tag in - the
#                                          definition belongs before -P, which
#                                          is where cmake reads -D
#
# The backend ABI number is read in the same pass because a release names both
# numbers and neither should be typed by hand.

get_filename_component(CFORM_VERSION_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(CFORM_VERSION_HEADER "${CFORM_VERSION_ROOT}/include/cf/version.h")
set(CFORM_ABI_HEADER "${CFORM_VERSION_ROOT}/include/cf/backend.h")

if(NOT EXISTS "${CFORM_VERSION_HEADER}")
    message(FATAL_ERROR "cmake/cform_version.cmake: ${CFORM_VERSION_HEADER} is missing, "
        "so no version can be read.")
endif()

set(cform_check OFF)
if(CMAKE_SCRIPT_MODE_FILE)
    # Scanned rather than read by position: a -D belongs before -P for cmake to
    # take it as a definition, which shifts where the script's own arguments
    # start.
    math(EXPR cform_last "${CMAKE_ARGC} - 1")
    foreach(cform_arg RANGE 1 ${cform_last})
        if(CMAKE_ARGV${cform_arg} STREQUAL "--check")
            set(cform_check ON)
        endif()
    endforeach()
endif()

file(READ "${CFORM_VERSION_HEADER}" cform_version_text)
file(READ "${CFORM_ABI_HEADER}" cform_backend_text)

foreach(cform_component MAJOR MINOR PATCH)
    string(REGEX MATCH "#define[ \t]+CF_VERSION_${cform_component}[ \t]+([0-9]+)"
        cform_matched "${cform_version_text}")
    if(NOT cform_matched)
        message(FATAL_ERROR "cmake/cform_version.cmake: ${CFORM_VERSION_HEADER} defines no "
            "CF_VERSION_${cform_component}, so the version cannot be read from it.")
    endif()
    set(CFORM_VERSION_${cform_component} "${CMAKE_MATCH_1}")
endforeach()

set(CFORM_VERSION "${CFORM_VERSION_MAJOR}.${CFORM_VERSION_MINOR}.${CFORM_VERSION_PATCH}")

string(REGEX MATCH "#define[ \t]+CF_BACKEND_ABI_VERSION[ \t]+([0-9]+)" cform_matched
    "${cform_backend_text}")
if(NOT cform_matched)
    message(FATAL_ERROR "cmake/cform_version.cmake: ${CFORM_ABI_HEADER} defines no "
        "CF_BACKEND_ABI_VERSION, so the backend ABI cannot be read from it.")
endif()
set(CFORM_BACKEND_ABI_VERSION "${CMAKE_MATCH_1}")

if(NOT cform_check)
    if(CMAKE_SCRIPT_MODE_FILE)
        # Plain message rather than STATUS: these two lines go into a CI step's
        # environment file, where a leading "-- " is a variable nobody sets.
        message("CFORM_VERSION=${CFORM_VERSION}")
        message("CFORM_BACKEND_ABI_VERSION=${CFORM_BACKEND_ABI_VERSION}")
    endif()
    return()
endif()

# The literal string is the number a caller prints; where it drifts from the
# macros, the run-time answer and the package answer differ.
string(REGEX MATCH "#define[ \t]+CF_VERSION_STRING[ \t]+\"([^\"]*)\"" cform_matched
    "${cform_version_text}")
if(NOT cform_matched)
    message(FATAL_ERROR "cmake/cform_version.cmake: ${CFORM_VERSION_HEADER} defines no "
        "CF_VERSION_STRING.")
endif()
if(NOT CMAKE_MATCH_1 STREQUAL CFORM_VERSION)
    message(FATAL_ERROR "cmake/cform_version.cmake: CF_VERSION_STRING is \"${CMAKE_MATCH_1}\" "
        "but the macros beside it say ${CFORM_VERSION}.")
endif()

# The header stays the single source only while project() reads it. A literal
# version reintroduced there is a second number waiting to disagree.
file(READ "${CFORM_VERSION_ROOT}/CMakeLists.txt" cform_root_text)
set(cform_project_version "VERSION \${CFORM_VERSION}")
string(FIND "${cform_root_text}" "${cform_project_version}" cform_project_version_at)
if(cform_project_version_at EQUAL -1)
    message(FATAL_ERROR "cmake/cform_version.cmake: CMakeLists.txt no longer passes "
        "\${CFORM_VERSION} to project(), so include/cf/version.h has stopped being the "
        "single source.")
endif()

if(DEFINED CFORM_EXPECTED_VERSION AND NOT CFORM_EXPECTED_VERSION STREQUAL CFORM_VERSION)
    message(FATAL_ERROR "cmake/cform_version.cmake: ${CFORM_EXPECTED_VERSION} was asked for "
        "but include/cf/version.h says ${CFORM_VERSION}.")
endif()

message(STATUS "cform-ui ${CFORM_VERSION}: header, CF_VERSION_STRING and project() agree "
    "(backend ABI ${CFORM_BACKEND_ABI_VERSION})")
