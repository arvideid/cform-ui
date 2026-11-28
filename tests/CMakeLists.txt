# The tiers are many small programs, and a runnable belongs to a human rather
# than to a suite: ctest names these by target and reads them from a directory
# of their own, so bin/ holds only what someone is meant to run.
set(CFORM_TEST_OUTPUT_DIR "${CMAKE_BINARY_DIR}/tests")

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

    set_property(TARGET ${name} PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CFORM_TEST_OUTPUT_DIR}")

    add_test(NAME ${name} COMMAND ${name})
    # A pass that cannot finish is a hang, and a hang in a suite nobody is
    # watching is a build that never reports. These tiers run in milliseconds,
    # so a minute is a verdict rather than a wait.
    set_tests_properties(${name} PROPERTIES TIMEOUT 60)
endfunction()

# The bench tier reports throughput rather than asserting it: a shared runner
# cannot be trusted for a wall-clock number, so the rig checks that the tree
# still lands where the spec says and prints what the machine managed.
# Labelled `bench` so a default run can skip it and a dedicated step can ask
# for it by name. No sanitizer flags: the sanitize job skips the label, and
# instrumentation in a timed rig measures the instrumentation.
function(cf_add_bench name source)
    add_executable(${name} ${source})

    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${name} PRIVATE ${ARGN})

    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${name} PRIVATE -Wall -Wextra)
    elseif(MSVC)
        target_compile_options(${name} PRIVATE /W4)
    endif()

    set_property(TARGET ${name} PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CFORM_TEST_OUTPUT_DIR}")

    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES LABELS bench)
endfunction()

cf_add_layer_test(base_arena unit/base_arena.c cf_base)
cf_add_layer_test(base_store unit/base_store.c cf_base)
cf_add_layer_test(base_intern unit/base_intern.c cf_base)
cf_add_layer_test(base_log unit/base_log.c cf_base)
cf_add_layer_test(base_version unit/base_version.c cf_base)
cf_add_layer_test(style_resolve unit/style_resolve.c cf_style)
cf_add_layer_test(layout_flex unit/layout_flex.c cf_layout)
cf_add_layer_test(layout_cases unit/layout_cases.c cf_layout)

# The standalone engine as a consumer takes it: one generated header, no include
# directory consulted. The case is built only where the packer ran, and depends
# on the target so the pack happens before the compile.
if(TARGET cf_amalgamate)
    cf_add_layer_test(layout_amalgamated unit/layout_amalgamated.c cf_layout)

    target_include_directories(layout_amalgamated PRIVATE "${CMAKE_BINARY_DIR}/amalgam")
    add_dependencies(layout_amalgamated cf_amalgamate)
endif()

cf_add_layer_test(text_measure unit/text_measure.c cf_text)

# Where a character begins and ends is its own rule, checked apart from the
# measure that walks the same bytes for width.
cf_add_layer_test(text_cluster unit/text_cluster.c cf_text)

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

# The switch is the first widget that states a bit its caller holds rather than
# bytes it edits: these cases flip it through either door and read the theme's
# answer in the stream.
cf_add_layer_test(widget_checkbox unit/widget_checkbox.c cf_widget cf_theme)

# The scroller and the list own the wheel: these cases walk content taller
# than the window and read the rows the window declares.
cf_add_layer_test(widget_scroll unit/widget_scroll.c cf_widget cf_theme)
cf_add_layer_test(widget_list unit/widget_list.c cf_widget cf_theme)

# The image is the first widget that paints something other than its box:
# this case reads the declaration the paint walk keys on.
cf_add_layer_test(widget_image unit/widget_image.c cf_widget cf_theme)

# The field edits the buffer its build owns: these cases press it, type into
# it and read the bytes back.
cf_add_layer_test(widget_field unit/widget_field.c cf_widget cf_theme)

# The dirty bit answers one question per frame: did this node's declaration
# move? These cases read it against the frame before it.
cf_add_layer_test(node_dirty unit/node_dirty.c cf_node)

# cf_frame_run is the pipeline the shell and the golden scenes play by hand:
# this case runs it once and reads the stream the whole pass leaves.
cf_add_layer_test(node_frame_run unit/node_frame_run.c cf_node)

# The declaration sugar is what a build spells its tree with: these cases read
# the children it counted, the direction it stated and the refusals it records.
cf_add_layer_test(node_build unit/node_build.c cf_node)

# The semantic export is the retained tree read as roles and text: this case
# declares one page and reads the dump a screen reader would walk.
cf_add_layer_test(semantics unit/semantics.c cf_node)

# The shell is the app layer's whole surface: this case plays a device that
# hands an event, dries up, then quits, and counts the frames it got. The panel
# reads a design system to report on, so cf_theme is linked for the case that
# names one.
cf_add_layer_test(app_run unit/app_run.c cf_app cf_backend_headless cf_theme)

