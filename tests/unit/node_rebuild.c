#include <string.h>

#include "cf/node.h"
#include "cform_test.h"

/*
 * Random rebuilds drive the diff through shapes no hand-written case thinks
 * of. The generator is deterministic - the same seed rebuilds the same
 * sequence of declarations - so a failure names its seed and frame. Every
 * generated node carries a unique width marker, and the CSR walk from the
 * root visits slots in declaration preorder, so pairing the two walks
 * catches a slot that kept another node's style.
 */
#define MAX_NODES 256u
#define MAX_DEPTH 6u
#define FRAMES 24u

typedef struct gen_tree {
    /* Every parent's children are one contiguous run of values, which is
     * what a cf_node declaration requires: the run is claimed for a parent
     * before its children recurse into runs beyond it, so a grandchild's
     * subtree never interleaves with a sibling. */
    cf_node storage[MAX_NODES];
    uint32_t used;
    uint32_t next_marker;
} gen_tree;

static uint64_t rng_next(uint64_t *state) {
    *state = *state * 6364136223846793005ULL + 1442695040888963407ULL;
    return *state >> 33;
}

static uint32_t rng_below(uint64_t *state, uint32_t bound) {
    return (uint32_t)(rng_next(state) % bound);
}

/* Fills one node and the subtree under it. */
static void gen_into(cf_node *node, gen_tree *tree, uint64_t *rng, uint32_t depth,
                     const cf_name *keys, uint32_t key_count) {
    *node = (cf_node){.style = cf_style_default()};
    node->style.width = CF_PX((float)tree->next_marker++);

    if (key_count > 0u && rng_below(rng, 100u) < 60u) {
        node->key = keys[rng_below(rng, key_count)];
    }

    if (depth + 1u >= MAX_DEPTH) {
        return;
    }

    uint32_t wanted = rng_below(rng, 4u);

    if (tree->used + wanted > MAX_NODES) {
        wanted = MAX_NODES - tree->used;
    }

    if (wanted == 0u) {
        return;
    }

    cf_node *run = &tree->storage[tree->used];
    tree->used += wanted;

    for (uint32_t i = 0u; i < wanted; ++i) {
        gen_into(&run[i], tree, rng, depth + 1u, keys, key_count);
    }

    node->children = run;
    node->child_count = wanted;
}

static cf_node *gen_root(gen_tree *tree, uint64_t *rng, const cf_name *keys, uint32_t key_count) {
    if (tree->used >= MAX_NODES) {
        return NULL;
    }

    cf_node *root = &tree->storage[tree->used];
    tree->used++;

    gen_into(root, tree, rng, 0u, keys, key_count);
    return root;
}

static uint32_t slot_walk(const cf_tree *tree, uint32_t slot, uint32_t *order, uint32_t at) {
    order[at++] = slot;

    for (uint32_t i = tree->child_start[slot]; i < tree->child_start[slot + 1u]; ++i) {
        at = slot_walk(tree, tree->child_slots[i], order, at);
    }

    return at;
}

static uint32_t decl_walk(const cf_node *node, const cf_node **order, uint32_t at) {
    order[at++] = node;

    for (uint32_t i = 0u; i < node->child_count; ++i) {
        at = decl_walk(&node->children[i], order, at);
    }

    return at;
}

static bool check_frame(cf_ctx *ctx, const cf_tree *tree, uint32_t root_slot,
                        const gen_tree *generated, const cf_node *root_decl,
                        unsigned long long seed, uint32_t frame, uint32_t *out_order,
                        uint32_t *out_visited) {
    if (ctx->nodes.alive != generated->used) {
        printf("  seed %llu frame %u: the store holds %zu nodes for %u declared\n", seed, frame,
               ctx->nodes.alive, generated->used);
        CF_CHECK(false, "retirement leaves the store holding exactly the declaration");
        return false;
    }

    const uint32_t total = tree->child_start[tree->node_count];

    if (total != generated->used - 1u) {
        printf("  seed %llu frame %u: the child rows hold %u edges for %u declared\n", seed, frame,
               total, generated->used);
        CF_CHECK(false, "the child rows encode the declared tree");
        return false;
    }

    for (uint32_t slot = 0u; slot < tree->node_count; ++slot) {
        if (tree->child_start[slot + 1u] < tree->child_start[slot]) {
            printf("  seed %llu frame %u: slot %u's row runs backwards\n", seed, frame, slot);
            CF_CHECK(false, "the child rows encode the declared tree");
            return false;
        }
    }

    for (uint32_t i = 0u; i < total; ++i) {
        if (tree->child_slots[i] >= tree->node_count) {
            printf("  seed %llu frame %u: edge %u points outside the store\n", seed, frame, i);
            CF_CHECK(false, "the child rows encode the declared tree");
            return false;
        }
    }

    const cf_node *decl_order[MAX_NODES];
    const uint32_t declared = decl_walk(root_decl, decl_order, 0u);
    *out_visited = slot_walk(tree, root_slot, out_order, 0u);

    if (declared != generated->used || *out_visited != generated->used) {
        printf("  seed %llu frame %u: the walk reaches %u of %u declared\n", seed, frame,
               *out_visited, generated->used);
        CF_CHECK(false, "the view reaches the whole declaration");
        return false;
    }

    for (uint32_t i = 0u; i < *out_visited; ++i) {
        const cf_length width = tree->styles[out_order[i]].width;

        if (width.kind != CF_LENGTH_PX || width.value != decl_order[i]->style.width.value) {
            printf("  seed %llu frame %u: preorder %u carries width %f, declared %f\n", seed, frame,
                   i, width.value, decl_order[i]->style.width.value);
            CF_CHECK(false, "a slot carries the style of the node at its preorder position");
            return false;
        }
    }

    return true;
}

