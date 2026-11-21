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
    # backend. The tests cannot see a pixel, so a human runs this one.
    add_executable(scenes scenes.c)

    target_link_libraries(scenes PRIVATE cf_backend_sdl)

    # The gallery: every widget on one page, running on the app shell. The
    # font is the one the tests fetch; offline, the labels draw nothing and
    # the rest still runs.
    add_executable(gallery gallery.c)

    target_link_libraries(gallery PRIVATE cf_backend_sdl cf_app cf_theme)

    target_compile_definitions(gallery PRIVATE
        CFORM_GALLERY_FONT="${CMAKE_BINARY_DIR}/fonts/Lato-Regular.ttf")

    if(CFORM_WARNINGS)
        target_compile_options(scenes PRIVATE ${CFORM_WARNINGS})
        target_compile_options(gallery PRIVATE ${CFORM_WARNINGS})
    endif()
endif()
