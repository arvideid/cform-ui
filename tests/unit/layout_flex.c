#include "cf/layout.h"
#include "cform_test.h"

#define MAX_NODES 16u
#define MAX_CHILDREN 4u

typedef struct fixture {
    cf_style styles[MAX_NODES];
    cf_measure_fn measures[MAX_NODES];
    void *measure_data[MAX_NODES];
    uint32_t children[MAX_NODES][MAX_CHILDREN];
    uint32_t child_counts[MAX_NODES];
    uint32_t child_start[MAX_NODES + 1u];
    uint32_t child_slots[MAX_NODES * MAX_CHILDREN];
    cf_rect rects[MAX_NODES];
    uint32_t node_count;
} fixture;

static cf_arena scratch;

/*
 * A text or image node the engine cannot look inside. The two intrinsic sizes
 * are answered separately so a shrink can be bounded by the narrowest the
 * content can be packed into rather than by its natural width.
 */
typedef struct foreign_content {
    cf_size min_content;
    cf_size max_content;
} foreign_content;

static cf_size measure_foreign(void *user_data, cf_available available, cf_intrinsic kind) {
    (void)available;
    const foreign_content *content = user_data;

    return kind == CF_INTRINSIC_MIN_CONTENT ? content->min_content : content->max_content;
}

static uint32_t add_node(fixture *f, cf_style style) {
    const uint32_t slot = f->node_count;
    f->styles[slot] = style;
    f->node_count++;
    return slot;
}

static uint32_t append(fixture *f, uint32_t parent, cf_style style) {
    const uint32_t slot = add_node(f, style);
    f->children[parent][f->child_counts[parent]++] = slot;
    return slot;
}

static uint32_t begin(fixture *f, cf_style root_style) {
    *f = (fixture){0};
    return add_node(f, root_style);
}

/* The usual case: a container whose own box is settled by its caller. */
static uint32_t begin_box(fixture *f, float width, float height) {
    cf_style root = cf_style_default();
    root.width = CF_PX(width);
    root.height = CF_PX(height);
    return begin(f, root);
}

/* The child_start range for a node is written when its children are flattened,
 * so a hand-built tree needs no particular slot order. */
static cf_tree tree_of(fixture *f) {
    uint32_t edge = 0u;
    for (uint32_t slot = 0u; slot < f->node_count; ++slot) {
        f->child_start[slot] = edge;
        for (uint32_t i = 0u; i < f->child_counts[slot]; ++i) {
            f->child_slots[edge++] = f->children[slot][i];
        }
    }
    f->child_start[f->node_count] = edge;

    return (cf_tree){
        .styles = f->styles,
        .measures = f->measures,
        .measure_data = f->measure_data,
        .child_start = f->child_start,
        .child_slots = f->child_slots,
        .node_count = f->node_count,
    };
}

static void arrange_with(cf_arena *arena, fixture *f, cf_available available) {
    const cf_tree tree = tree_of(f);
    cf_layout(arena, &tree, 0u, available, (cf_point){0.0f, 0.0f}, f->rects);
}

static void arrange_in(fixture *f, float width, float height) {
    arrange_with(&scratch, f, (cf_available){.width = CF_PX(width), .height = CF_PX(height)});
}

static cf_style fixed_box(float width, float height) {
    cf_style style = cf_style_default();
    style.width = CF_PX(width);
    style.height = CF_PX(height);
    return style;
}

static void free_space_splits_between_items_by_their_grow_factors(void) {
    fixture f;
    const uint32_t root = begin_box(&f, 300.0f, 100.0f);

    for (uint32_t grow = 1u; grow <= 3u; ++grow) {
        cf_style child = cf_style_default();
        child.width = CF_PX(0.0f);
        child.flex_grow = (float)grow;
        append(&f, root, child);
    }
    arrange_in(&f, 300.0f, 100.0f);

    CF_NEAR(f.rects[1].width, 50.0f);
    CF_NEAR(f.rects[2].width, 100.0f);
    CF_NEAR(f.rects[3].width, 150.0f);
    CF_NEAR(f.rects[1].x, 0.0f);
    CF_NEAR(f.rects[2].x, 50.0f);
    CF_NEAR(f.rects[3].x, 150.0f);
    /* The cross axis is stretched, so a row fills the height it was offered. */
    CF_NEAR(f.rects[1].height, 100.0f);
}

