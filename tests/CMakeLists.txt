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
cf_add_layer_test(text_measure unit/text_measure.c cf_text)

# The measure slot is the seam layout and text share: this one links both
# layers and asserts the height a wrapped label contributes to its parent.
cf_add_layer_test(text_layout unit/text_layout.c cf_layout cf_text)
cf_add_layer_test(paint_emit unit/paint_emit.c cf_paint)
cf_add_layer_test(backend_record unit/backend_record.c cf_backend_headless)

# The diff is the retained side of a frame: these cases drive two or more
# declarations through one context and read which slot each node kept.
cf_add_layer_test(node_tree unit/node_tree.c cf_node)

# cf_state is the memory a build asks for by name: these cases drive one
# name through frames that keep it, skip it and outgrow it.
cf_add_layer_test(node_state unit/node_state.c cf_node)

# The rebuild rig drives random declarations through one context: the same
# seed rebuilds the same sequence, so a failure names its seed and frame.
cf_add_layer_test(node_rebuild unit/node_rebuild.c cf_node)

# cf_input is the dispatch half of a frame: these cases lay a tree out,
# drive events at it and read which handler saw them.
cf_add_layer_test(node_input unit/node_input.c cf_node)

# The resolver is the seam the frame driver owns: these cases play that
# driver, linking the node layer that walks the view and the theme layer
# that supplies the variants.
cf_add_layer_test(theme_resolve unit/theme_resolve.c cf_node cf_theme)

# The scroll math is core-owned: these cases drive offsets, thumb geometry
# and thumb drags through one scrollbar's worth of numbers.
cf_add_layer_test(node_scroll unit/node_scroll.c cf_node)

# Tweens live in the same keyed blocks: these cases step values through
# durations, eases and rebuilds.
cf_add_layer_test(node_anim unit/node_anim.c cf_node)

# The widgets are the first layer a build composes: these cases drive a
# button through the dispatch it configures and read the click it fires.
cf_add_layer_test(widget_button unit/widget_button.c cf_widget cf_theme)

# The scroller and the list own the wheel: these cases walk content taller
# than the window and read the rows the window declares.
cf_add_layer_test(widget_scroll unit/widget_scroll.c cf_widget cf_theme)
cf_add_layer_test(widget_list unit/widget_list.c cf_widget cf_theme)

# The dirty bit answers one question per frame: did this node's declaration
# move? These cases read it against the frame before it.
cf_add_layer_test(node_dirty unit/node_dirty.c cf_node)

# The golden tier snapshots the command stream the recorder backend receives:
# each scene lays out, paints and submits, and the text one command per line is
# the diff surface a paint change is judged on. A plain run compares against
# the snapshots; `golden_cases --update` rewrites them after an intentional
# change.
cf_add_layer_test(golden_cases golden/golden_cases.c cf_backend_headless)
target_compile_definitions(golden_cases PRIVATE
    CFORM_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden/snapshots")

# Yoga's fixtures, ported by tools/gentest: the flexbox suite as data, with the
# box a reference browser measured beside it. This is the tier the ≥95%
# conformance claim is checked against, so it runs with the unit tests rather
# than behind a label.
cf_add_layer_test(yoga_cases conformance/yoga_cases.c cf_layout)

# The real font answers the same table the fakes do, measured without
# rasterizing; it needs the backend's fetch, so it builds only where the
# backend does. The repo carries no font binary: the one the adapter reads
# is fetched once into the build, and a machine offline or without the
# backend skips the test rather than pretends.
if(CFORM_WITH_SDL_BACKEND)
    set(CFORM_FONT_FILE "${CMAKE_BINARY_DIR}/fonts/Lato-Regular.ttf")

    if(NOT EXISTS "${CFORM_FONT_FILE}")
        file(DOWNLOAD
            "https://raw.githubusercontent.com/google/fonts/main/ofl/lato/Lato-Regular.ttf"
            "${CFORM_FONT_FILE}"
            STATUS CFORM_FONT_STATUS)
    endif()

    cf_add_layer_test(text_sdl_font unit/text_sdl_font.c cf_backend_sdl)
    target_compile_definitions(text_sdl_font PRIVATE CFORM_FONT_PATH="${CFORM_FONT_FILE}")
endif()

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

# The node layer's own budget: a 10k-node declaration reconciled frame
# after frame, cold and idle, beside the layout pass it feeds.
add_executable(bench_node bench/bench_node.c)

target_include_directories(bench_node PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bench_node PRIVATE cf_node)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(bench_node PRIVATE -Wall -Wextra)
elseif(MSVC)
    target_compile_options(bench_node PRIVATE /W4)
endif()

add_test(NAME bench_node COMMAND bench_node)
set_tests_properties(bench_node PROPERTIES LABELS bench)
