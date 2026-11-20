# An example is a build-time proof rather than a demo: `layout_only` links
# cf_layout and nothing else, so the engine stops being standalone the moment
# a layer it must not reach is added to that line.
add_executable(layout_only layout_only.c)

target_link_libraries(layout_only PRIVATE cf_layout)

if(CFORM_WARNINGS)
    target_compile_options(layout_only PRIVATE ${CFORM_WARNINGS})
endif()
