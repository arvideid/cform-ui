cmake_minimum_required(VERSION 3.20)

project(cform-ui
    VERSION 0.2.0
    DESCRIPTION "A small declarative layout UI library for C, rendered with SDL3."
    LANGUAGES C
)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(CMAKE_C_STANDARD 99)
    set(CMAKE_C_STANDARD_REQUIRED ON)

    # Emscripten's EM_ASM macros used inside SDL3 rely on GCC statement
    # expressions, so strict ISO C99 (-std=c99) cannot compile them. Native
    # builds keep the strict dialect to catch portability mistakes.
    if(EMSCRIPTEN)
        set(CMAKE_C_EXTENSIONS ON)
    else()
        set(CMAKE_C_EXTENSIONS OFF)
    endif()

    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

    # CMake is the single source of truth for compiler flags: clangd, the IDE
    # and CI all read the same compile_commands.json this produces.
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

# Prefer a locally installed SDL3 for native builds; the web target always
# fetches, because a native SDL3 cannot link into a WebAssembly binary.
if(NOT EMSCRIPTEN)
    find_package(SDL3 CONFIG QUIET)
    find_package(SDL3_ttf CONFIG QUIET)
    find_package(SDL3_image CONFIG QUIET)
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

if(NOT SDL3_ttf_FOUND)
    message(STATUS "SDL3_ttf not found locally - fetching a pinned release with FetchContent")

    include(FetchContent)

    # Vendored build pulls freetype and harfbuzz from SDL_ttf's git submodules,
    # so the demo works on machines that do not ship a system freetype.
    set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
    set(SDLTTF_SAMPLES OFF CACHE BOOL "" FORCE)
    set(SDLTTF_INSTALL OFF CACHE BOOL "" FORCE)
    set(SDLTTF_PLUTOSVG OFF CACHE BOOL "" FORCE)
    set(SDLTTF_TESTS OFF CACHE BOOL "" FORCE)
    set(SDLTTF_SHARED ON CACHE BOOL "" FORCE)

    FetchContent_Declare(
        SDL3_ttf
        GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
        GIT_TAG        release-3.2.2
        GIT_SHALLOW    TRUE
        GIT_SUBMODULES "external/freetype;external/harfbuzz"
    )
    FetchContent_MakeAvailable(SDL3_ttf)
endif()

if(NOT SDL3_image_FOUND)
    message(STATUS "SDL3_image not found locally - fetching a pinned release with FetchContent")

    include(FetchContent)

    # Vendored so the demo decodes PNG/JPEG/WEBP without system codecs.
    # Rare formats stay off: each one drags in another heavy dependency.
    set(SDLIMAGE_VENDORED ON CACHE BOOL "" FORCE)
    set(SDLIMAGE_SAMPLES OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_INSTALL OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_TESTS OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_AVIF OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_JXL OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_TIF OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_XCF OFF CACHE BOOL "" FORCE)

    if(EMSCRIPTEN)
        set(SDLIMAGE_SHARED OFF CACHE BOOL "" FORCE)
        set(SDLIMAGE_STATIC ON CACHE BOOL "" FORCE)
    else()
        set(SDLIMAGE_SHARED ON CACHE BOOL "" FORCE)
        set(SDLIMAGE_STATIC OFF CACHE BOOL "" FORCE)
    endif()

    # Only the four submodules the enabled formats need. Listing none would
    # recurse into aom, dav1d, libavif and libjxl too - hundreds of MB of
    # download for AVIF/JPEG-XL/TIFF, which the toggles above already disable.
    FetchContent_Declare(
        SDL3_image
        GIT_REPOSITORY https://github.com/libsdl-org/SDL_image.git
        GIT_TAG        release-3.2.6
        GIT_SHALLOW    TRUE
        GIT_SUBMODULES "external/jpeg;external/libpng;external/libwebp;external/zlib"
    )
    FetchContent_MakeAvailable(SDL3_image)
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

# Consumers that link `cform` get the core, SDL3 and the SDL3 companion
# libraries (text and image decoding) transitively.
target_link_libraries(cform
    PUBLIC cform_core SDL3::SDL3 SDL3_ttf::SDL3_ttf SDL3_image::SDL3_image
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

    # The demo loads images relative to its own directory; ship the assets
    # beside the executable so SDL_GetBasePath finds them from any CWD.
    add_custom_command(TARGET cform_demo POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/example/assets
            $<TARGET_FILE_DIR:cform_demo>/assets
    )

    # WebAssembly: compile the SAME demo.c and layout into a browser-runnable
    # .html/.js/.wasm. ASYNCIFY lets the existing blocking ui_render() loop
    # yield to the browser instead of freezing the JS main thread.
    if(EMSCRIPTEN)
        target_link_options(cform_demo PRIVATE
            "-sUSE_WEBGL2=1"
            "-sASYNCIFY=1"
            "-sALLOW_MEMORY_GROWTH=1"
            "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/example/assets@/assets"
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
