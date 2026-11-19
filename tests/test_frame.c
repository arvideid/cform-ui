#include "cform_test.h"

#include <cform/cform.h>
#include "internal.h"

static void context_create_returns_valid_defaults(void) {
    UI_Context *ctx = ui_create();

    CF_CHECK(ctx != NULL, "ui_create returns a context");
    CF_NEAR(ctx->frame_count, 0);

    ui_destroy(ctx);
}

static void frame_copies_config_by_value(void) {
    UI_Context *ctx = ui_create();

    UI_Frame *f = ui_frame(ctx, (UI_FrameConfig){
        .width = UI_FIXED(30), .height = UI_FILL });

    CF_CHECK(f != NULL, "ui_frame returns a frame");
    CF_NEAR(f->config.width.value, 30);
    CF_CHECK(f->config.width.type == UI_SIZE_FIXED, "width is fixed");
    CF_CHECK(f->config.height.type == UI_SIZE_FILL, "height is fill");
    CF_NEAR(f->child_count, 0);
    CF_CHECK(f->parent == NULL, "detached frame has no parent");

    ui_destroy(ctx);
}

static void add_links_child_to_parent(void) {
    UI_Context *ctx = ui_create();

    UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){0});
    UI_Frame *a = ui_frame(ctx, (UI_FrameConfig){0});
    UI_Frame *b = ui_frame(ctx, (UI_FrameConfig){0});

    ui_add(root, a);
    ui_add(root, b);

    CF_NEAR(root->child_count, 2);
    CF_CHECK(root->children[0] == a, "first child is a");
    CF_CHECK(root->children[1] == b, "second child is b");
    CF_CHECK(a->parent == root, "a's parent is root");
    CF_CHECK(b->parent == root, "b's parent is root");

    ui_destroy(ctx);
}

static void children_array_grows_by_doubling(void) {
    UI_Context *ctx = ui_create();

    UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){0});

    /* More children than the initial capacity of 4, to exercise the growth. */
    for (int i = 0; i < 10; i++) {
        ui_add(root, ui_frame(ctx, (UI_FrameConfig){0}));
    }

    CF_NEAR(root->child_count, 10);

    ui_destroy(ctx);
}

static void context_registers_every_frame(void) {
    UI_Context *ctx = ui_create();

    for (int i = 0; i < 20; i++) {
        ui_frame(ctx, (UI_FrameConfig){0});
    }

    CF_NEAR(ctx->frame_count, 20);

    ui_destroy(ctx);
}

static void update_replaces_config_in_place(void) {
    UI_Context *ctx = ui_create();

    UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){0});
    UI_Frame *a = ui_frame(ctx, (UI_FrameConfig){
        .width = UI_FIXED(30) });
    ui_add(root, a);

    ui_update(a, (UI_FrameConfig){
        .width = UI_FILL, .height = UI_FIXED(80), .gap = 5.0f });

    CF_CHECK(a->config.width.type == UI_SIZE_FILL, "width switched to fill");
    CF_NEAR(a->config.height.value, 80);
    CF_NEAR(a->config.gap, 5);
    CF_NEAR(a->child_count, 0);
    CF_CHECK(a->parent == root, "update does not detach the frame");

    ui_destroy(ctx);
}

static void insert_shifts_later_siblings_right(void) {
    UI_Context *ctx = ui_create();

    UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){0});
    UI_Frame *a = ui_frame(ctx, (UI_FrameConfig){0});
    UI_Frame *b = ui_frame(ctx, (UI_FrameConfig){0});
    UI_Frame *x = ui_frame(ctx, (UI_FrameConfig){0});

    ui_add(root, a);
    ui_add(root, b);
    ui_insert(root, 1, x);

    CF_NEAR(root->child_count, 3);
    CF_CHECK(root->children[0] == a, "a stays first");
    CF_CHECK(root->children[1] == x, "x lands at index 1");
    CF_CHECK(root->children[2] == b, "b shifts to index 2");
    CF_CHECK(x->parent == root, "insert sets parent");

    ui_destroy(ctx);
}