static void items_shrink_by_their_weighted_base_sizes(void) {
    fixture f;
    const uint32_t root = begin_box(&f, 200.0f, 100.0f);

    for (int32_t i = 0; i < 3; ++i) {
        append(&f, root, fixed_box(100.0f, 0.0f));
    }
    arrange_in(&f, 200.0f, 100.0f);

    const float share = 200.0f / 3.0f;
    for (uint32_t i = 1u; i <= 3u; ++i) {
        CF_NEAR(f.rects[i].width, share);
        CF_NEAR(f.rects[i].x, share * (float)(i - 1u));
    }
}

static void a_floored_item_hands_the_space_it_cannot_give_back(void) {
    fixture f;
    const uint32_t root = begin_box(&f, 300.0f, 100.0f);

    cf_style grower = cf_style_default();
    grower.width = CF_PX(0.0f);
    grower.flex_grow = 1.0f;
    const uint32_t wide = append(&f, root, grower);

    grower.max_width = CF_PX(40.0f);
    const uint32_t capped = append(&f, root, grower);

    arrange_in(&f, 300.0f, 100.0f);

    /* The capped item freezes at 40 and the second round hands the 260 the
     * first round had split evenly to the item that can still take it. */
    CF_NEAR(f.rects[wide].width, 260.0f);
    CF_NEAR(f.rects[capped].width, 40.0f);
    CF_NEAR(f.rects[capped].x, 260.0f);
}

static void an_item_clamped_up_by_its_minimum_gives_its_growth_to_the_rest(void) {
    fixture f;
    const uint32_t root = begin_box(&f, 200.0f, 100.0f);

    cf_style child = cf_style_default();
    child.width = CF_PX(30.0f);
    child.min_width = CF_PX(120.0f);
    child.flex_grow = 1.0f;
    const uint32_t raised = append(&f, root, child);

    child.width = CF_PX(20.0f);
    child.min_width = CF_PX(40.0f);
    child.flex_grow = 4.0f;
    const uint32_t flexing = append(&f, root, child);

    arrange_in(&f, 200.0f, 100.0f);

    /* A round books the 30 and 20 the items based on, not the 120 and 40 their
     * minima impose: the first round breaks the first item's floor, freezes it
     * there and the restart hands the rest of the line the room left over. */
    CF_NEAR(f.rects[raised].width, 120.0f);
    CF_NEAR(f.rects[flexing].width, 80.0f);
    CF_NEAR(f.rects[flexing].x, 120.0f);
}

static void grow_factors_below_one_leave_their_space_undistributed(void) {
    fixture f;
    const uint32_t root = begin_box(&f, 100.0f, 50.0f);

    for (uint32_t i = 0u; i < 2u; ++i) {
        cf_style child = cf_style_default();
        child.width = CF_PX(0.0f);
        child.flex_grow = 0.2f;
        append(&f, root, child);
    }
    arrange_in(&f, 100.0f, 50.0f);

    /* Each item takes its 20% of the 100 pixels and the line stops there,
     * rather than normalising the two factors until the box is full. */
    CF_NEAR(f.rects[1].width, 20.0f);
    CF_NEAR(f.rects[2].width, 20.0f);
    CF_NEAR(f.rects[2].x, 20.0f);
}

