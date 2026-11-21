# The layers are headless by construction, so a test links the layer under
# test and nothing else - reaching outside a layer's dependencies fails at link
# rather than at review.
function(cf_add_layer_test name source)
    add_executable(${name} ${source})

    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${name} PRIVATE ${ARGN})

    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${name} PRIVATE -Wall -Wextra ${CFORM_SANITIZERS})
    elseif(MSVC)
        target_compile_options(${name} PRIVATE /W4)
    endif()

    add_test(NAME ${name} COMMAND ${name})
endfunction()

cf_add_layer_test(base_arena unit/base_arena.c cf_base)
cf_add_layer_test(base_store unit/base_store.c cf_base)
cf_add_layer_test(base_intern unit/base_intern.c cf_base)
cf_add_layer_test(base_log unit/base_log.c cf_base)
cf_add_layer_test(style_resolve unit/style_resolve.c cf_style)
cf_add_layer_test(layout_flex unit/layout_flex.c cf_layout)
cf_add_layer_test(layout_cases unit/layout_cases.c cf_layout)
cf_add_layer_test(paint_emit unit/paint_emit.c cf_paint)
cf_add_layer_test(backend_record unit/backend_record.c cf_backend_headless)

# Yoga's fixtures, ported by tools/gentest: the flexbox suite as data, with the
# box a reference browser measured beside it. This is the tier the ≥95%
# conformance claim is checked against, so it runs with the unit tests rather
# than behind a label.
cf_add_layer_test(yoga_cases conformance/yoga_cases.c cf_layout)

# The bench tier reports throughput rather than asserting it: a shared runner
# cannot be trusted for a wall-clock number, so the rig checks that the tree
# still lays out and prints what the machine managed. Labelled `bench` so a
# default run can skip it and a dedicated step can ask for it by name.
add_executable(bench_layout bench/bench_layout.c)

target_include_directories(bench_layout PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bench_layout PRIVATE cf_layout)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(bench_layout PRIVATE -Wall -Wextra)
elseif(MSVC)
    target_compile_options(bench_layout PRIVATE /W4)
endif()

add_test(NAME bench_layout COMMAND bench_layout)
set_tests_properties(bench_layout PROPERTIES LABELS bench)