static void insert_clamps_out_of_range_index(void) {
    UI_Context *ctx = ui_create();

    UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){0});
    UI_Frame *a = ui_frame(ctx, (UI_FrameConfig){0});
    UI_Frame *head = ui_frame(ctx, (UI_FrameConfig){0});
    UI_Frame *tail = ui_frame(ctx, (UI_FrameConfig){0});

    ui_add(root, a);
    ui_insert(root, -5, head);
    ui_insert(root, 999, tail);

    CF_CHECK(root->children[0] == head, "negative index prepends");
    CF_CHECK(root->children[2] == tail, "oversized index appends");

    /* Insert into an empty parent at any index behaves like add. */
    UI_Frame *empty = ui_frame(ctx, (UI_FrameConfig){0});
    UI_Frame *only = ui_frame(ctx, (UI_FrameConfig){0});
    ui_insert(empty, 42, only);
    CF_NEAR(empty->child_count, 1);
    CF_CHECK(empty->children[0] == only, "insert into empty parent");

    ui_destroy(ctx);
}

static void insert_grows_children_array_by_doubling(void) {
    UI_Context *ctx = ui_create();

    UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){0});

    /* Prepend five frames onto an initial capacity of 4 to force a realloc. */
    for (int i = 0; i < 5; i++) {
        ui_insert(root, 0, ui_frame(ctx, (UI_FrameConfig){
            .width = UI_FIXED((float)i) }));
    }

    CF_NEAR(root->child_count, 5);
    /* Last inserted was index 0, so iteration reads descending i. */
    for (int i = 0; i < 5; i++) {
        CF_NEAR(root->children[i]->config.width.value, 4 - i);
    }

    ui_destroy(ctx);
}

static void remove_detaches_child_without_freeing(void) {
    UI_Context *ctx = ui_create();

    UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){0});
    UI_Frame *a = ui_frame(ctx, (UI_FrameConfig){0});
    UI_Frame *b = ui_frame(ctx, (UI_FrameConfig){0});
    UI_Frame *c = ui_frame(ctx, (UI_FrameConfig){0});

    ui_add(root, a);
    ui_add(root, b);
    ui_add(root, c);

    ui_remove(root, b);

    CF_NEAR(root->child_count, 2);
    CF_CHECK(root->children[0] == a, "a unchanged");
    CF_CHECK(root->children[1] == c, "c shifts left to fill b's slot");
    CF_CHECK(b->parent == NULL, "removed child has no parent");

    /* The frame stays registered on the context and is still readable. */
    CF_NEAR(ctx->frame_count, 4);

    /* Removing a non-member is a silent no-op. */
    UI_Frame *stranger = ui_frame(ctx, (UI_FrameConfig){0});
    ui_remove(root, stranger);
    CF_NEAR(root->child_count, 2);
    CF_CHECK(stranger->parent == NULL, "stranger stays detached");

    /* Removing the tail preserves the NULL sentinel past child_count. */
    ui_remove(root, c);
    CF_NEAR(root->child_count, 1);
    CF_CHECK(root->children[1] == NULL, "tail slot is NULL");

    ui_destroy(ctx);
}

static void null_inputs_are_silent_no_ops(void) {
    CF_CHECK(ui_frame(NULL, (UI_FrameConfig){0}) == NULL,
             "ui_frame with NULL context returns NULL");

    UI_Context *ctx = ui_create();
    UI_Frame *root = ui_frame(ctx, (UI_FrameConfig){0});

    ui_add(NULL, NULL);          /* no crash */
    ui_add(root, NULL);
    CF_NEAR(root->child_count, 0);

    ui_insert(NULL, 0, NULL);
    ui_insert(root, 0, NULL);
    ui_remove(NULL, NULL);
    ui_remove(root, NULL);
    CF_NEAR(root->child_count, 0);

    ui_update(NULL, (UI_FrameConfig){0});  /* no crash */

    ui_destroy(ctx);
    ui_destroy(NULL);            /* no crash */
}

int main(void) {
    printf("test_frame\n");
    CF_RUN(context_create_returns_valid_defaults);
    CF_RUN(frame_copies_config_by_value);
    CF_RUN(add_links_child_to_parent);
    CF_RUN(children_array_grows_by_doubling);
    CF_RUN(context_registers_every_frame);
    CF_RUN(update_replaces_config_in_place);
    CF_RUN(insert_shifts_later_siblings_right);
    CF_RUN(insert_clamps_out_of_range_index);
    CF_RUN(insert_grows_children_array_by_doubling);
    CF_RUN(remove_detaches_child_without_freeing);
    CF_RUN(null_inputs_are_silent_no_ops);
    return CF_REPORT();
}