static void the_automatic_minimum_size_stops_a_shrink_at_the_content(void) {
    foreign_content text = {
        .min_content = {.width = 40.0f, .height = 20.0f},
        .max_content = {.width = 100.0f, .height = 20.0f},
    };

    fixture f;
    const uint32_t root = begin_box(&f, 60.0f, 100.0f);

    for (uint32_t i = 1u; i <= 2u; ++i) {
        append(&f, root, cf_style_default());
        f.measures[i] = measure_foreign;
        f.measure_data[i] = &text;
    }
    arrange_in(&f, 60.0f, 100.0f);

    /* Each item would give 30 of its 100 base pixels, but min-width:auto
     * floors it at the 40 its content needs, so the line overflows by design. */
    CF_NEAR(f.rects[1].width, 40.0f);
    CF_NEAR(f.rects[2].width, 40.0f);
    CF_NEAR(f.rects[2].x, 40.0f);
}

static void a_column_stacks_its_items_vertically(void) {
    fixture f;
    const uint32_t root = begin_box(&f, 100.0f, 300.0f);
    f.styles[root].direction = CF_DIRECTION_COLUMN;

    for (int32_t i = 0; i < 2; ++i) {
        cf_style child = cf_style_default();
        child.height = CF_PX(50.0f);
        child.flex_grow = 1.0f;
        append(&f, root, child);
    }
    arrange_in(&f, 100.0f, 300.0f);

    CF_NEAR(f.rects[1].x, 0.0f);
    CF_NEAR(f.rects[1].y, 0.0f);
    CF_NEAR(f.rects[1].width, 100.0f);
    CF_NEAR(f.rects[1].height, 150.0f);
    CF_NEAR(f.rects[2].y, 150.0f);
    CF_NEAR(f.rects[2].height, 150.0f);
}

static void wrap_breaks_between_items_at_the_container_edge(void) {
    fixture f;
    const uint32_t root = begin_box(&f, 100.0f, 200.0f);
    f.styles[root].wrap = CF_WRAP;
    f.styles[root].align_content = CF_ALIGN_CONTENT_START;

    for (int32_t i = 0; i < 3; ++i) {
        append(&f, root, fixed_box(60.0f, 50.0f));
    }
    arrange_in(&f, 100.0f, 200.0f);

    /* A line never breaks inside an item, so 60 + 60 does not fit and each
     * item lands on a line of its own, stacked at the cross start. */
    CF_NEAR(f.rects[1].y, 0.0f);
    CF_NEAR(f.rects[2].y, 50.0f);
    CF_NEAR(f.rects[3].y, 100.0f);
    CF_NEAR(f.rects[3].x, 0.0f);
    CF_NEAR(f.rects[3].width, 60.0f);

    f.styles[root].wrap = CF_WRAP_REVERSE;
    arrange_in(&f, 100.0f, 200.0f);

    /* wrap-reverse breaks the same lines and stacks them from the cross end,
     * so the first item lands at the bottom of the box. */
    CF_NEAR(f.rects[1].y, 150.0f);
    CF_NEAR(f.rects[2].y, 100.0f);
    CF_NEAR(f.rects[3].y, 50.0f);
}

static void justify_content_distributes_the_leftover_main_space(void) {
    fixture f;
    const uint32_t root = begin_box(&f, 300.0f, 100.0f);

    append(&f, root, fixed_box(100.0f, 0.0f));
    append(&f, root, fixed_box(100.0f, 0.0f));

    f.styles[root].justify_content = CF_JUSTIFY_CENTER;
    arrange_in(&f, 300.0f, 100.0f);
    CF_NEAR(f.rects[1].x, 50.0f);
    CF_NEAR(f.rects[2].x, 150.0f);

    f.styles[root].justify_content = CF_JUSTIFY_SPACE_BETWEEN;
    arrange_in(&f, 300.0f, 100.0f);
    CF_NEAR(f.rects[1].x, 0.0f);
    CF_NEAR(f.rects[2].x, 200.0f);
}

static void an_auto_margin_claims_the_space_before_justify_content_does(void) {
    fixture f;
    const uint32_t root = begin_box(&f, 300.0f, 100.0f);

    append(&f, root, fixed_box(100.0f, 0.0f));

    cf_style pushed = fixed_box(100.0f, 0.0f);
    pushed.margin.left = CF_AUTO;
    append(&f, root, pushed);

    f.styles[root].justify_content = CF_JUSTIFY_CENTER;
    arrange_in(&f, 300.0f, 100.0f);

    /* All 100 pixels of leftover go to the auto margin, so the centring that
     * would have split them lands the second item flush against the end. */
    CF_NEAR(f.rects[1].x, 0.0f);
    CF_NEAR(f.rects[2].x, 200.0f);
}

