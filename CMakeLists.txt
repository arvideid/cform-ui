cmake_minimum_required(VERSION 3.25)

project(cform-ui
    VERSION 1.0.0
    DESCRIPTION "A retained-mode, data-oriented UI framework in C."
    LANGUAGES C
)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(CMAKE_C_STANDARD 17)
    set(CMAKE_C_STANDARD_REQUIRED ON)
    set(CMAKE_C_EXTENSIONS OFF)

    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

    # CMake is the single source of truth for compiler flags: clangd, the IDE
    # and CI all read the same compile_commands.json this produces.
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

# Warning policy for our own targets, applied per-target so it never leaks into
# a dependency. Examples and tests opt in separately.
set(CFORM_WARNINGS "")
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    set(CFORM_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion
        -Wstrict-prototypes -Wmissing-prototypes)
elseif(MSVC)
    set(CFORM_WARNINGS /W4)
endif()

# The layer graph: one target per layer, so a dependency can only be created
# where the graph allows it - the linker is the check. A layer target is added
# when the layer has code, not before.
add_library(cf_base STATIC
    src/base/arena.c
    src/base/hash.c
    src/base/intern.c
    src/base/log.c
    src/base/store.c
)

# cf_style is libc and cf_base only; it knows no node, no geometry, no backend.
add_library(cf_style STATIC
    src/style/style.c
)

target_link_libraries(cf_style PUBLIC cf_base)

# cf_theme is the design system over the style vocabulary: the neutral scale
# a build reads its surfaces from, and the per-state paint each widget family
# resolves by. It is data, and the layers above it are its only readers.
add_library(cf_theme STATIC
    src/theme/default.c
)

target_link_libraries(cf_theme PUBLIC cf_style)

# cf_layout reads styles and lengths only: no windowing, no text metrics, no
# backend. Content it cannot see arrives through a measure callback.
add_library(cf_layout STATIC
    src/layout/layout.c
    src/layout/content.c
    src/layout/absolute.c
    src/layout/flex.c
    src/layout/flex_item.c
    src/layout/flex_line.c
    src/layout/flex_align.c
    src/layout/flex_baseline.c
    src/layout/grid.c
    src/layout/grid_place.c
    src/layout/grid_track.c
)

target_link_libraries(cf_layout PUBLIC cf_style cf_base)

# cf_text turns borrowed text into measured extents: line breaking, min and
# max content, the ellipsis cap. It reads fonts through a table of pen
# distances and never rasterizes - a real font backend answers the same
# table, which is what keeps this layer headless.
add_library(cf_text STATIC
    src/text/measure.c
    src/text/utf8.c
)

target_link_libraries(cf_text PUBLIC cf_base)

# cf_paint turns placed boxes into the flat command stream backends read: no
# window, no device pixels, no backend - the stream is the whole contract. A
# text node's lines come off the same walk its measure answered from, so the
# stream carries text as a dependency of layout, not of any font.
add_library(cf_paint STATIC
    src/paint/emit.c
)

target_link_libraries(cf_paint PUBLIC cf_layout cf_text)

# cf_node reconciles each frame's declaration into retained slots: identity
# by depth and key, position for the keyless, and the view cf_layout reads.
# state.c holds the blocks a build asks for by name across frames, scroll.c
# the offsets a scroller keeps under its name, resolve.c the theme's per-state
# paint applied to the view the diff built. It is the first layer that owns
# memory across frames.
add_library(cf_node STATIC
    src/node/anim.c
    src/node/context.c
    src/node/diff.c
    src/node/input.c
    src/node/resolve.c
    src/node/scroll.c
    src/node/state.c
    src/node/tree.c
)

target_link_libraries(cf_node PUBLIC cf_layout)

# cf_widget fills declarations: each widget states the fields a build would
# otherwise spell by hand - a label's text and measure, a button's role and
# press handler, a scroller's offset and clip - out of the context's frame
# arena. Text, fonts and tones stay arguments, so the layer owns no loading
# and stays headless.
add_library(cf_widget STATIC
    src/widget/button.c
    src/widget/label.c
    src/widget/list.c
    src/widget/scroll.c
)

target_link_libraries(cf_widget PUBLIC cf_node cf_text)

# cf_backend_headless is the reference backend: it copies the submitted stream
# into its arena so tests read what a device would have drawn. It opens no
# window and touches no device pixels - the draw is everywhere else.
add_library(cf_backend_headless STATIC
    src/backend/headless/record.c
)

