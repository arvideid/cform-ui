cmake_minimum_required(VERSION 3.25)

# The version is read out of the header that states it rather than written here
# as well, so a package cannot report a number a caller cannot test.
include(cmake/cform_version.cmake)

project(cform-ui
    VERSION ${CFORM_VERSION}
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
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(cf_base STATIC
    src/base/arena.c
    src/base/block.c
    src/base/hash.c
    src/base/intern.c
    src/base/log.c
    src/base/store.c
    src/base/version.c
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
# max content, the ellipsis cap, and the cluster boundaries an editor steps
# along. It reads fonts through a table of pen distances and never rasterizes -
# a real font backend answers the same table, which is what keeps this layer
# headless.
add_library(cf_text STATIC
    src/text/cluster.c
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
# match.c indexes the retained keyed members diff.c takes slots from; state.c
# holds the blocks a build asks for by name across frames, scroll.c the offsets
# a scroller keeps under its name, resolve.c the theme's per-state paint applied
# to the view the diff built and build.c the children a declaration lists,
# copied where the diff can still read them. The layer is the first that owns
# memory across frames.
add_library(cf_node STATIC
    src/node/anim.c
    src/node/build.c
    src/node/context.c
    src/node/diff.c
    src/node/frame_run.c
    src/node/input.c
    src/node/match.c
    src/node/resolve.c
    src/node/scroll.c
    src/node/semantic.c
    src/node/state.c
    src/node/tree.c
)

target_link_libraries(cf_node PUBLIC cf_layout cf_paint)

# cf_widget fills declarations: each widget states the fields a build would
# otherwise spell by hand - a label's text and measure, a button's role and
# press handler, a scroller's offset and clip - out of the context's frame
# arena. Text, fonts and tones stay arguments, so the layer owns no loading
# and stays headless.
add_library(cf_widget STATIC
    src/widget/button.c
    src/widget/field.c
    src/widget/image.c
    src/widget/label.c
    src/widget/list.c
    src/widget/scroll.c
)

target_link_libraries(cf_widget PUBLIC cf_node cf_paint cf_text)

# cf_app is the shell a whole program fits in: it owns the loop the backend
# serves - the device's events in, one declared frame out - until the device
# asks to close. The inspector and the accessibility export land beside it as
# they exist.
add_library(cf_app STATIC
    src/app/inspector.c
    src/app/loop.c
)

target_link_libraries(cf_app PUBLIC cf_widget)

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

    # The browser has no DLLs to load, so SDL links static there, and SDL's own
    # EM_ASM uses need the GCC statement-expression extension that the strict
    # ISO dialect project-wide refuses.
    if(EMSCRIPTEN)
        set(CMAKE_C_EXTENSIONS ON)
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
    endif()

    FetchContent_Declare(sdl3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.2.20
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(sdl3)

    # Metrics and rasterization come from the same family, over one font file:
    # the numbers cf_text walks read and the glyphs the backend draws are the
    # same hinting decisions. The shaping stack is vendored on every platform -
    # a system harfbuzz is a different version per machine, and the conformance,
    # golden and bench tiers all compare against numbers that one version made.
    set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
    set(SDLTTF_SAMPLES OFF CACHE BOOL "" FORCE)
    set(SDLTTF_PLUTOSVG OFF CACHE BOOL "" FORCE)

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
        src/backend/sdl/clipboard.c
        src/backend/sdl/font.c
        src/backend/sdl/image.c
        src/backend/sdl/text.c
    )

    target_link_libraries(cf_backend_sdl PUBLIC cf_paint cf_text SDL3::SDL3 SDL3_ttf::SDL3_ttf)

    # The one font the examples and the font adapter read is fetched, not
    # committed: the repository carries no binary it can download. Both read
    # this path, so it is settled once here rather than per consumer, and an
    # offline machine gets a page without labels instead of a failed configure.
    set(CFORM_FONT_FILE "${CMAKE_BINARY_DIR}/fonts/Lato-Regular.ttf")

    if(NOT EXISTS "${CFORM_FONT_FILE}")
        file(DOWNLOAD
            "https://raw.githubusercontent.com/google/fonts/main/ofl/lato/Lato-Regular.ttf"
            "${CFORM_FONT_FILE}"
            STATUS CFORM_FONT_STATUS)
    endif()
endif()

set(CFORM_LAYER_TARGETS cf_base cf_style cf_theme cf_layout cf_text cf_paint cf_node cf_widget cf_app cf_backend_headless)

if(CFORM_WITH_SDL_BACKEND)
    list(APPEND CFORM_LAYER_TARGETS cf_backend_sdl)
endif()

foreach(layer IN LISTS CFORM_LAYER_TARGETS)
    target_include_directories(${layer}
        PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
               $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE src
    )

    # The dialect travels with the target rather than with whoever configured
    # it, so a project taking these layers through add_subdirectory inherits the
    # same C17 contract a top-level or installed build gets - the guard at the
    # top of this file only speaks for a build rooted here. The namespaced alias
    # then lets both adoption routes spell the targets the same way.
    target_compile_features(${layer} PUBLIC c_std_17)
    add_library(cform::${layer} ALIAS ${layer})

    if(CFORM_WARNINGS)
        target_compile_options(${layer} PRIVATE ${CFORM_WARNINGS})
    endif()
endforeach()

# A project outside this repository takes the headless floors as an installed
# package: the archives, the headers they were built against, and the two
# discovery files a consumer reads. The SDL backend is not in the package. Its
# interface names SDL3::SDL3, a target FetchContent creates inside a build tree
# and nowhere else, so an installed file that referenced it would fail to
# configure - and a window is the platform's own link line in any port.
set(CFORM_INSTALLED_TARGETS cf_base cf_style cf_theme cf_layout cf_text cf_paint cf_node cf_widget cf_app cf_backend_headless)

install(TARGETS ${CFORM_INSTALLED_TARGETS}
    EXPORT cformTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/cf
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)

install(EXPORT cformTargets
    FILE cform-ui-targets.cmake
    NAMESPACE cform::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cform-ui
)

configure_package_config_file(cmake/cform-ui-config.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/cform-ui-config.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cform-ui
)

# SameMinorVersion while the major is zero: 0.MINOR is where a break is allowed
# to live, so an install of 0.2 must not answer a find_package that asked for
# 0.9. At 1.0 this becomes SameMajorVersion and stays there - the promise that
# makes it safe is the one docs/versioning.md states for a MAJOR.
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/cform-ui-config-version.cmake"
    COMPATIBILITY SameMinorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/cform-ui-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/cform-ui-config-version.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cform-ui
)

# The same package for a build that does not use CMake. The prefix is derived
# from where the installed file sits, so moving the tree under it - a vendored
# install, an unpacked archive somewhere else - keeps the file answering without
# being reconfigured.
configure_file(cmake/cform.pc.in "${CMAKE_CURRENT_BINARY_DIR}/cform.pc" @ONLY)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/cform.pc"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)

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

# The one header a project outside this repository drops in beside its own
# source. Python is a build-time tool and nothing more: with no interpreter the
# packed header is not produced, and the case that compiles against it is not
# built. `cf_amalgamate` is the handle a packager builds.
find_package(Python3 QUIET COMPONENTS Interpreter)

set(CFORM_AMALGAMATED_LAYOUT "${CMAKE_BINARY_DIR}/amalgam/cf_layout.h")

if(Python3_Interpreter_FOUND)
    # The packer's whole input is these four headers: it reads no source file,
    # and their intra-project includes stay inside the packed set (types.h is
    # the only one they reach), so nothing is omitted and a stale header cannot
    # survive a change to any of them. The implementation the header declares is
    # linked from cf_layout by the test that compiles against it, not packed.
    add_custom_command(OUTPUT "${CFORM_AMALGAMATED_LAYOUT}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/amalgam"
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/amalgamate.py"
                --out "${CFORM_AMALGAMATED_LAYOUT}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/amalgamate.py"
                include/cf/types.h include/cf/base.h include/cf/style.h include/cf/layout.h
        COMMENT "Packing cf_layout.h from the layout headers"
        VERBATIM)

    add_custom_target(cf_amalgamate DEPENDS "${CFORM_AMALGAMATED_LAYOUT}")
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