static void a_cross_end_auto_margin_leaves_the_item_at_cross_start(void) {
    fixture f;
    const uint32_t root = begin_box(&f, 200.0f, 200.0f);
    f.styles[root].direction = CF_DIRECTION_COLUMN;
    f.styles[root].align_items = CF_ALIGN_CENTER;

    cf_style pushed = fixed_box(50.0f, 50.0f);
    pushed.margin.right = CF_AUTO;
    const uint32_t item = append(&f, root, pushed);
    arrange_in(&f, 200.0f, 200.0f);

    /* The claim sits on the side that made it, so the 150 pixels the column
     * leaves over stay behind the box instead of pushing it across the line. */
    CF_NEAR(f.rects[item].x, 0.0f);
    CF_NEAR(f.rects[item].width, 50.0f);
}

static void an_item_wider_than_its_line_overflows_both_edges(void) {
    fixture f;
    const uint32_t root = begin_box(&f, 100.0f, 100.0f);
    f.styles[root].direction = CF_DIRECTION_COLUMN;
    f.styles[root].align_items = CF_ALIGN_CENTER;

    const uint32_t wide = append(&f, root, fixed_box(140.0f, 20.0f));
    arrange_in(&f, 100.0f, 100.0f);

    /* A single line is the cross size the container settled on, and centring
     * reads the shortfall against it: the 40 pixels the item outgrows by split
     * across both edges, the way an over-long item splits along the line. */
    CF_NEAR(f.rects[wide].x, -20.0f);
    CF_NEAR(f.rects[wide].width, 140.0f);
}

static void order_moves_items_without_moving_them_in_the_tree(void) {
    fixture f;
    const uint32_t root = begin_box(&f, 300.0f, 100.0f);

    const int32_t orders[3] = {2, 0, 1};
    for (int32_t i = 0; i < 3; ++i) {
        cf_style child = cf_style_default();
        child.width = CF_PX(0.0f);
        child.flex_grow = 1.0f;
        child.order = orders[i];
        append(&f, root, child);
    }
    arrange_in(&f, 300.0f, 100.0f);

    CF_NEAR(f.rects[1].x, 200.0f);
    CF_NEAR(f.rects[2].x, 0.0f);
    CF_NEAR(f.rects[3].x, 100.0f);
}

static void a_measure_callback_sizes_content_the_engine_cannot_see(void) {
    foreign_content text = {
        .min_content = {.width = 20.0f, .height = 11.0f},
        .max_content = {.width = 37.0f, .height = 11.0f},
    };

    fixture f;
    begin(&f, cf_style_default());
    append(&f, 0u, cf_style_default());
    f.measures[1] = measure_foreign;
    f.measure_data[1] = &text;

    arrange_with(&scratch, &f, (cf_available){.width = CF_PX(200.0f), .height = CF_AUTO});

    /* A node that declares nothing hugs its content instead of filling the
     * width it was offered, and the offer only caps it when content is wider. */
    CF_NEAR(f.rects[0].width, 37.0f);
    CF_NEAR(f.rects[0].height, 11.0f);
    CF_NEAR(f.rects[1].x, 0.0f);
    CF_NEAR(f.rects[1].width, 37.0f);
    CF_NEAR(f.rects[1].height, 11.0f);
}

/* A callback that asks nothing of its caller, so a tree can be handed to it
 * without a measure_data table. */
static cf_size measure_icon(void *user_data, cf_available available, cf_intrinsic kind) {
    (void)user_data;
    (void)available;
    (void)kind;

    return (cf_size){.width = 32.0f, .height = 32.0f};
}