target_link_libraries(cf_backend_headless PUBLIC cf_paint)

# The SDL renderer backend is off by default so a headless configure never
# fetches or builds SDL; the machine that wants to see a frame turns it on.
option(CFORM_WITH_SDL_BACKEND "Build the SDL renderer backend" OFF)

if(CFORM_WITH_SDL_BACKEND)
    include(FetchContent)
    FetchContent_Declare(sdl3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.2.20
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(sdl3)

    # Text metrics come from the same family: the font file is read for its
    # numbers alone, and a glyph is priced, never rasterized, until the
    # widgets draw one.
    FetchContent_Declare(sdl3_ttf
        GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
        GIT_TAG release-3.2.2
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(sdl3_ttf)

    # The stream's first real draw path: it owns the window until the app shell
    # exists, and every layer above it stays window-free.
    add_library(cf_backend_sdl STATIC
        src/backend/sdl/gpu.c
        src/backend/sdl/font.c
    )

    target_link_libraries(cf_backend_sdl PUBLIC cf_paint cf_text SDL3::SDL3 SDL3_ttf::SDL3_ttf)
endif()

set(CFORM_LAYER_TARGETS cf_base cf_style cf_theme cf_layout cf_text cf_paint cf_node cf_widget cf_backend_headless)

if(CFORM_WITH_SDL_BACKEND)
    list(APPEND CFORM_LAYER_TARGETS cf_backend_sdl)
endif()

foreach(layer IN LISTS CFORM_LAYER_TARGETS)
    target_include_directories(${layer}
        PUBLIC include
        PRIVATE src
    )

    if(CFORM_WARNINGS)
        target_compile_options(${layer} PRIVATE ${CFORM_WARNINGS})
    endif()
endforeach()

# Address and undefined behaviour sanitizers over the layers and their tests.
# Off by default: the pair needs runtime libraries that neither MSVC nor a bare
# MinGW toolchain ships, and the toolchain switch is a build-time decision
# rather than a code one.
option(CFORM_WITH_SANITIZERS "Build the layers under ASan and UBSan" OFF)

set(CFORM_SANITIZERS "")
if(CFORM_WITH_SANITIZERS)
    set(CFORM_SANITIZERS -fsanitize=address,undefined -fno-omit-frame-pointer)

    # Asking for sanitizers and quietly getting an unsanitized build would
    # report clean memory use for a run that checked nothing.
    include(CheckCSourceCompiles)
    set(CFORM_REQUIRED_FLAGS_BACKUP "${CMAKE_REQUIRED_FLAGS}")
    list(JOIN CFORM_SANITIZERS " " CFORM_SANITIZER_FLAGS)
    set(CMAKE_REQUIRED_FLAGS "${CFORM_SANITIZER_FLAGS}")
    check_c_source_compiles("int main(void) { return 0; }" CFORM_SANITIZERS_USABLE)
    set(CMAKE_REQUIRED_FLAGS "${CFORM_REQUIRED_FLAGS_BACKUP}")

    if(NOT CFORM_SANITIZERS_USABLE)
        message(FATAL_ERROR
            "CFORM_WITH_SANITIZERS=ON, but the C compiler (${CMAKE_C_COMPILER_ID}) "
            "cannot link -fsanitize=address,undefined. MSVC has no equivalent pair; "
            "MinGW needs the sanitizer runtime packages installed. Reconfigure with "
            "-DCFORM_WITH_SANITIZERS=OFF, or use clang on Linux or macOS.")
    endif()

    foreach(layer IN LISTS CFORM_LAYER_TARGETS)
        target_compile_options(${layer} PRIVATE ${CFORM_SANITIZERS})
        target_link_options(${layer} PUBLIC ${CFORM_SANITIZERS})
    endforeach()
endif()

option(CFORM_BUILD_TESTS "Build the cform-ui test suite" OFF)

if(CFORM_BUILD_TESTS AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    enable_testing()
    add_subdirectory(tests)
endif()

# Off by default so a project that adds these layers as a subdirectory never
# builds a program it did not ask for; on in the developer preset and in CI,
# where the standalone-engine claim has to keep compiling.
option(CFORM_BUILD_EXAMPLES "Build the cform-ui examples" OFF)

if(CFORM_BUILD_EXAMPLES AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_subdirectory(examples)
endif()
