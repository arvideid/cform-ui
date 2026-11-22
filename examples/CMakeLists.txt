# An example is a build-time proof rather than a demo: `layout_only` links
# cf_layout and nothing else, so the engine stops being standalone the moment
# a layer it must not reach is added to that line.
add_executable(layout_only layout_only.c)

target_link_libraries(layout_only PRIVATE cf_layout)

if(CFORM_WARNINGS)
    target_compile_options(layout_only PRIVATE ${CFORM_WARNINGS})
endif()

if(CFORM_WITH_SDL_BACKEND)
    # The draw path's proof: the golden scenes' panel submitted to the SDL
    # backend. The tests cannot see a pixel, so a human runs this one. Native
    # only - the browser runs the gallery, and a second .wasm nobody opens is
    # build time spent on noise.
    if(NOT EMSCRIPTEN)
        add_executable(scenes scenes.c)

        target_link_libraries(scenes PRIVATE cf_backend_sdl)
    endif()

    # The gallery: every widget on one page, running on the app shell. The
    # font is the one fetched at configure; offline, the labels draw nothing
    # and the rest still runs.
    add_executable(gallery gallery.c)

    target_link_libraries(gallery PRIVATE cf_backend_sdl cf_app cf_theme)

    if(EMSCRIPTEN)
        # The playground: the same page in a tab. The font is preloaded into
        # the data filesystem because a browser has no path to open, and
        # ASYNCIFY lets cf_app_run's loop yield at each present instead of
        # freezing the tab.
        target_compile_definitions(gallery PRIVATE CFORM_GALLERY_FONT="/Lato-Regular.ttf")

        target_link_options(gallery PRIVATE
            "-sASYNCIFY=1"
            "-sALLOW_MEMORY_GROWTH=1"
            "-sUSE_WEBGL2=1"
            "--preload-file=${CFORM_FONT_FILE}@/Lato-Regular.ttf"
            "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html"
        )

        # A shell file needs a .html output; the .js and .wasm land beside it.
        set_target_properties(gallery PROPERTIES SUFFIX ".html")
    else()
        target_compile_definitions(gallery PRIVATE CFORM_GALLERY_FONT="${CFORM_FONT_FILE}")
    endif()

    if(CFORM_WARNINGS)
        target_compile_options(gallery PRIVATE ${CFORM_WARNINGS})

        if(NOT EMSCRIPTEN)
            target_compile_options(scenes PRIVATE ${CFORM_WARNINGS})
        endif()
    endif()
endif()