# The gallery page drawn with no window: the same chapter builders examples/
# ships, driven through cf_app_run against the recorder across surfaces and
# scripted interactions, with every submitted stream checked for finite
# geometry, clips inside the output, radii inside their boxes and sharp
# fills that arrived cut. examples/ is export-ignored, so the source-only
# paths carry the page header to a test consumer too.
cf_add_layer_test(gallery_headless unit/gallery_headless.c
    cf_app cf_widget cf_theme cf_backend_headless cf_base)

target_include_directories(gallery_headless PRIVATE "${CMAKE_SOURCE_DIR}/examples")

# The page is header-only, so the test's own source has to name it for a
# change there to rebuild the tier.
set_source_files_properties(unit/gallery_headless.c PROPERTIES
    OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/examples/gallery_page.h")

# The documentation site, drawn the same way: every row of the navigation table
# driven through one frame at three surfaces, the stream checked for the geometry
# invariants above and the page checked for the heading it promises. The pages are
# real translation units rather than a header, so they are compiled in here exactly
# as examples/ builds them, and the file that answers the address bar on a machine
# with no browser comes along so the routing fallback is the one the site links.
cf_add_layer_test(site_headless unit/site_headless.c
    cf_app cf_widget cf_theme cf_backend_headless cf_base)

target_sources(site_headless PRIVATE
    "${CMAKE_SOURCE_DIR}/examples/site/page_getting_started.c"
    "${CMAKE_SOURCE_DIR}/examples/site/page_concepts.c"
    "${CMAKE_SOURCE_DIR}/examples/site/page_api.c"
    "${CMAKE_SOURCE_DIR}/examples/site/page_project.c"
    "${CMAKE_SOURCE_DIR}/examples/site/page_showcase.c"
    "${CMAKE_SOURCE_DIR}/src/backend/sdl/web_nav_none.c")

target_include_directories(site_headless PRIVATE "${CMAKE_SOURCE_DIR}/examples/site")

# Four headers carry the blocks, the values and the table every page reads, so a
# change to any of them has to rebuild the tier that draws the result.
set_source_files_properties(unit/site_headless.c PROPERTIES
    OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/examples/site/nav.h;${CMAKE_SOURCE_DIR}/examples/site/pages.h;${CMAKE_SOURCE_DIR}/examples/site/blocks.h;${CMAKE_SOURCE_DIR}/examples/site/tokens.h")

# The route a host with its own loop takes, asserted over the header the example runs: the page
# links no cf_app and no backend library, because the whole claim of the route is that frames come
# from cf_frame_run and the answers come from the host's own cf_backend. The tier is named the way
# the gallery and site ones are, so the program a person runs keeps the plain name.
cf_add_layer_test(host_loop_headless unit/host_loop.c cf_widget cf_theme)

target_include_directories(host_loop_headless PRIVATE "${CMAKE_SOURCE_DIR}/examples")

# The loop is header-only, so the tier's source has to name it for a change there to rebuild it.
set_source_files_properties(unit/host_loop.c PROPERTIES
    OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/examples/host_loop.h")

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
# backend does. The font itself is downloaded where the path is settled, in the
# root's SDL block, and a machine offline or without the backend skips this test
# rather than pretends.
if(CFORM_WITH_SDL_BACKEND)
    cf_add_layer_test(text_sdl_font unit/text_sdl_font.c cf_backend_sdl)
    target_compile_definitions(text_sdl_font PRIVATE CFORM_FONT_PATH="${CFORM_FONT_FILE}")

    # A loader finds a DLL beside the binary or on PATH, and the shared libraries
    # this one needs are runtime outputs of the example directory rather than of
    # the test directory. Linux and macOS get an RPATH from CMake instead.
    if(WIN32)
        set_tests_properties(text_sdl_font PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    endif()

    # The resource slots are only observable where a device owns bytes, so this
    # tier opens the window SDL's dummy video driver gives - no display, no
    # desktop automation - and reads what upload, release and teardown do, and what
    # a teardown may not touch.
    cf_add_layer_test(sdl_lifecycle unit/sdl_lifecycle.c cf_backend_sdl cf_backend_headless)
    # The drawn-word case rasterizes through a real face, so it reads the file the
    # font case reads and skips itself where the download never landed.
    target_compile_definitions(sdl_lifecycle PRIVATE CFORM_FONT_PATH="${CFORM_FONT_FILE}")
    set_tests_properties(sdl_lifecycle PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=dummy")

    if(WIN32)
        set_tests_properties(sdl_lifecycle PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    endif()
endif()

cf_add_bench(bench_layout bench/bench_layout.c cf_layout)

# The node layer's own budget: a 10k-node declaration reconciled frame
# after frame, cold and idle, beside the layout pass it feeds.
cf_add_bench(bench_node bench/bench_node.c cf_node)

# The paint rig's budget is the stream a 10k page hands a device: the command
# count is what the rig asserts, and the scratch it costs is what a host sizes
# an arena against.
cf_add_bench(bench_paint bench/bench_paint.c cf_node)

# The question the time rigs cannot ask: whether a page that stays open grows.
# The arena a 10k frame costs and the state a churning session holds are both
# printed, and both are held flat against the frame count.
cf_add_bench(bench_memory bench/bench_memory.c cf_node)
