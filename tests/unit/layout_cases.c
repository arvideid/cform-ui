#include "layout_case.h"

/* Vocabulary the hand-written corpus uses on top of the row format. */
#define BOX(each) {CF_PX(each), CF_PX(each), CF_PX(each), CF_PX(each)}
#define EDGES(left, top, right, bottom) {CF_PX(left), CF_PX(top), CF_PX(right), CF_PX(bottom)}
#define MARGIN(margin_box, ...)                                                                    \
    (cf_style) {                                                                                   \
        .margin = margin_box, .flex_shrink = 1.0f, __VA_ARGS__                                     \
    }
#define SIZE(box_width, box_height) STYLE(.width = CF_PX(box_width), .height = CF_PX(box_height))
#define FILL STYLE(.flex_grow = 1.0f)
#define GROWN(...) STYLE(.flex_grow = 1.0f, __VA_ARGS__)
#define COLUMN(...) STYLE(.direction = CF_DIRECTION_COLUMN, __VA_ARGS__)
#define ABSOLUTE(...) STYLE(.position = CF_POSITION_ABSOLUTE, __VA_ARGS__)
#define GRID(...) STYLE(.display = CF_DISPLAY_GRID, __VA_ARGS__)
/* A track is minmax(min, max); a fixed one repeats its length in both slots. */
#define TRACK(min_length, max_length) {(min_length), (max_length)}
#define TRACKS(...)                                                                                \
    (const cf_track[]) {                                                                           \
        __VA_ARGS__                                                                                \
    }
#define FIXED(pixels) TRACK(CF_PX(pixels), CF_PX(pixels))

/*
 * The corpus is the 0.2 layout suite, restated against the flexbox rules, the
 * children that leave the flow, and the grid subset the track solver
 * implements. Expectations are what the spec says comes out, not what the old
 * engine drew, which is why a declared cross size survives stretch and a
 * border moves the content box.
 */
static const layout_case cases[] = {
#include "layout_cases_data.h"
};

int main(void) {
    const uint32_t count = (uint32_t)(sizeof cases / sizeof cases[0]);
    const uint32_t matched = layout_corpus_run("layout cases", cases, count);

    CF_CHECK(matched == count, "every case in the corpus lands where the spec puts it");

    return CF_REPORT();
}
