#include <stdio.h>

#include "cf/widget.h"

/* The bytes one list keeps for the dispatch it drives: the context to read
 * the viewport's laid-out height, the name the offset block lives under,
 * and the geometry the rows are held to. They live in the frame arena
 * beside the frame that declared them. */
typedef struct list_data {
    cf_ctx *ctx;
    cf_name key;
    float row_height;
    float content_height;
} list_data;

/* Enough for a build's name and the row's index. The name is capped so the
 * index suffix always survives: writing the whole name first could push the
 * `/%u` off the end, and two rows then intern the same key and reconcile
 * onto each other's slots. Truncating the name keeps every index distinct. */
#define ROW_KEY_MAX 64u

static void list_pointer(cf_event *event, void *user_data) {
    list_data *list = user_data;

    if (event->type != CF_EVENT_SCROLL || list->ctx->rects == NULL) {
        return;
    }

    cf_scroll *held = cf_scroll_of(list->ctx, list->key);

    if (held == NULL) {
        return;
    }

    /* One notch walks one row: the row is the unit the list shows in. */
    const float viewport_height = list->ctx->rects[event->target].height;

    const float walked = cf_scroll_clamp(held->offset + event->wheel.y * list->row_height,
                                         viewport_height, list->content_height);

    /* The row the list stops on is the row it has run out of, and a list at the
     * end of its rows is not a reason for the page under it to stand still. */
    if (walked != held->offset) {
        held->offset = walked;
        cf_stop(event);
    }
}

cf_node cf_list(cf_ctx *ctx, const char *name, float viewport_height, float row_height,
                uint32_t row_count, cf_row_fn make_row, void *user_data) {
    cf_node node = {.style = cf_style_default()};

    if (ctx == NULL || name == NULL || make_row == NULL || row_height <= 0.0f) {
        return node;
    }

    list_data *data = cf_frame_alloc(ctx, sizeof *data);
    cf_node *content = cf_frame_alloc(ctx, sizeof *content);
    cf_node *viewport_children = cf_frame_alloc(ctx, sizeof *viewport_children);

    if (data == NULL || content == NULL || viewport_children == NULL) {
        return node;
    }

    *data = (list_data){.ctx = ctx,
                        .key = cf_ctx_key(ctx, name),
                        .row_height = row_height,
                        .content_height = (float)row_count * row_height};

    const cf_scroll *held = cf_scroll_of(ctx, data->key);
    const float offset = held != NULL ? held->offset : 0.0f;
    const float clamped = cf_scroll_clamp(offset, viewport_height, data->content_height);
    const uint32_t first = (uint32_t)(clamped / row_height);
    uint32_t shown = (uint32_t)(viewport_height / row_height) + 1u;

    if (first + shown > row_count) {
        shown = row_count - first;
    }

    /* The rows live in the frame arena beside the content wrapper that
     * borrows them: both are read by this frame's diff alone. */
    cf_node *rows = NULL;

    if (shown > 0u) {
        rows = cf_frame_alloc(ctx, (size_t)shown * sizeof *rows);

        if (rows == NULL) {
            return node;
        }
    }

    for (uint32_t i = 0u; i < shown; ++i) {
        const uint32_t index = first + i;

        rows[i] = make_row(ctx, index, user_data);
        rows[i].style.height = CF_PX(row_height);

        if (rows[i].key == CF_NO_NAME) {
            char text[ROW_KEY_MAX];
            /* Leave the widest index a uint32 can print its own bytes plus the
             * slash; the name fills the rest and truncates before the index. */
            const int name_room = (int)(sizeof text - 12u);
            int name_len = 0;
            while (name_len < name_room && name[name_len] != '\0') {
                ++name_len;
            }
            (void)snprintf(text, sizeof text, "%.*s/%u", name_len, name, index);
            rows[i].key = cf_ctx_key(ctx, text);
        }
    }

    *content = (cf_node){.style = cf_style_default(), .children = rows, .child_count = shown};
    content->style.direction = CF_DIRECTION_COLUMN;
    content->style.position = CF_POSITION_RELATIVE;
    /* The wrapper keeps its whole window's worth of rows whatever the
     * viewport shows: a shrunk wrapper would shrink the rows after it. */
    content->style.flex_shrink = 0.0f;

    /* The window's first row sits partly above the top: the wrapper moves
     * by what the offset hides beyond whole rows. */
    content->style.inset.top = CF_PX((float)first * row_height - clamped);

    viewport_children[0] = *content;

    node.style =
        ctx->theme != NULL ? ctx->theme->variants[CF_ROLE_SCROLL].base : cf_style_default();
    node.role = CF_ROLE_SCROLL;
    /* A list is more than a scroller to a bridge: it is a run of items a person steps through, so
     * the boxes the row factory returns are worth declaring CF_ACCESS_ITEM on. Only the rows this
     * frame built are in the tree, which is exactly what a bridge should be told. */
    node.access = CF_ACCESS_LIST;
    /* The list is a scroller: it carries the name its offset lives under so the
     * tree reads the block back from the record, the same handle a keyboard
     * reveal moves. */
    node.key = data->key;
    /* The scroll axis is the column's: the wrapper hugs its rows instead of
     * stretching to the window and shrinking them to fit. */
    node.style.direction = CF_DIRECTION_COLUMN;
    node.style.clips_content = true;
    node.style.height = CF_PX(viewport_height);
    node.handlers.pointer = list_pointer;
    node.handlers.pointer_data = data;
    node.children = viewport_children;
    node.child_count = 1u;
    return node;
}
