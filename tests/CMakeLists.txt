# Tests link `cform_core` (never the SDL renderer) so they run headlessly in CI.

function(cform_add_test name)
    add_executable(${name} ${name}.c)

    target_include_directories(${name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
    )

    target_link_libraries(${name} PRIVATE cform_core)

    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${name} PRIVATE -Wall -Wextra)
    elseif(MSVC)
        target_compile_options(${name} PRIVATE /W4)
    endif()

    add_test(NAME ${name} COMMAND ${name})
endfunction()

cform_add_test(test_layout)
cform_add_test(test_frame)
cform_add_test(test_input)
cform_add_test(test_style)

# The 1.0 layers are headless by construction, so a test links the layer under
# test and nothing else - reaching outside a layer's dependencies fails at link
# rather than at review.
function(cf_add_unit_test name)
    add_executable(${name} unit/${name}.c)

    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${name} PRIVATE ${ARGN})

    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${name} PRIVATE -Wall -Wextra ${CFORM_SANITIZERS})
    elseif(MSVC)
        target_compile_options(${name} PRIVATE /W4)
    endif()

    add_test(NAME ${name} COMMAND ${name})
endfunction()

cf_add_unit_test(base_arena cf_base)
cf_add_unit_test(base_store cf_base)
cf_add_unit_test(base_intern cf_base)
cf_add_unit_test(base_log cf_base)
cf_add_unit_test(style_resolve cf_style)
cf_add_unit_test(layout_flex cf_layout)
cf_add_unit_test(layout_cases cf_layout)
