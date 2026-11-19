cmake_minimum_required(VERSION 3.20)

project(cform-ui
    VERSION 0.1.0
    DESCRIPTION "A small declarative layout UI library for C, rendered with SDL3."
    LANGUAGES C
)

# ---------------------------------------------------------------------------
# Language defaults
# ---------------------------------------------------------------------------
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(CMAKE_C_STANDARD 99)
    set(CMAKE_C_STANDARD_REQUIRED ON)
    set(CMAKE_C_EXTENSIONS OFF)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

    # CMake is the single source of truth for compiler flags: clangd, the IDE
    # and CI all read the same compile_commands.json this produces.
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

# ---------------------------------------------------------------------------
# Dependencies: SDL3
#
# Use a locally installed / toolchain-provided SDL3 when available; otherwise
# fetch a pinned release so the project builds out of the box for everyone.
# ---------------------------------------------------------------------------
find_package(SDL3 CONFIG QUIET)

if(NOT SDL3_FOUND)
    message(STATUS "SDL3 not found locally - fetching a pinned release with FetchContent")

    include(FetchContent)

    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG        release-3.2.20
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(SDL3)
endif()

# ---------------------------------------------------------------------------
# Library
#
# Split into a renderer-agnostic core (`cform_core`) and an SDL3 backend
# (`cform`). Keeping the layout math free of SDL means it can be unit-tested
# headlessly - no window, no display, no SDL on the runtime path.
# ---------------------------------------------------------------------------
add_library(cform_core STATIC
    src/core/context.c
    src/core/frame.c
    src/core/layout.c
)

target_include_directories(cform_core
    PUBLIC  include
    PRIVATE src
)

add_library(cform STATIC
    src/renderer/sdl_renderer.c
)

target_include_directories(cform
    PRIVATE src
)

# Consumers that link `cform` get the core and SDL3 transitively.
target_link_libraries(cform
    PUBLIC cform_core SDL3::SDL3
)

# Warning policy for our own targets, applied per-target so it never leaks
# into SDL3 or other dependencies. Examples and tests opt in separately.
set(CFORM_WARNINGS "")
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    set(CFORM_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion
        -Wstrict-prototypes -Wmissing-prototypes)
elseif(MSVC)
    set(CFORM_WARNINGS /W4)
endif()

if(CFORM_WARNINGS)
    target_compile_options(cform_core PRIVATE ${CFORM_WARNINGS})
    target_compile_options(cform PRIVATE ${CFORM_WARNINGS})
endif()

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------
option(CFORM_BUILD_EXAMPLES "Build cform-ui example programs" ON)

if(CFORM_BUILD_EXAMPLES)
    add_executable(cform_demo example/demo.c)
    target_link_libraries(cform_demo PRIVATE cform)
endif()

# ---------------------------------------------------------------------------
# Tests (opt-in, and only when cform-ui is the top-level project)
# ---------------------------------------------------------------------------
option(CFORM_BUILD_TESTS "Build the cform-ui test suite" OFF)

if(CFORM_BUILD_TESTS AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    enable_testing()
    add_subdirectory(tests)
endif()
