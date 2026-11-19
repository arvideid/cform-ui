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
