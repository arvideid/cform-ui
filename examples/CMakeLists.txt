# The programs a person runs, one per thing they might want to prove. `layout_only` is a build-time
# proof rather than a demo: it links cf_layout and nothing else, so the engine stops being
# standalone the moment a layer it must not reach is added to that line.
add_executable(layout_only layout_only.c)

target_link_libraries(layout_only PRIVATE cf_layout)

if(CFORM_WARNINGS)
    target_compile_options(layout_only PRIVATE ${CFORM_WARNINGS})
endif()

# The host that owns its own loop, and the second adoption route: a program with a main loop and a
# renderer of its own takes frames without ever calling cf_app_run. It needs no window, no font file
# and no backend library, so it builds everywhere - which is the point of the route, and the reason
# it sits outside the SDL block below, where the third route (the forms application) lives.
add_executable(host_loop host_loop.c)

target_link_libraries(host_loop PRIVATE cf_widget cf_theme)

if(CFORM_WARNINGS)
    target_compile_options(host_loop PRIVATE ${CFORM_WARNINGS})
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

    # The documentation site, which is a program of the same shape as the gallery:
    # one driver, pages as declaration functions, the app shell, the SDL backend.
    # It is built natively so the pages get compiled and headless-tested on every
    # OS the CI runs, and to WebAssembly so the deployment can serve the same C.
    add_executable(site
        site/shell.c
        site/page_getting_started.c
        site/page_concepts.c
        site/page_api.c
        site/page_project.c
        site/page_showcase.c)

    target_link_libraries(site PRIVATE cf_backend_sdl cf_app cf_theme)
    target_include_directories(site PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/site)

    if(EMSCRIPTEN)
        # The same flags the gallery needs, plus the second face and the shell file
        # that carries this site's title, description and noscript mirror. Both fonts
        # are embedded rather than fetched: one file a tab opens, no sidecar to
        # arrange to be served beside it.
        target_compile_definitions(site PRIVATE
            CFORM_SITE_FONT="/Lato-Regular.ttf"
            CFORM_SITE_FONT_MONO="/IBMPlexMono-Regular.ttf")

        target_link_options(site PRIVATE
            "-sASYNCIFY=1"
            "-sALLOW_MEMORY_GROWTH=1"
            "-sUSE_WEBGL2=1"
            "-sSINGLE_FILE=1"
            "--embed-file=${CFORM_FONT_FILE}@/Lato-Regular.ttf"
            "--embed-file=${CFORM_FONT_MONO_FILE}@/IBMPlexMono-Regular.ttf"
            "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/web/site_shell.html"
        )

        set_target_properties(site PROPERTIES SUFFIX ".html")

        # The shell file is read at link time and is not a source, so nothing would
        # notice it changing: LINK_DEPENDS is what makes editing the HTML rebuild the
        # page instead of leaving the old one in the build tree.
        set_property(TARGET site APPEND PROPERTY LINK_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/web/site_shell.html")
    else()
        target_compile_definitions(site PRIVATE
            CFORM_SITE_FONT="${CFORM_FONT_FILE}"
            CFORM_SITE_FONT_MONO="${CFORM_FONT_MONO_FILE}")
    endif()

    if(CFORM_WARNINGS)
        target_compile_options(site PRIVATE ${CFORM_WARNINGS})
    endif()
endif()
