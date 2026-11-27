/*
 * Getting started: the three ways this library arrives in a build, and the whole
 * program any one of them runs.
 *
 * The commands are the ones README's quickstart states and the ones the release
 * workflow is tested against, so a panel here is a transcription rather than a
 * second opinion. What the page adds is the sentence that chooses between them,
 * which a transcript does not have. The prose about what the toolkit *is* stays in
 * README and this page links there: the same argument written twice disagrees
 * within a month, and the reader cannot tell which one the source still supports.
 */
#include "pages.h"

/* The three routes, in the order a reader meets them: vendored, installed, packed. */
static const char *const routes[] = {"In your tree", "Installed", "One header"};

static const char *const embedded[] = {
    "# vendor/cform-ui is this repository at whatever tag you want to build on",
    "add_subdirectory(vendor/cform-ui)",
    "",
    "target_link_libraries(my_app PRIVATE cform::cf_app)",
    "",
    "# or the engine alone, with no widgets and no window:",
    "target_link_libraries(my_solver PRIVATE cform::cf_layout)",
};

static const char *const installed[] = {
    "# once, on the machine that builds the library",
    "cmake -S . -B build/dev -DCFORM_WITH_SDL_BACKEND=ON",
    "cmake --build build/dev",
    "cmake --install build/dev --prefix /where/you/want",
    "",
    "# then in the consumer's CMakeLists.txt",
    "find_package(cform-ui REQUIRED CONFIG)",
    "target_link_libraries(my_app PRIVATE cform::cf_app)",
    "",
    "# and for a build that does not use CMake at all",
    "cc my_app.c $(pkg-config --cflags --libs cform)",
};

static const char *const packed[] = {
    "cmake --build build/dev --target cf_amalgamate",
    "ls build/dev/amalgam/cf_layout.h",
    "",
    "/* one header, no include path, no archive: the engine as a file */",
    "#include \"cf_layout.h\"",
    "",
    "cf_style root = cf_style_default();",
    "root.width = CF_PT(200.0f);",
    "root.direction = CF_DIRECTION_ROW;",
};

/* The complete program README prints, in the order a reader types it. */
static const char *const program[] = {
    "#include <stdio.h>",
    "",
    "#include \"cf/app.h\"     /* the run loop          */",
    "#include \"cf/build.h\"   /* CF_COL, CF_ROW        */",
    "#include \"cf/widget.h\"  /* cf_label, cf_button   */",
    "",
    "static const cf_theme *theme;",
    "static cf_font font;",
    "static cf_text_stack fonts;",
    "",
    "static void say_hello(cf_ctx *ctx, cf_id node, void *user_data) {",
    "    (void)ctx, (void)node, (void)user_data;",
    "    printf(\"hello\\n\");",
    "}",
    "",
    "static cf_node build(cf_ctx *ctx) {",
    "    const cf_node text = cf_label(ctx, cf_string_of(\"press me\"), &fonts, theme->on_ink);",
    "    const cf_node button = cf_button(ctx, \"greet\", say_hello, NULL, &text, 1u);",
    "",
    "    return CF_COL(ctx, theme->page, button);",
    "}",
    "",
    "int main(void) {",
    "    theme = cf_theme_default();",
    "    font = cf_font_sdl(\"Lato-Regular.ttf\", 14.0f);",
    "    fonts = (cf_text_stack){.fonts = &font, .count = 1u};",
    "",
    "    cf_app_run(&(cf_app_config){.build = build, .theme = theme, .fonts = &fonts,",
    "                                .backend = cf_backend_sdl(&(cf_sdl_config){",
    "                                    .title = \"hello\", .width = 480u, .height = 320u})});",
    "}",
};

cf_node site_page_getting_started(cf_ctx *ctx, const site_view *view) {
    /* Each panel states the route it belongs to, so the tab labels and the code
     * cannot drift apart: a reader who opens "Installed" and sees a fetchContent
     * line has found the bug without being told about it. */
    const cf_node panels[] = {
        site_panel(ctx, view, embedded, SITE_COUNT(embedded)),
        site_panel(ctx, view, installed, SITE_COUNT(installed)),
        site_panel(ctx, view, packed, SITE_COUNT(packed)),
    };

    const cf_node taking_it_in = site_section(
        ctx, view, "Taking it into a build", "three-ways-in",
        (cf_node[]){
            site_tabs(ctx, view, "install", routes, SITE_COUNT(routes), panels, SITE_COUNT(panels)),
            site_note(ctx, view,
                      "Embedding is what this repository does to itself and what "
                      "a project that ships one binary wants: the layers arrive as "
                      "cform::cf_app, cform::cf_widget, cform::cf_layout, and every "
                      "one of them carries its own C17 requirement. Installing is "
                      "what a consumer who does not rebuild the world wants, and it "
                      "is the route CI proves on every push - the package has to "
                      "answer find_package and pkg-config from a prefix it was never "
                      "configured with. The packed header is the layout engine "
                      "alone: no widgets, no text, no window, one file."),
        },
        2u);

    const cf_node whole_program =
        site_section(ctx, view, "A complete program", "complete-program",
                     (cf_node[]){
                         site_p(ctx, view,
                                "This is a button that prints when you click it, and it is the "
                                "whole of what using the library looks like: a build function "
                                "that answers what the screen should hold, and a shell that "
                                "calls it as often as the screen needs. Nothing is registered, "
                                "nothing is subscribed and nothing is torn down."),
                         site_panel(ctx, view, program, SITE_COUNT(program)),
                         site_aside(ctx, view,
                                    "The string \"greet\" is the button's name: it is how the "
                                    "frame recognises the same control from one declaration to "
                                    "the next, so a click count, a scroll offset and the "
                                    "keyboard's place in the page survive a rebuild."),
                     },
                     3u);

    const cf_node ways_out[] = {
        site_link(ctx, view, "to-readme", "The README quickstart", SITE_BLOB "README.md"),
        site_link(ctx, view, "to-gallery", "The gallery in a tab", SITE_GALLERY),
    };

    const cf_node blocks[] = {
        taking_it_in,
        whole_program,
        site_strip(ctx, ways_out, SITE_COUNT(ways_out)),
    };

    return site_page(ctx, view, SITE_GROUP_DOCS, "Getting started", "getting-started",
                     "CMake 3.25 or newer, a C17 compiler and no third-party dependency the "
                     "engine itself needs. Take the sources, take the installed package, or take "
                     "one generated header.",
                     blocks, SITE_COUNT(blocks));
}
