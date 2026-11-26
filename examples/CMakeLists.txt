# Two programs, for two different reasons. `layout_only` is a build-time proof
# rather than a demo: it links cf_layout and nothing else, so the engine stops
# being standalone the moment a layer it must not reach is added to that line.
add_executable(layout_only layout_only.c)

target_link_libraries(layout_only PRIVATE cf_layout)

if(CFORM_WARNINGS)
    target_compile_options(layout_only PRIVATE ${CFORM_WARNINGS})
endif()

if(CFORM_WITH_SDL_BACKEND)
    # The gallery: every widget on one page, running on the app shell. It is the
    # program a human opens and the only proof the draw path exists, since the
    # commands it submits are the ones the golden tier already snapshots. The
    # font is the one fetched at configure; offline, the labels draw nothing and
    # the rest still runs.
    add_executable(gallery gallery.c)

    target_link_libraries(gallery PRIVATE cf_backend_sdl cf_app cf_theme)

    if(EMSCRIPTEN)
        # The playground: the same page in a tab. The font is embedded in the
        # emitted JS rather than preloaded because a browser has no path to
        # open and a `file://` tab will not fetch a sibling .data either; the
        # preload sidecar would put us back where we started. ASYNCIFY lets
        # cf_app_run's loop yield at each present instead of freezing the tab,
        # and SINGLE_FILE base64-embeds the wasm so the shell-file HTML is one
        # self-contained document a double-click opens.
        target_compile_definitions(gallery PRIVATE CFORM_GALLERY_FONT="/Lato-Regular.ttf")

        target_link_options(gallery PRIVATE
            "-sASYNCIFY=1"
            "-sALLOW_MEMORY_GROWTH=1"
            "-sUSE_WEBGL2=1"
            "-sSINGLE_FILE=1"
            "--embed-file=${CFORM_FONT_FILE}@/Lato-Regular.ttf"
            "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html"
        )

        # A shell file needs a .html output; with SINGLE_FILE and --embed-file
        # the JS, the wasm and the font all end up inside that one file.
        set_target_properties(gallery PROPERTIES SUFFIX ".html")
    else()
        target_compile_definitions(gallery PRIVATE CFORM_GALLERY_FONT="${CFORM_FONT_FILE}")
    endif()

    if(CFORM_WARNINGS)
        target_compile_options(gallery PRIVATE ${CFORM_WARNINGS})
    endif()
endif()