static void a_tree_that_carries_no_measure_tables_still_lays_out(void) {
    const cf_available offer = {.width = CF_PX(200.0f), .height = CF_AUTO};

    fixture f;
    begin(&f, cf_style_default());
    append(&f, 0u, cf_style_default());
    f.measures[1] = measure_icon;

    cf_tree tree = tree_of(&f);
    tree.measure_data = NULL;
    cf_layout(&scratch, &tree, 0u, offer, (cf_point){0.0f, 0.0f}, f.rects);
    CF_NEAR(f.rects[1].width, 32.0f);

    /* A caller that measures nothing carries neither array, which is the shape
     * a leaf-only tree has: the callback lives in the table that is gone. */
    tree.measures = NULL;
    cf_layout(&scratch, &tree, 0u, offer, (cf_point){0.0f, 0.0f}, f.rects);
    CF_NEAR(f.rects[0].width, 0.0f);
    CF_NEAR(f.rects[1].width, 0.0f);
}

static void padding_and_border_inset_the_content_box(void) {
    fixture f;
    const uint32_t root = begin_box(&f, 200.0f, 100.0f);

    cf_style panel = cf_style_default();
    panel.width = CF_PX(100.0f);
    panel.padding = (cf_box){
        .left = CF_PX(10.0f), .top = CF_PX(10.0f), .right = CF_PX(10.0f), .bottom = CF_PX(10.0f)};
    panel.border = (cf_box){
        .left = CF_PX(1.0f), .top = CF_PX(1.0f), .right = CF_PX(1.0f), .bottom = CF_PX(1.0f)};
    const uint32_t framed = append(&f, root, panel);

    append(&f, framed, fixed_box(50.0f, 20.0f));
    arrange_in(&f, 200.0f, 100.0f);

    /* A declared width is the border box: the insets sit inside it, and the
     * child starts 11 in from the panel's edge. */
    CF_NEAR(f.rects[framed].width, 100.0f);
    CF_NEAR(f.rects[framed].height, 100.0f);
    CF_NEAR(f.rects[2].x, 11.0f);
    CF_NEAR(f.rects[2].y, 11.0f);
    CF_NEAR(f.rects[2].width, 50.0f);
    CF_NEAR(f.rects[2].height, 20.0f);
}

static void a_declared_main_size_still_measures_its_cross_from_the_content(void) {
    fixture f;
    const uint32_t root = begin_box(&f, 200.0f, 100.0f);
    f.styles[root].align_items = CF_ALIGN_START;

    cf_style panel = cf_style_default();
    panel.width = CF_PX(60.0f);
    const uint32_t framed = append(&f, root, panel);
    append(&f, framed, fixed_box(40.0f, 30.0f));
    arrange_in(&f, 200.0f, 100.0f);

    /* The panel declared its main size and left the other axis to its content,
     * which still has to be measured before the line it joins has a size
     * across. Nothing stretches here, so the 30 is the content's own answer. */
    CF_NEAR(f.rects[framed].width, 60.0f);
    CF_NEAR(f.rects[framed].height, 30.0f);
}

static void an_auto_box_keeps_the_width_its_content_cannot_break(void) {
    fixture f;
    const uint32_t root = begin_box(&f, 100.0f, 100.0f);

    const uint32_t unwrappable = append(&f, root, cf_style_default());
    append(&f, unwrappable, fixed_box(140.0f, 20.0f));
    arrange_in(&f, 100.0f, 100.0f);

    /* Shrink-to-fit caps the box at the width it was offered, but never below
     * what the content cannot break: the 140 child overflows instead of being
     * clipped, and the automatic minimum size keeps it there. */
    CF_NEAR(f.rects[unwrappable].width, 140.0f);
    CF_NEAR(f.rects[unwrappable].x, 0.0f);
}