static bool drive_frames(cf_ctx *ctx, unsigned long long seed, uint32_t same_percent,
                         uint32_t empty_percent) {
    const cf_name keys[] = {
        cf_intern_text(&ctx->names, "alpha"), cf_intern_text(&ctx->names, "beta"),
        cf_intern_text(&ctx->names, "gamma"), cf_intern_text(&ctx->names, "delta"),
        cf_intern_text(&ctx->names, "epsilon")};
    gen_tree generated = {0};
    uint64_t rng = seed;

    uint32_t previous[MAX_NODES];
    uint32_t previous_count = 0u;
    bool previous_valid = false;
    const cf_node *kept_root = NULL;

    for (uint32_t frame = 0u; frame < FRAMES; ++frame) {
        cf_frame_begin(ctx);

        const bool declare_nothing = rng_below(&rng, 100u) < empty_percent;
        const bool same_as_last =
            !declare_nothing && previous_valid && rng_below(&rng, 100u) < same_percent;

        if (declare_nothing) {
            if (cf_diff(ctx, NULL) != CF_NO_LINK || cf_frame_tree(ctx) != NULL ||
                ctx->nodes.alive != 0u) {
                printf("  seed %llu frame %u: an empty declaration left something behind\n", seed,
                       frame);
                CF_CHECK(false, "an empty declaration empties the tree");
                return false;
            }

            previous_valid = false;
            continue;
        }

        const cf_node *root;

        if (same_as_last) {
            root = kept_root;
        } else {
            generated.used = 0u;
            generated.next_marker = 1u;
            root = gen_root(&generated, &rng, keys, 5u);

            if (root == NULL) {
                printf("  seed %llu frame %u: the generator built nothing\n", seed, frame);
                CF_CHECK(false, "the generator builds a tree");
                return false;
            }
        }

        const uint32_t root_slot = cf_diff(ctx, root);
        const cf_tree *tree = cf_frame_tree(ctx);

        if (root_slot == CF_NO_LINK || tree == NULL) {
            printf("  seed %llu frame %u: the diff returned no view\n", seed, frame);
            CF_CHECK(false, "the diff answers every generated tree");
            return false;
        }

        uint32_t current[MAX_NODES];
        uint32_t visited = 0u;

        if (!check_frame(ctx, tree, root_slot, &generated, root, seed, frame, current, &visited)) {
            return false;
        }

        if (same_as_last) {
            if (visited != previous_count) {
                printf("  seed %llu frame %u: %u slots where the last rebuild had %u\n", seed,
                       frame, visited, previous_count);
                CF_CHECK(false, "an unchanged declaration keeps every slot");
                return false;
            }

            for (uint32_t i = 0u; i < visited; ++i) {
                if (current[i] != previous[i]) {
                    printf("  seed %llu frame %u: preorder %u moved slots\n", seed, frame, i);
                    CF_CHECK(false, "an unchanged declaration keeps every slot");
                    return false;
                }
            }
        }

        memcpy(previous, current, visited * sizeof *current);
        previous_count = visited;
        previous_valid = true;
        kept_root = root;
    }

    return true;
}

static bool context_of(cf_ctx *ctx) {
    if (!cf_ctx_init(ctx, 64u * 1024u)) {
        CF_CHECK(false, "the context starts");
        return false;
    }

    return true;
}

static void unchanged_rebuilds_keep_every_slot(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    /* Every frame after the first declares the same tree, so the whole run
     * is one long stability check. */
    drive_frames(&ctx, 1u, 100u, 0u);
    cf_ctx_free(&ctx);
}

static void changed_rebuilds_retire_and_reclaim_cleanly(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    /* Every frame declares a fresh tree: retirement, slot reuse and keyed
     * matches across generations churn without a stable frame in between. */
    drive_frames(&ctx, 2u, 0u, 0u);
    cf_ctx_free(&ctx);
}

static void mixed_rebuilds_stay_consistent(void) {
    cf_ctx ctx;

    if (!context_of(&ctx)) {
        return;
    }

    /* Same trees, fresh trees and empty frames interleave, the pattern a
     * real interface produces. */
    drive_frames(&ctx, 0x9e3779b97f4a7c15ULL, 40u, 15u);
    cf_ctx_free(&ctx);
}

int main(void) {
    printf("node rebuild\n");

    CF_RUN(unchanged_rebuilds_keep_every_slot);
    CF_RUN(changed_rebuilds_retire_and_reclaim_cleanly);
    CF_RUN(mixed_rebuilds_stay_consistent);

    return CF_REPORT();
}
