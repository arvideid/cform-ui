# -----------------------------------------------------------------------------
# cform-ui test suite
#
# Linked against `cform_core` only (never the SDL renderer), so every test runs
# headlessly - no window, no display, no SDL needed at runtime. CTest is the
# runner; enabled from the root project with -DCFORM_BUILD_TESTS=ON.
# -----------------------------------------------------------------------------

function(cform_add_test name)
    add_executable(${name} ${name}.c)

    target_include_directories(${name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}   # for cform_test.h
        ${CMAKE_SOURCE_DIR}/src       # for internal.h
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
