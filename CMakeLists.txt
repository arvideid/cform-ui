cmake_minimum_required(VERSION 3.20)

project(cform-ui
    VERSION 0.1.0
    DESCRIPTION "A small declarative layout UI library for C, rendered with SDL3."
    LANGUAGES C
)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(CMAKE_C_STANDARD 99)
    set(CMAKE_C_STANDARD_REQUIRED ON)
    set(CMAKE_C_EXTENSIONS OFF)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

    # CMake is the single source of truth for compiler flags: clangd, the IDE
    # and CI all read the same compile_commands.json this produces.
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

# Prefer a locally installed SDL3 for native builds; the web target always
# fetches, because a native SDL3 cannot link into a WebAssembly binary.
if(NOT EMSCRIPTEN)
    find_package(SDL3 CONFIG QUIET)
endif()

if(NOT SDL3_FOUND)
    message(STATUS "SDL3 not found locally - fetching a pinned release with FetchContent")

    include(FetchContent)

    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)

    if(EMSCRIPTEN)
        # The browser has no DLLs to load, so SDL must be linked statically.
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
    else()
        set(SDL_SHARED ON CACHE BOOL "" FORCE)
        set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    endif()

    FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG        release-3.2.20
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(SDL3)
endif()

# A renderer-agnostic core (`cform_core`) plus an SDL3 backend (`cform`) so the
# layout math can be unit-tested headlessly - no window, no display, no SDL.
add_library(cform_core STATIC
    src/core/context.c
    src/core/frame.c
    src/core/input.c
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

option(CFORM_BUILD_EXAMPLES "Build cform-ui example programs" ON)

if(CFORM_BUILD_EXAMPLES)
    add_executable(cform_demo example/demo.c)
    target_link_libraries(cform_demo PRIVATE cform)

    # WebAssembly: compile the SAME demo.c and layout into a browser-runnable
    # .html/.js/.wasm. ASYNCIFY lets the existing blocking ui_render() loop
    # yield to the browser instead of freezing the JS main thread.
    if(EMSCRIPTEN)
        target_link_options(cform_demo PRIVATE
            "-sUSE_WEBGL2=1"
            "-sASYNCIFY=1"
            "-sALLOW_MEMORY_GROWTH=1"
            "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/example/web/index.html"
        )
        # --shell-file requires the linker output to end in .html; emcc then
        # emits cform_demo.{html,js,wasm} side-by-side.
        set_target_properties(cform_demo PROPERTIES SUFFIX ".html")
    endif()
endif()

option(CFORM_BUILD_TESTS "Build the cform-ui test suite" OFF)

if(CFORM_BUILD_TESTS AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    enable_testing()
    add_subdirectory(tests)
endif()
