#include "cf/build.h"
#include "cf/widget.h"

/* The bytes one scroller keeps for the dispatch it drives: the context to
 * read the rects the wheel walks between, and the name the offset block
 * lives under. They live in the frame arena beside the frame that declared
 * them. */
typedef struct scroll_data {
    cf_ctx *ctx;
    cf_name key;
} scroll_data;

/* One wheel notch walks forty-eight pixels, about three lines of text. */
#define WHEEL_NOTCH 48.0f

static uint32_t content_of(const cf_ctx *ctx, uint32_t viewport_slot) {
    const cf_tree *tree = cf_frame_tree(ctx);

    if (tree == NULL || viewport_slot == CF_NO_LINK) {
        return CF_NO_LINK;
    }

    return tree->child_slots[tree->child_start[viewport_slot]];
}

static void scroll_pointer(cf_event *event, void *user_data) {
    scroll_data *scroll = user_data;

    if (event->type != CF_EVENT_SCROLL || scroll->ctx->rects == NULL) {
        return;
    }

    cf_scroll *held = cf_scroll_of(scroll->ctx, scroll->key);
    const uint32_t content_slot = content_of(scroll->ctx, event->target);

    if (held == NULL || content_slot == CF_NO_LINK) {
        return;
    }

    /* The content's own rect is its whole height: the wrapper hugs what the
     * build declared, however little the viewport shows of it. */
    const float viewport_height = scroll->ctx->rects[event->target].height;
    const float content_height = scroll->ctx->rects[content_slot].height;

    const float walked = cf_scroll_clamp(held->offset + event->wheel.y * WHEEL_NOTCH,
                                         viewport_height, content_height);

    /* A pane that moves takes the notch and the tree hears nothing more of it.
     * One already at its end lets the notch climb on, so a scroll inside a
     * scroll chains to the one outside the way a reader expects instead of
     * running out at the first column that happens to be under the pointer. */
    if (walked != held->offset) {
        held->offset = walked;
        cf_stop(event);
    }
}

cf_node cf_scroller(cf_ctx *ctx, const char *name, const cf_node *children, uint32_t child_count) {
    cf_node node = {.style = cf_style_default()};

    if (ctx == NULL || name == NULL) {
        return node;
    }

    scroll_data *data = cf_frame_alloc(ctx, sizeof *data);
    cf_node *content = cf_frame_alloc(ctx, sizeof *content);
    cf_node *viewport_children = cf_frame_alloc(ctx, sizeof *viewport_children);

    if (data == NULL || content == NULL || viewport_children == NULL) {
        return node;
    }

    *data = (scroll_data){.ctx = ctx, .key = cf_ctx_key(ctx, name)};

    const cf_node *carried = cf_children(ctx, children, child_count);

    /* The offset is read at declaration time, so this frame's layout moves
     * the content by it; the wheel handler writes it for the next one. */
    const cf_scroll *held = cf_scroll_of(ctx, data->key);
    const float offset = held != NULL ? held->offset : 0.0f;

    *content = (cf_node){.style = cf_style_default(),
                         .children = carried,
                         .child_count = carried != NULL ? child_count : 0u};
    content->style.direction = CF_DIRECTION_COLUMN;
    content->style.position = CF_POSITION_RELATIVE;
    /* The wrapper keeps its whole height whatever the window shows of it:
     * the wheel's range is that height, and a shrunk wrapper walks nothing. */
    content->style.flex_shrink = 0.0f;
    content->style.inset.top = CF_PX(-offset);

    viewport_children[0] = *content;

    node.style =
        ctx->theme != NULL ? ctx->theme->variants[CF_ROLE_SCROLL].base : cf_style_default();
    node.role = CF_ROLE_SCROLL;
    /* A plain scroller announces as the region it is: what is worth saying about it is what it
     * holds, and a bridge that says "scroll area" at least tells a person where they are. */
    node.access = CF_ACCESS_SCROLL_AREA;
    /* The node carries the name its offset lives under, so the tree can read
     * this scroller's own block back from the record: a stable identity for the
     * diff, and the handle a keyboard reveal moves. */
    node.key = data->key;
    /* The scroll axis is the column's: the wrapper hugs what it carries
     * instead of stretching to the window and shrinking it to fit. */
    node.style.direction = CF_DIRECTION_COLUMN;
    node.style.clips_content = true;
    node.handlers.pointer = scroll_pointer;
    node.handlers.pointer_data = data;
    node.children = viewport_children;
    node.child_count = 1u;
    return node;
}
