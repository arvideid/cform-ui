#include "cf/node.h"
#include "cform_test.h"

/* The dirty bit answers one question per frame: did this node's declaration
 * move? Every case drives two or more frames through one context, because
 * the bit only means something against the frame before it. */
static bool context_of(cf_ctx *ctx) {
    if (!cf_ctx_init(ctx, 64u * 1024u, 4u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return false;
    }

    return true;
}

static cf_node box(float width, float height) {
    cf_node node = {.style = cf_style_default()};
    node.style.width = CF_PX(width);
    node.style.height = CF_PX(height);
    return node;
}

static uint32_t child_slot(const cf_ctx *ctx, uint32_t index) {
    const cf_tree *tree = cf_frame_tree(ctx);

    if (tree == NULL || ctx->root_slot == CF_NO_LINK) {
        return CF_NO_LINK;
    }

    return tree->child_slots[tree->child_start[ctx->root_slot] + index];
}

static void a_fresh_slot_is_dirty(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    cf_node children[] = {box(50.0f, 50.0f)};
    cf_node root = box(200.0f, 200.0f);
    root.children = children;
    root.child_count = 1u;

    cf_frame_begin(&ctx);
    cf_diff(&ctx, &root);

    CF_CHECK(cf_dirty(&ctx, ctx.root_slot), "the root just joined");
    CF_CHECK(cf_dirty(&ctx, child_slot(&ctx, 0u)), "the child just joined");
    CF_CHECK(!cf_dirty(&ctx, CF_NO_LINK), "a slot that names no node is not dirty");

    cf_ctx_free(&ctx);
}

static void an_unchanged_declaration_is_not_dirty(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    cf_node children[] = {box(50.0f, 50.0f)};
    cf_node root = box(200.0f, 200.0f);
    root.children = children;
    root.child_count = 1u;

    cf_frame_begin(&ctx);
    cf_diff(&ctx, &root);

    cf_frame_begin(&ctx);
    cf_diff(&ctx, &root);

    CF_CHECK(!cf_dirty(&ctx, ctx.root_slot), "the same root is clean");
    CF_CHECK(!cf_dirty(&ctx, child_slot(&ctx, 0u)), "the same child is clean");

    cf_ctx_free(&ctx);
}

static void a_changed_style_dirties_only_what_changed(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    cf_node children[] = {box(50.0f, 50.0f), box(60.0f, 50.0f)};
    cf_node root = box(200.0f, 200.0f);
    root.children = children;
    root.child_count = 2u;

    cf_frame_begin(&ctx);
    cf_diff(&ctx, &root);

    const uint32_t first = child_slot(&ctx, 0u);
    const uint32_t second = child_slot(&ctx, 1u);

    children[0] = box(70.0f, 50.0f);

    cf_frame_begin(&ctx);
    cf_diff(&ctx, &root);

    CF_CHECK(cf_dirty(&ctx, first), "the widened child changed");
    CF_CHECK(!cf_dirty(&ctx, second), "the child beside it kept its declaration");
    CF_CHECK(!cf_dirty(&ctx, ctx.root_slot), "the container kept its own declaration");

    cf_ctx_free(&ctx);
}

static void a_changed_child_count_dirties_the_parent(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    cf_node first_children[] = {box(50.0f, 50.0f)};
    cf_node grown_children[] = {box(50.0f, 50.0f), box(60.0f, 50.0f)};
    cf_node root = box(200.0f, 200.0f);
    root.children = first_children;
    root.child_count = 1u;

    cf_frame_begin(&ctx);
    cf_diff(&ctx, &root);

    const uint32_t kept = child_slot(&ctx, 0u);

    root.children = grown_children;
    root.child_count = 2u;

    cf_frame_begin(&ctx);
    cf_diff(&ctx, &root);

    CF_CHECK(cf_dirty(&ctx, ctx.root_slot), "the container's child list changed shape");
    CF_CHECK(!cf_dirty(&ctx, kept), "the child that stayed keeps its slot clean");
    CF_CHECK(cf_dirty(&ctx, child_slot(&ctx, 1u)), "the child that joined is dirty");

    cf_ctx_free(&ctx);
}

static void a_subtree_mark_reaches_every_descendant(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    cf_node grandchildren[] = {box(20.0f, 20.0f)};
    cf_node child = box(50.0f, 50.0f);
    child.children = grandchildren;
    child.child_count = 1u;
    cf_node children[] = {child};
    cf_node root = box(200.0f, 200.0f);
    root.children = children;
    root.child_count = 1u;

    cf_frame_begin(&ctx);
    cf_diff(&ctx, &root);

    /* The first frame dirtied everything by arriving; the second leaves the
     * tree clean so the mark is the only dirt left. */
    cf_frame_begin(&ctx);
    cf_diff(&ctx, &root);

    const uint32_t child_slot_now = child_slot(&ctx, 0u);
    const cf_tree *tree = cf_frame_tree(&ctx);
    const uint32_t grandchild = tree->child_slots[tree->child_start[child_slot_now]];

    CF_CHECK(!cf_dirty(&ctx, child_slot_now) && !cf_dirty(&ctx, grandchild) &&
                 !cf_dirty(&ctx, ctx.root_slot),
             "an unchanged second frame reads clean");

    cf_dirty_subtree(&ctx, child_slot_now);

    CF_CHECK(cf_dirty(&ctx, child_slot_now), "the mark reaches the node it starts at");
    CF_CHECK(cf_dirty(&ctx, grandchild), "the mark walks down the subtree");
    CF_CHECK(!cf_dirty(&ctx, ctx.root_slot), "the mark stops at the subtree's root");

    cf_ctx_free(&ctx);
}

static void the_bit_lives_one_frame(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    cf_node root = box(200.0f, 200.0f);

    cf_frame_begin(&ctx);
    cf_diff(&ctx, &root);

    CF_CHECK(cf_dirty(&ctx, ctx.root_slot), "the first frame arrived dirty");

    cf_frame_begin(&ctx);

    CF_CHECK(!cf_dirty(&ctx, ctx.root_slot), "the next begin cleared it");

    cf_ctx_free(&ctx);
}

static void a_mark_made_before_the_diff_survives_an_unchanged_declaration(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    cf_node children[] = {box(50.0f, 50.0f)};
    cf_node root = box(200.0f, 200.0f);
    root.children = children;
    root.child_count = 1u;

    cf_frame_begin(&ctx);
    cf_diff(&ctx, &root);

    cf_frame_begin(&ctx);
    cf_dirty_subtree(&ctx, ctx.root_slot);
    cf_diff(&ctx, &root);

    CF_CHECK(cf_dirty(&ctx, ctx.root_slot), "the mark outlives the unchanged claim");
    CF_CHECK(cf_dirty(&ctx, child_slot(&ctx, 0u)), "the mark reached the child before the diff");

    cf_ctx_free(&ctx);
}

int main(void) {
    printf("node dirty\n");

    CF_RUN(a_fresh_slot_is_dirty);
    CF_RUN(an_unchanged_declaration_is_not_dirty);
    CF_RUN(a_changed_style_dirties_only_what_changed);
    CF_RUN(a_changed_child_count_dirties_the_parent);
    CF_RUN(a_subtree_mark_reaches_every_descendant);
    CF_RUN(the_bit_lives_one_frame);
    CF_RUN(a_mark_made_before_the_diff_survives_an_unchanged_declaration);

    return CF_REPORT();
}
