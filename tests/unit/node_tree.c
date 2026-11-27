#include "cf/layout.h"
#include "cf/node.h"
#include "cform_test.h"

/*
 * Width markers tell the reached nodes apart: the view writes styles only
 * for the slots this frame claimed, and find_reached walks only what the
 * root reaches, so a retired slot's stale style is never mistaken for a
 * live node.
 */
static cf_node node_of(cf_name key, float width, const cf_node *children, uint32_t count) {
    cf_style style = cf_style_default();

    style.width = CF_PX(width);

    return (cf_node){.key = key, .style = style, .children = children, .child_count = count};
}

static uint32_t find_in(const cf_tree *tree, uint32_t slot, float width) {
    if (tree->styles[slot].width.kind == CF_LENGTH_PX && tree->styles[slot].width.value == width) {
        return slot;
    }

    for (uint32_t at = tree->child_start[slot]; at < tree->child_start[slot + 1u]; ++at) {
        const uint32_t found = find_in(tree, tree->child_slots[at], width);

        if (found != CF_NO_LINK) {
            return found;
        }
    }

    return CF_NO_LINK;
}

static uint32_t first_child(const cf_tree *tree, uint32_t slot) {
    if (tree->child_start[slot + 1u] == tree->child_start[slot]) {
        return CF_NO_LINK;
    }

    return tree->child_slots[tree->child_start[slot]];
}

static cf_size fixed_measure(void *user_data, cf_available available, cf_intrinsic kind) {
    (void)user_data;
    (void)available;
    (void)kind;
    return (cf_size){.width = 30.0f, .height = 20.0f};
}

/* Every case builds in its own context so a leak or a bad state in one does
 * not dress up as a diff bug in another. */
