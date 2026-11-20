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

# The 1.0 layers are headless by construction, so their tests link nothing but
# the layer under test.
function(cf_add_unit_test name)
    add_executable(${name} unit/${name}.c)

    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${name} PRIVATE cf_base)

    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${name} PRIVATE -Wall -Wextra ${CFORM_SANITIZERS})
    elseif(MSVC)
        target_compile_options(${name} PRIVATE /W4)
    endif()

    add_test(NAME ${name} COMMAND ${name})
endfunction()

cf_add_unit_test(base_arena)
cf_add_unit_test(base_store)
cf_add_unit_test(base_intern)
cf_add_unit_test(base_log)