static void a_gap_sits_between_items_only(void) {
    fixture f;
    const uint32_t root = begin_box(&f, 300.0f, 100.0f);
    f.styles[root].column_gap = CF_PX(10.0f);

    for (int32_t i = 0; i < 3; ++i) {
        cf_style child = cf_style_default();
        child.width = CF_PX(100.0f);
        child.flex_shrink = 0.0f;
        append(&f, root, child);
    }
    arrange_in(&f, 300.0f, 100.0f);

    /* Nothing shrinks and nothing grows, so the line is 320 wide in a 300 box
     * and the gap counts twice, never at the ends. */
    CF_NEAR(f.rects[1].x, 0.0f);
    CF_NEAR(f.rects[2].x, 110.0f);
    CF_NEAR(f.rects[3].x, 220.0f);
    CF_NEAR(f.rects[3].width, 100.0f);
}

static void refused_scratch_leaves_the_subtree_as_it_was(void) {
    fixture f;
    const uint32_t root = begin_box(&f, 300.0f, 100.0f);
    append(&f, root, fixed_box(100.0f, 0.0f));
    append(&f, root, fixed_box(100.0f, 0.0f));

    const cf_rect untouched = {.x = 7.0f, .y = 7.0f, .width = 7.0f, .height = 7.0f};
    f.rects[1] = untouched;
    f.rects[2] = untouched;

    cf_arena tiny;
    CF_CHECK(cf_arena_init(&tiny, 64u), "the test needs a scratch it can exhaust");
    arrange_with(&tiny, &f, (cf_available){.width = CF_PX(300.0f), .height = CF_PX(100.0f)});

    /* The node whose item table did not fit still gets its own rect; its
     * children keep whatever rect they had rather than being zeroed. */
    CF_NEAR(f.rects[0].width, 300.0f);
    CF_NEAR(f.rects[0].height, 100.0f);
    CF_NEAR(f.rects[1].x, 7.0f);
    CF_NEAR(f.rects[1].width, 7.0f);
    CF_NEAR(f.rects[2].x, 7.0f);

    cf_arena_free(&tiny);
}

static void a_tree_or_rect_array_that_is_not_there_changes_nothing(void) {
    fixture f;
    begin_box(&f, 100.0f, 100.0f);
    append(&f, 0u, fixed_box(50.0f, 50.0f));

    const cf_rect untouched = {.x = 3.0f, .y = 3.0f, .width = 3.0f, .height = 3.0f};
    f.rects[0] = untouched;

    cf_tree tree = tree_of(&f);
    cf_layout(&scratch, &tree, tree.node_count,
              (cf_available){.width = CF_PX(100.0f), .height = CF_PX(100.0f)},
              (cf_point){0.0f, 0.0f}, f.rects);
    CF_NEAR(f.rects[0].width, 3.0f);

    cf_layout(&scratch, NULL, 0u, (cf_available){CF_AUTO, CF_AUTO}, (cf_point){0.0f, 0.0f},
              f.rects);
    CF_NEAR(f.rects[0].width, 3.0f);
}

static void a_broken_line_starts_without_the_gap_it_crossed(void) {
    fixture f;
    cf_style root = cf_style_default();
    root.width = CF_PX(80.0f);
    root.wrap = CF_WRAP;
    root.column_gap = CF_PX(10.0f);
    root.row_gap = CF_PX(20.0f);
    begin(&f, root);

    for (int32_t i = 0; i < 9; ++i) {
        append(&f, 0u, fixed_box(20.0f, 20.0f));
    }
    arrange_in(&f, 80.0f, 100.0f);

    /* Three lines of three, exactly filling the box: the gap belongs between
     * items and between lines, never ahead of a line the break started. */
    CF_NEAR(f.rects[0].height, 100.0f);
    CF_NEAR(f.rects[3].x, 60.0f);
    CF_NEAR(f.rects[4].y, 40.0f);
    CF_NEAR(f.rects[6].x, 60.0f);
    CF_NEAR(f.rects[6].y, 40.0f);
    CF_NEAR(f.rects[7].y, 80.0f);
    CF_NEAR(f.rects[9].x, 60.0f);
    CF_NEAR(f.rects[9].y, 80.0f);
}