static bool context_of(cf_ctx *ctx) {
    if (!cf_ctx_init(ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return false;
    }

    return true;
}

static void a_declaration_becomes_a_tree(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    const cf_name alpha = cf_ctx_key(&ctx, "alpha");
    const cf_name beta = cf_ctx_key(&ctx, "beta");
    const cf_node leaves[] = {node_of(alpha, 10.0f, NULL, 0u), node_of(beta, 20.0f, NULL, 0u)};
    const cf_node root = node_of(CF_NO_NAME, 30.0f, leaves, 2u);

    cf_frame_begin(&ctx);

    const uint32_t root_slot = cf_diff(&ctx, &root);
    CF_CHECK(root_slot != CF_NO_LINK, "the root takes a slot");

    const cf_tree *tree = cf_frame_tree(&ctx);
    CF_CHECK(tree != NULL, "the diff answers a view");

    if (tree != NULL && root_slot != CF_NO_LINK) {
        CF_CHECK(tree->node_count == 3u, "three nodes are retained");
        CF_CHECK(tree->child_start[root_slot + 1u] - tree->child_start[root_slot] == 2u,
                 "the root reaches two children");

        const uint32_t first = first_child(tree, root_slot);
        const uint32_t second = tree->child_slots[tree->child_start[root_slot] + 1u];
        CF_CHECK(tree->styles[first].width.value == 10.0f, "the first child keeps its style");
        CF_CHECK(tree->styles[second].width.value == 20.0f, "the second child keeps its style");
    }

    cf_ctx_free(&ctx);
}

static void an_unchanged_declaration_keeps_its_slots(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    const cf_name alpha = cf_ctx_key(&ctx, "alpha");
    const cf_node leaves[] = {node_of(alpha, 10.0f, NULL, 0u)};
    const cf_node root = node_of(CF_NO_NAME, 30.0f, leaves, 1u);

    cf_frame_begin(&ctx);
    const uint32_t first_root = cf_diff(&ctx, &root);

    cf_frame_begin(&ctx);
    const uint32_t second_root = cf_diff(&ctx, &root);

    CF_CHECK(first_root == second_root, "the root keeps its slot");
    CF_CHECK(first_child(cf_frame_tree(&ctx), second_root) ==
                 first_child(cf_frame_tree(&ctx), first_root),
             "the child keeps its slot");

    cf_ctx_free(&ctx);
}

static void a_keyed_child_keeps_its_slot_when_siblings_swap(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    const cf_name alpha = cf_ctx_key(&ctx, "alpha");
    const cf_name beta = cf_ctx_key(&ctx, "beta");
    const cf_node before[] = {node_of(alpha, 10.0f, NULL, 0u), node_of(beta, 20.0f, NULL, 0u)};
    const cf_node after[] = {node_of(beta, 20.0f, NULL, 0u), node_of(alpha, 10.0f, NULL, 0u)};
    const cf_node first = node_of(CF_NO_NAME, 30.0f, before, 2u);
    const cf_node second = node_of(CF_NO_NAME, 30.0f, after, 2u);

    cf_frame_begin(&ctx);
    const uint32_t root = cf_diff(&ctx, &first);
    const cf_tree *tree = cf_frame_tree(&ctx);
    const uint32_t alpha_slot = find_in(tree, root, 10.0f);
    const uint32_t beta_slot = find_in(tree, root, 20.0f);

    cf_frame_begin(&ctx);
    const uint32_t rebuilt = cf_diff(&ctx, &second);
    tree = cf_frame_tree(&ctx);

    CF_CHECK(rebuilt == root, "the root keeps its slot");
    CF_CHECK(find_in(tree, rebuilt, 10.0f) == alpha_slot, "alpha keeps its slot");
    CF_CHECK(find_in(tree, rebuilt, 20.0f) == beta_slot, "beta keeps its slot");
    CF_CHECK(first_child(tree, rebuilt) == beta_slot, "beta now paints first");

    cf_ctx_free(&ctx);
}

static void a_keyed_child_keeps_its_slot_when_it_moves_to_another_parent(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    const cf_name left = cf_ctx_key(&ctx, "left");
    const cf_name right = cf_ctx_key(&ctx, "right");
    const cf_name moved = cf_ctx_key(&ctx, "moved");
    const cf_node under_left[] = {node_of(moved, 10.0f, NULL, 0u)};
    const cf_node under_right[] = {node_of(moved, 10.0f, NULL, 0u)};
    const cf_node before[] = {node_of(left, 30.0f, under_left, 1u),
                              node_of(right, 20.0f, NULL, 0u)};
    const cf_node after[] = {node_of(left, 30.0f, NULL, 0u),
                             node_of(right, 20.0f, under_right, 1u)};
    const cf_node first = node_of(CF_NO_NAME, 40.0f, before, 2u);
    const cf_node second = node_of(CF_NO_NAME, 40.0f, after, 2u);

    cf_frame_begin(&ctx);
    const uint32_t root = cf_diff(&ctx, &first);
    const uint32_t moved_slot = find_in(cf_frame_tree(&ctx), root, 10.0f);

    cf_frame_begin(&ctx);
    const uint32_t rebuilt = cf_diff(&ctx, &second);
    const cf_tree *tree = cf_frame_tree(&ctx);

    CF_CHECK(find_in(tree, rebuilt, 10.0f) == moved_slot, "the child keeps its slot");
    CF_CHECK(first_child(tree, find_in(tree, rebuilt, 20.0f)) == moved_slot,
             "it now hangs off the other parent");

    cf_ctx_free(&ctx);
}

static void a_vanished_node_frees_its_slot_for_the_next_one(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    const cf_name alpha = cf_ctx_key(&ctx, "alpha");
    const cf_node two[] = {node_of(alpha, 10.0f, NULL, 0u), node_of(CF_NO_NAME, 20.0f, NULL, 0u)};
    const cf_node one[] = {node_of(alpha, 10.0f, NULL, 0u)};
    const cf_node with_replacement[] = {node_of(alpha, 10.0f, NULL, 0u),
                                        node_of(CF_NO_NAME, 30.0f, NULL, 0u)};
    const cf_node first = node_of(CF_NO_NAME, 40.0f, two, 2u);
    const cf_node second = node_of(CF_NO_NAME, 40.0f, one, 1u);
    const cf_node third = node_of(CF_NO_NAME, 40.0f, with_replacement, 2u);

    cf_frame_begin(&ctx);
    const uint32_t root = cf_diff(&ctx, &first);
    const uint32_t alpha_slot = find_in(cf_frame_tree(&ctx), root, 10.0f);

    cf_frame_begin(&ctx);
    cf_diff(&ctx, &second);

    cf_frame_begin(&ctx);
    const uint32_t rebuilt = cf_diff(&ctx, &third);
    const cf_tree *tree = cf_frame_tree(&ctx);

    CF_CHECK(find_in(tree, rebuilt, 10.0f) == alpha_slot, "alpha keeps its slot");
    const uint32_t replacement = find_in(tree, rebuilt, 30.0f);
    CF_CHECK(replacement != CF_NO_LINK && replacement != alpha_slot,
             "the replacement takes a slot of its own");

    cf_ctx_free(&ctx);
}

static void a_keyless_child_is_reidentified_by_its_ordinal(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    const cf_node before[] = {node_of(CF_NO_NAME, 10.0f, NULL, 0u),
                              node_of(CF_NO_NAME, 20.0f, NULL, 0u)};
    const cf_node after[] = {node_of(CF_NO_NAME, 20.0f, NULL, 0u),
                             node_of(CF_NO_NAME, 10.0f, NULL, 0u)};
    const cf_node first = node_of(CF_NO_NAME, 30.0f, before, 2u);
    const cf_node second = node_of(CF_NO_NAME, 30.0f, after, 2u);

    cf_frame_begin(&ctx);
    const uint32_t root = cf_diff(&ctx, &first);
    const uint32_t lead_slot = find_in(cf_frame_tree(&ctx), root, 10.0f);

    cf_frame_begin(&ctx);
    const uint32_t rebuilt = cf_diff(&ctx, &second);

    CF_CHECK(rebuilt == root, "the keyless root keeps its slot");
    CF_CHECK(find_in(cf_frame_tree(&ctx), rebuilt, 20.0f) == lead_slot,
             "the child at the ordinal keeps the slot, whatever it holds now");

    cf_ctx_free(&ctx);
}

static void a_duplicate_key_becomes_a_second_node(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    const cf_name shared = cf_ctx_key(&ctx, "shared");
    const cf_node children[] = {node_of(shared, 10.0f, NULL, 0u), node_of(shared, 20.0f, NULL, 0u)};
    const cf_node root = node_of(CF_NO_NAME, 30.0f, children, 2u);

    cf_frame_begin(&ctx);
    const uint32_t root_slot = cf_diff(&ctx, &root);
    const cf_tree *tree = cf_frame_tree(&ctx);

    const uint32_t first = find_in(tree, root_slot, 10.0f);
    const uint32_t second = find_in(tree, root_slot, 20.0f);
    CF_CHECK(first != CF_NO_LINK && second != CF_NO_LINK && first != second,
             "both declarations are retained, each with its own slot");

    cf_ctx_free(&ctx);
}

static void a_deep_chain_wires_every_link(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    cf_node chain[5];

    for (uint32_t i = 5u; i-- > 0u;) {
        chain[i] = node_of(CF_NO_NAME, (float)(i + 1u) * 10.0f, i + 1u < 5u ? &chain[i + 1u] : NULL,
                           i + 1u < 5u ? 1u : 0u);
    }

    const cf_node root = node_of(CF_NO_NAME, 60.0f, chain, 1u);

    cf_frame_begin(&ctx);
    const uint32_t root_slot = cf_diff(&ctx, &root);
    const cf_tree *tree = cf_frame_tree(&ctx);

    uint32_t at = root_slot;
    uint32_t depth = 0u;

    while (at != CF_NO_LINK) {
        depth++;
        at = first_child(tree, at);
    }

    CF_CHECK(depth == 6u, "the chain reaches six nodes");

    cf_ctx_free(&ctx);
}

static void an_undeclared_frame_yields_no_view(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    const cf_name alpha = cf_ctx_key(&ctx, "alpha");
    const cf_node children[] = {node_of(alpha, 10.0f, NULL, 0u)};
    const cf_node root = node_of(CF_NO_NAME, 30.0f, children, 1u);

    cf_frame_begin(&ctx);
    cf_diff(&ctx, &root);

    CF_CHECK(cf_diff(&ctx, NULL) == CF_NO_LINK, "an empty declaration declares nothing");
    CF_CHECK(cf_frame_tree(&ctx) == NULL, "an empty frame yields no view");

    cf_frame_begin(&ctx);
    const uint32_t rebuilt = cf_diff(&ctx, &root);
    const cf_tree *tree = cf_frame_tree(&ctx);

    CF_CHECK(rebuilt != CF_NO_LINK && find_in(tree, rebuilt, 10.0f) != CF_NO_LINK,
             "the tree rebuilds after an empty frame");

    cf_ctx_free(&ctx);
}

static void layout_reads_the_diffed_tree(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    const cf_name box = cf_ctx_key(&ctx, "box");
    cf_node children[] = {node_of(box, 10.0f, NULL, 0u)};
    children[0].measure = fixed_measure;
    /* The measure answers only an auto axis; a declared width settles first. */
    children[0].style.width = CF_AUTO;

    cf_node root = node_of(CF_NO_NAME, 100.0f, children, 1u);
    root.style.direction = CF_DIRECTION_COLUMN;
    /* Auto cross sizes keep their measured extent only without the stretch. */
    root.style.align_items = CF_ALIGN_START;

    cf_frame_begin(&ctx);
    const uint32_t root_slot = cf_diff(&ctx, &root);
    const cf_tree *tree = cf_frame_tree(&ctx);

    if (tree != NULL && root_slot != CF_NO_LINK) {
        cf_rect rects[8];

        cf_layout(&ctx.frame, tree, root_slot,
                  (cf_available){.width = CF_PX(100.0f), .height = CF_AUTO}, (cf_point){0.0f, 0.0f},
                  rects);

        const uint32_t child = first_child(tree, root_slot);
        CF_NEAR(rects[child].width, 30.0f); /* what the measure answered */
        CF_NEAR(rects[child].height, 20.0f);
        CF_NEAR(rects[root_slot].height, 20.0f); /* the column hugs it */
    }

    cf_ctx_free(&ctx);
}

int main(void) {
    printf("node tree\n");

    CF_RUN(a_declaration_becomes_a_tree);
    CF_RUN(an_unchanged_declaration_keeps_its_slots);
    CF_RUN(a_keyed_child_keeps_its_slot_when_siblings_swap);
    CF_RUN(a_keyed_child_keeps_its_slot_when_it_moves_to_another_parent);
    CF_RUN(a_vanished_node_frees_its_slot_for_the_next_one);
    CF_RUN(a_keyless_child_is_reidentified_by_its_ordinal);
    CF_RUN(a_duplicate_key_becomes_a_second_node);
    CF_RUN(a_deep_chain_wires_every_link);
    CF_RUN(an_undeclared_frame_yields_no_view);
    CF_RUN(layout_reads_the_diffed_tree);

    return CF_REPORT();
}
