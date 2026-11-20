cmake_minimum_required(VERSION 3.25)

project(cform-ui
    VERSION 0.2.0
    DESCRIPTION "A small declarative layout UI library for C, rendered with SDL3."
    LANGUAGES C
)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(CMAKE_C_STANDARD 17)
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

# Remote image sources need an HTTP client. SDL3, SDL3_ttf and SDL3_image
# provide none, so native builds use libcurl, the browser build uses
# emscripten_fetch, and a native build without libcurl degrades to a stub that
# reports the URL as unloadable instead of failing the whole build.
set(CFORM_HTTP_BACKEND "none")

if(EMSCRIPTEN)
    set(CFORM_HTTP_BACKEND "emscripten")
    message(STATUS "Remote images: emscripten_fetch (CORS applies)")
else()
    find_package(CURL QUIET)

    if(CURL_FOUND)
        set(CFORM_HTTP_BACKEND "curl")
        message(STATUS "Remote images: libcurl ${CURL_VERSION_STRING}")
    else()
        message(WARNING
            "Remote images DISABLED - libcurl not found, so http(s) image_src "
            "will not load. Install it and reconfigure:\n"
            "  MSYS2:  pacman -S mingw-w64-ucrt-x86_64-curl\n"
            "  Debian: sudo apt install libcurl4-openssl-dev\n"
            "  or set -DCURL_DIR=/path/to/curl (local paths are unaffected).")
    endif()
endif()

# A renderer-agnostic core (`cform_core`) plus an SDL3 backend (`cform`) so the
# layout math can be unit-tested headlessly - no window, no display, no SDL.
add_library(cform_core STATIC
    src/core/context.c
    src/core/frame.c
    src/core/input.c
    src/core/layout.c
    src/core/style.c
)

target_include_directories(cform_core
    PUBLIC  include
    PRIVATE src
)

# One HTTP backend per platform behind a single internal header; the renderer
# never sees a platform branch.
if(CFORM_HTTP_BACKEND STREQUAL "emscripten")
    set(CFORM_HTTP_SOURCES src/renderer/http_loader_emscripten.c)
elseif(CFORM_HTTP_BACKEND STREQUAL "curl")
    set(CFORM_HTTP_SOURCES src/renderer/http_loader_curl.c)
else()
    set(CFORM_HTTP_SOURCES src/renderer/http_loader_none.c)
endif()

add_library(cform STATIC
    src/renderer/sdl_renderer.c
    ${CFORM_HTTP_SOURCES}
)

target_include_directories(cform
    PRIVATE src
)

# Consumers that link `cform` get the core, SDL3 and the SDL3 companion
# libraries (text and image decoding) transitively.
target_link_libraries(cform
    PUBLIC cform_core SDL3::SDL3 SDL3_ttf::SDL3_ttf SDL3_image::SDL3_image
)

# PRIVATE, because curl is an implementation detail of the renderer; CMake
# still records it as a link requirement for consumers of the static library.
if(CFORM_HTTP_BACKEND STREQUAL "curl")
    target_link_libraries(cform PRIVATE CURL::libcurl)
endif()

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

# The 1.0 layer graph starts here: one target per layer, so a dependency can
# only be created where the layer graph allows it — the linker is the check. A
# layer target is added when the layer has code, not before.
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
)

target_link_libraries(cf_layout PUBLIC cf_style cf_base)

set(CFORM_LAYER_TARGETS cf_base cf_style cf_layout)

foreach(layer IN LISTS CFORM_LAYER_TARGETS)
    target_include_directories(${layer}
        PUBLIC include
        PRIVATE src
    )

    if(CFORM_WARNINGS)
        target_compile_options(${layer} PRIVATE ${CFORM_WARNINGS})
    endif()
endforeach()

# Address and undefined behaviour sanitizers over the 1.0 layers and their
# tests. Off by default: the pair needs runtime libraries that neither MSVC nor
# a bare MinGW toolchain ships, and the toolchain switch is a build-time
# decision rather than a code one.
option(CFORM_WITH_SANITIZERS "Build the 1.0 layers under ASan and UBSan" OFF)

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

option(CFORM_BUILD_EXAMPLES "Build cform-ui example programs" ON)

if(CFORM_BUILD_EXAMPLES)
    add_executable(cform_demo example/demo.c)
    target_link_libraries(cform_demo PRIVATE cform)

    # The renderer probes `assets/fonts/…` as a last-resort font candidate.
    # Ship the directory beside the executable so SDL_GetBasePath finds it
    # from any CWD; the browser build mounts it into the virtual FS below.
    add_custom_command(TARGET cform_demo POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/example/assets
            $<TARGET_FILE_DIR:cform_demo>/assets
    )

    # On Windows libcurl is a shared library with a dependency chain of its own
    # (OpenSSL, zlib, nghttp2, brotli...). Copy the closure beside the demo so
    # it starts from Explorer or an IDE session without a prepared MSYS2 PATH.
    if(WIN32 AND CFORM_HTTP_BACKEND STREQUAL "curl")
        if(CURL_LIBRARY_RELEASE)
            set(CFORM_CURL_IMPLIB "${CURL_LIBRARY_RELEASE}")
        else()
            set(CFORM_CURL_IMPLIB "${CURL_LIBRARY}")
        endif()

        # The runtime DLLs sit in bin/ next to the import library's ../lib.
        # Globs tolerate an unnormalized path; anything unmatched simply is not
        # copied, so toolchains that ship curl elsewhere stay silent.
        get_filename_component(CFORM_CURL_LIBDIR "${CFORM_CURL_IMPLIB}" DIRECTORY)
        set(CFORM_CURL_BINDIR "${CFORM_CURL_LIBDIR}/../bin")
        set(CFORM_CURL_DLLS "")

        foreach(pattern IN ITEMS
            "libcurl-*.dll" "libcrypto-*.dll" "libssl-*.dll" "libz-*.dll"
            "libidn2-*.dll" "libunistring-*.dll" "libpsl-*.dll" "libintl-*.dll"
            "libiconv-*.dll" "libnghttp2-*.dll" "libnghttp3-*.dll"
            "libngtcp2*.dll" "libbrotli*.dll" "libssh2-*.dll" "libzstd*.dll"
        )
            file(GLOB matched "${CFORM_CURL_BINDIR}/${pattern}")
            list(APPEND CFORM_CURL_DLLS ${matched})
        endforeach()

        if(CFORM_CURL_DLLS)
            add_custom_command(TARGET cform_demo POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${CFORM_CURL_DLLS}
                    $<TARGET_FILE_DIR:cform_demo>
                COMMENT "Copying libcurl runtime DLLs beside cform_demo"
            )
        endif()
    endif()

    # WebAssembly: compile the SAME demo.c and layout into a browser-runnable
    # .html/.js/.wasm. ASYNCIFY lets the existing blocking ui_render() loop
    # yield to the browser instead of freezing the JS main thread.
    if(EMSCRIPTEN)
        target_link_options(cform_demo PRIVATE
            "-sUSE_WEBGL2=1"
            "-sASYNCIFY=1"
            "-sALLOW_MEMORY_GROWTH=1"
            "-sFETCH=1"
            "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/example/assets@/assets"
            "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/example/web/shell.html"
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