static void baseline_group_aligns_on_the_tallest_members_baseline(void) {
    fixture f;
    const uint32_t root = begin_box(&f, 100.0f, 100.0f);
    f.styles[root].align_items = CF_ALIGN_BASELINE;

    append(&f, root, fixed_box(50.0f, 50.0f));
    append(&f, root, fixed_box(50.0f, 20.0f));
    arrange_in(&f, 100.0f, 100.0f);

    /* Both hang from the shared baseline: the taller member's bottom edge is
     * the baseline, and the short one drops below it by the difference. */
    CF_NEAR(f.rects[1].y, 0.0f);
    CF_NEAR(f.rects[2].y, 30.0f);
}

static void a_column_hands_the_baseline_its_first_item_sits_at(void) {
    fixture f;
    const uint32_t root = begin_box(&f, 100.0f, 100.0f);
    f.styles[root].align_items = CF_ALIGN_BASELINE;

    append(&f, root, fixed_box(50.0f, 50.0f));
    const uint32_t column = append(&f, root, fixed_box(50.0f, 20.0f));
    append(&f, column, fixed_box(50.0f, 10.0f));
    arrange_in(&f, 100.0f, 100.0f);

    /* The column's baseline is where its first item's sits, so the column
     * drops until that baseline meets the leaf's bottom edge. */
    CF_NEAR(f.rects[1].y, 0.0f);
    CF_NEAR(f.rects[2].y, 40.0f);
    CF_NEAR(f.rects[3].y, 40.0f);
}

static void a_wrapped_baseline_line_keeps_the_groups_descent(void) {
    fixture f;
    const uint32_t root = begin_box(&f, 100.0f, 100.0f);
    f.styles[root].wrap = CF_WRAP;
    f.styles[root].align_items = CF_ALIGN_BASELINE;

    append(&f, root, fixed_box(50.0f, 50.0f));
    const uint32_t first = append(&f, root, fixed_box(50.0f, 20.0f));
    append(&f, first, fixed_box(50.0f, 10.0f));
    const uint32_t second = append(&f, root, fixed_box(50.0f, 20.0f));
    append(&f, second, fixed_box(50.0f, 10.0f));
    append(&f, root, fixed_box(50.0f, 50.0f));
    arrange_in(&f, 100.0f, 100.0f);

    /* The first line is 60 tall - the tall member's ascent plus the room the
     * column reaches below the baseline - so the second line starts there. */
    CF_NEAR(f.rects[1].y, 0.0f);
    CF_NEAR(f.rects[2].y, 40.0f);
    CF_NEAR(f.rects[3].y, 40.0f);
    CF_NEAR(f.rects[4].y, 100.0f);
    CF_NEAR(f.rects[5].y, 100.0f);
    CF_NEAR(f.rects[6].y, 60.0f);
}

static void baseline_group_counts_margins_into_the_ascent(void) {
    fixture f;
    const uint32_t root = begin_box(&f, 100.0f, 100.0f);
    f.styles[root].align_items = CF_ALIGN_BASELINE;

    const uint32_t framed = append(&f, root, fixed_box(50.0f, 50.0f));
    f.styles[framed].flex_shrink = 0.0f;
    f.styles[framed].margin = (cf_box){
        .left = CF_PX(5.0f), .top = CF_PX(5.0f), .right = CF_PX(5.0f), .bottom = CF_PX(5.0f)};
    const uint32_t column = append(&f, root, fixed_box(50.0f, 20.0f));
    const uint32_t inner = append(&f, column, fixed_box(50.0f, 10.0f));
    f.styles[inner].margin = (cf_box){
        .left = CF_PX(1.0f), .top = CF_PX(1.0f), .right = CF_PX(1.0f), .bottom = CF_PX(1.0f)};
    arrange_in(&f, 100.0f, 100.0f);

    /* The ascent is measured from the margin box, so the framed item's margin
     * lifts the whole group, and the column's inner margin rides inside the
     * baseline the column hands up. */
    CF_NEAR(f.rects[framed].x, 5.0f);
    CF_NEAR(f.rects[framed].y, 5.0f);
    CF_NEAR(f.rects[column].x, 60.0f);
    CF_NEAR(f.rects[column].y, 44.0f);
    CF_NEAR(f.rects[inner].x, 61.0f);
    CF_NEAR(f.rects[inner].y, 45.0f);
}

