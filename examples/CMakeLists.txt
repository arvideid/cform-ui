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

    if(CFORM_WARNINGS)
        target_compile_options(scenes PRIVATE ${CFORM_WARNINGS})
    endif()
endif()