static void child_percentages_read_the_content_box_inside_the_frame(void) {
    fixture f;
    const uint32_t root = begin_box(&f, 200.0f, 200.0f);
    f.styles[root].direction = CF_DIRECTION_COLUMN;

    const uint32_t framed = append(&f, root, cf_style_default());
    f.styles[framed].width = CF_PERCENT(50);
    f.styles[framed].padding = (cf_box){
        .left = CF_PX(10.0f), .top = CF_PX(10.0f), .right = CF_PX(10.0f), .bottom = CF_PX(10.0f)};
    const uint32_t leaf = append(&f, framed, fixed_box(20.0f, 20.0f));
    f.styles[leaf].margin = (cf_box){.left = CF_PERCENT(5),
                                     .top = CF_PERCENT(5),
                                     .right = CF_PERCENT(5),
                                     .bottom = CF_PERCENT(5)};
    arrange_in(&f, 200.0f, 200.0f);

    /* The leaf's margins are percentages of the box they fill, so the frame
     * the container declares is taken off before the measure hands the box
     * down: the auto height grows inside the frame, never by it. */
    CF_NEAR(f.rects[framed].height, 48.0f);
    CF_NEAR(f.rects[leaf].x, 14.0f);
    CF_NEAR(f.rects[leaf].y, 14.0f);
}

int main(void) {
    printf("layout flex\n");
    CF_CHECK(cf_arena_init(&scratch, 4096u), "layout needs scratch");

    CF_RUN(free_space_splits_between_items_by_their_grow_factors);
    CF_RUN(items_shrink_by_their_weighted_base_sizes);
    CF_RUN(a_floored_item_hands_the_space_it_cannot_give_back);
    CF_RUN(an_item_clamped_up_by_its_minimum_gives_its_growth_to_the_rest);
    CF_RUN(grow_factors_below_one_leave_their_space_undistributed);
    CF_RUN(the_automatic_minimum_size_stops_a_shrink_at_the_content);
    CF_RUN(a_column_stacks_its_items_vertically);
    CF_RUN(wrap_breaks_between_items_at_the_container_edge);
    CF_RUN(a_broken_line_starts_without_the_gap_it_crossed);
    CF_RUN(justify_content_distributes_the_leftover_main_space);
    CF_RUN(an_auto_margin_claims_the_space_before_justify_content_does);
    CF_RUN(a_cross_end_auto_margin_leaves_the_item_at_cross_start);
    CF_RUN(an_item_wider_than_its_line_overflows_both_edges);
    CF_RUN(order_moves_items_without_moving_them_in_the_tree);
    CF_RUN(a_measure_callback_sizes_content_the_engine_cannot_see);
    CF_RUN(a_tree_that_carries_no_measure_tables_still_lays_out);
    CF_RUN(padding_and_border_inset_the_content_box);
    CF_RUN(a_declared_main_size_still_measures_its_cross_from_the_content);
    CF_RUN(child_percentages_read_the_content_box_inside_the_frame);
    CF_RUN(an_auto_box_keeps_the_width_its_content_cannot_break);
    CF_RUN(a_gap_sits_between_items_only);
    CF_RUN(baseline_group_aligns_on_the_tallest_members_baseline);
    CF_RUN(a_column_hands_the_baseline_its_first_item_sits_at);
    CF_RUN(a_wrapped_baseline_line_keeps_the_groups_descent);
    CF_RUN(baseline_group_counts_margins_into_the_ascent);
    CF_RUN(refused_scratch_leaves_the_subtree_as_it_was);
    CF_RUN(a_tree_or_rect_array_that_is_not_there_changes_nothing);

    cf_arena_free(&scratch);
    return CF_REPORT();
}
