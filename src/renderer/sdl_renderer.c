#include <math.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "internal.h"
#include "http_loader.h"

#define DEFAULT_FONT_SIZE 16.0f
#define TEXT_CACHE_CAPACITY 64
#define FONT_CACHE_CAPACITY 16
#define IMAGE_CACHE_CAPACITY 16

/* Foreground used when a labelled frame leaves text_color unset. */
static const UI_Color default_text_color = { 9, 9, 11, 255 };

/* Probed in order at startup; the first that opens wins. The bundled Lato is
 * last so every platform ends with a readable face, and it is the only one the
 * browser can reach, where there is no system font filesystem. */
static const char *const font_candidates[] = {
    "C:/Windows/Fonts/segoeui.ttf",
    "C:/Windows/Fonts/arial.ttf",
    "/System/Library/Fonts/Helvetica.ttc",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "assets/fonts/Lato-Regular.ttf",
};

typedef struct {
    const char *src;
    SDL_Texture *texture;
    /* Non-NULL while a remote source is still downloading. */
    UI_HttpDownload *download;
    int width;
    int height;
} ImageEntry;

typedef struct {
    float size;
    UI_FontStyle style;
    TTF_Font *font;
} FontEntry;

typedef struct {
    const char *text;
    float size;
    UI_Color color;
    UI_FontStyle style;
    int wrap_width;
    SDL_Texture *texture;
    int width;
    int height;
} TextEntry;

/* Renderer-owned caches. The path is NULL until a font loads; entries live
 * for the whole ui_render() call and are dropped before the window closes. */
static const char *g_font_path;
static char g_font_resolved[1024];
static FontEntry g_font_cache[FONT_CACHE_CAPACITY];
static int g_font_cache_count;
static TextEntry g_text_cache[TEXT_CACHE_CAPACITY];
static int g_text_cache_count;
static ImageEntry g_image_cache[IMAGE_CACHE_CAPACITY];
static int g_image_cache_count;

/* Ratio of device pixels to layout points for the active display. Text is
 * rasterized at `logical * scale` so the texture maps 1:1 onto the backbuffer
 * that SDL_WINDOW_HIGH_PIXEL_DENSITY allocated; every other draw stays in
 * logical points and relies on SDL_SetRenderScale to lift it up. The caches
 * key on the rasterized value, so a mid-run DPI change simply adds new
 * entries rather than corrupting old ones. */
static float g_display_scale = 1.0f;

#define SCROLLBAR_SIZE 10.0f

static const UI_Color default_scrollbar_color = { 0, 0, 0, 48 };

/* Ring drawn around the keyboard-focused frame. Dark enough to read on every
 * surface the demo uses. */
static const UI_Color default_focus_color = { 23, 23, 23, 255 };

static void ui_draw_frame(SDL_Renderer *renderer, UI_Frame *frame);

static void set_color(SDL_Renderer *renderer, UI_Color color) {
    SDL_SetRenderDrawColor(
        renderer,
        color.r,
        color.g,
        color.b,
        color.a
    );
}

/* SDL_SetRenderClipRect replaces the active clip instead of intersecting it,
 * so a nested clip must fold the ancestor's rect in by hand or content escapes
 * the parent's bounds. Intersects `rect` with the current clip, applies it, and
 * reports whether a clip was already active for the caller to hand to pop_clip. */
static bool push_clip(
    SDL_Renderer *renderer,
    SDL_Rect rect,
    SDL_Rect *previous
) {
    bool had = SDL_RenderClipEnabled(renderer);

    if (had) {
        SDL_GetRenderClipRect(renderer, previous);

        int right = rect.x + rect.w;
        int bottom = rect.y + rect.h;

        if (rect.x < previous->x) {
            rect.x = previous->x;
        }

        if (rect.y < previous->y) {
            rect.y = previous->y;
        }

        if (right > previous->x + previous->w) {
            right = previous->x + previous->w;
        }

        if (bottom > previous->y + previous->h) {
            bottom = previous->y + previous->h;
        }

        rect.w = right > rect.x ? right - rect.x : 0;
        rect.h = bottom > rect.y ? bottom - rect.y : 0;
    }

    SDL_SetRenderClipRect(renderer, &rect);

    return had;
}

static void pop_clip(
    SDL_Renderer *renderer,
    bool had,
    const SDL_Rect *previous
) {
    SDL_SetRenderClipRect(renderer, had ? previous : NULL);
}

/* Scanline fill for an axis-aligned rect with rounded corners. Radius is
 * clamped to half the shorter side so a rectangle can never over-round.
 * Falls through to the fast path when the radius is zero. */
static void fill_round_rect(
    SDL_Renderer *renderer,
    SDL_FRect rect,
    float radius
) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) {
        return;
    }

    float max_radius = (rect.w < rect.h ? rect.w : rect.h) * 0.5f;

    if (radius > max_radius) {
        radius = max_radius;
    }

    if (radius <= 0.0f) {
        SDL_RenderFillRect(renderer, &rect);
        return;
    }

    float cy_top = rect.y + radius;
    float cy_bottom = rect.y + rect.h - radius;
    float r_squared = radius * radius;

    int y_first = (int)rect.y;
    int y_last = (int)(rect.y + rect.h);

    for (int y = y_first; y < y_last; y++) {
        float fy = (float)y + 0.5f;
        float inset = 0.0f;
        float dy = 0.0f;

        if (fy < cy_top) {
            dy = cy_top - fy;
        } else if (fy > cy_bottom) {
            dy = fy - cy_bottom;
        }

        if (dy > 0.0f) {
            if (dy >= radius) {
                continue;
            }

            inset = radius - sqrtf(r_squared - dy * dy);
        }

        SDL_FRect row = {
            rect.x + inset,
            (float)y,
            rect.w - 2.0f * inset,
            1.0f
        };
        SDL_RenderFillRect(renderer, &row);
    }
}

static TTF_FontStyleFlags ttf_style(UI_FontStyle style) {
    switch (style) {
    case UI_FONT_BOLD: return TTF_STYLE_BOLD;
    case UI_FONT_ITALIC: return TTF_STYLE_ITALIC;
    case UI_FONT_BOLD_ITALIC: return TTF_STYLE_BOLD | TTF_STYLE_ITALIC;
    case UI_FONT_NORMAL: break;
    }

    return TTF_STYLE_NORMAL;
}

static TTF_Font *get_font(float size, UI_FontStyle style) {
    if (g_font_path == NULL) {
        return NULL;
    }

    float raster_size = size * g_display_scale;

    for (int i = 0; i < g_font_cache_count; i++) {
        if (g_font_cache[i].size == raster_size
            && g_font_cache[i].style == style) {
            return g_font_cache[i].font;
        }
    }

    if (g_font_cache_count >= FONT_CACHE_CAPACITY) {
        for (int i = 0; i < g_font_cache_count; i++) {
            TTF_CloseFont(g_font_cache[i].font);
        }
        g_font_cache_count = 0;
    }

    TTF_Font *font = TTF_OpenFont(g_font_path, raster_size);

    if (font == NULL) {
        return NULL;
    }

    TTF_SetFontStyle(font, ttf_style(style));

    g_font_cache[g_font_cache_count].size = raster_size;
    g_font_cache[g_font_cache_count].style = style;
    g_font_cache[g_font_cache_count].font = font;
    g_font_cache_count++;

    return font;
}

/* Picks the first readable font and keeps it open in the cache at the default
 * size. Silent no-op if none exists on this machine. */
static void probe_font(void) {
    const size_t count = sizeof(font_candidates) / sizeof(font_candidates[0]);

    for (size_t i = 0; i < count; i++) {
        TTF_Font *font = TTF_OpenFont(font_candidates[i], DEFAULT_FONT_SIZE);

        if (font != NULL) {
            g_font_path = font_candidates[i];
        } else {
            /* Relative paths resolve against the executable directory, where
             * the build copies the demo assets and where the browser mounts
             * its preloaded filesystem. */
            const char *base = SDL_GetBasePath();

            if (base == NULL) {
                continue;
            }

            SDL_snprintf(
                g_font_resolved, sizeof g_font_resolved, "%s%s", base,
                font_candidates[i]);
            SDL_free((void *)base);
            font = TTF_OpenFont(g_font_resolved, DEFAULT_FONT_SIZE);
            g_font_path = font != NULL ? g_font_resolved : NULL;
        }

        if (font == NULL) {
            continue;
        }

        g_font_cache[0].size = DEFAULT_FONT_SIZE;
        g_font_cache[0].style = UI_FONT_NORMAL;
        g_font_cache[0].font = font;
        g_font_cache_count = 1;

        return;
    }
}

/* Returns the cached texture for (text, size, color, style, wrap_width),
 * rebuilding on any change. Wrapped labels get a new entry per width, so a
 * full cache is flushed and rebuilt rather than silently dropping the text.
 * `size` and `wrap_width` are in logical points; the bitmap is baked at
 * device resolution so SDL's render scale maps it 1:1 onto the backbuffer.
 * The reported dimensions come back in logical points so draw_text can place
 * them without knowing about the DPI factor. */
static SDL_Texture *get_text_texture(
    SDL_Renderer *renderer,
    const char *text,
    float size,
    UI_Color color,
    UI_FontStyle style,
    int wrap_width,
    int *out_w,
    int *out_h
) {
    float raster_size = size * g_display_scale;
    int raster_wrap = wrap_width > 0
        ? (int)((float)wrap_width * g_display_scale)
        : 0;

    for (int i = 0; i < g_text_cache_count; i++) {
        TextEntry *e = &g_text_cache[i];

        if (e->text == text && e->size == raster_size && e->style == style
            && e->wrap_width == raster_wrap
            && memcmp(&e->color, &color, sizeof color) == 0) {
            *out_w = (int)((float)e->width / g_display_scale);
            *out_h = (int)((float)e->height / g_display_scale);
            return e->texture;
        }
    }

    if (g_text_cache_count >= TEXT_CACHE_CAPACITY) {
        for (int i = 0; i < g_text_cache_count; i++) {
            SDL_DestroyTexture(g_text_cache[i].texture);
        }
        g_text_cache_count = 0;
    }

    TTF_Font *font = get_font(size, style);

    if (font == NULL) {
        return NULL;
    }

    SDL_Color fg = { color.r, color.g, color.b, color.a };
    SDL_Surface *surface = raster_wrap > 0
        ? TTF_RenderText_Blended_Wrapped(font, text, 0, fg, raster_wrap)
        : TTF_RenderText_Blended(font, text, 0, fg);

    if (surface == NULL) {
        return NULL;
    }

    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    int w = surface->w;
    int h = surface->h;
    SDL_DestroySurface(surface);

    if (texture == NULL) {
        return NULL;
    }

    TextEntry *slot = &g_text_cache[g_text_cache_count++];
    slot->text = text;
    slot->size = raster_size;
    slot->color = color;
    slot->style = style;
    slot->wrap_width = raster_wrap;
    slot->texture = texture;
    slot->width = w;
    slot->height = h;

    *out_w = (int)((float)w / g_display_scale);
    *out_h = (int)((float)h / g_display_scale);

    return texture;
}

static UI_Color pick_background(const UI_Frame *frame) {
    const UI_FrameConfig *c = &frame->config;

    if (frame->pressed && c->pressed_background.a != 0) {
        return c->pressed_background;
    }

    if (frame->hovered && c->hover_background.a != 0) {
        return c->hover_background;
    }

    return c->background;
}

/* A scrollable's viewport is its padding-inset box, which is the span the
 * layout clamps the scroll offset against. The thumb must travel that same
 * span and start at the same origin, or it paints past the frame's edge once
 * the content is fully scrolled. */
static void scroll_viewport(
    const UI_Frame *frame,
    bool vertical,
    float *origin,
    float *track,
    float *extent
) {
    const UI_Edges *pad = &frame->config.padding;

    if (vertical) {
        *origin = frame->y + pad->top;
        *track = frame->height - pad->top - pad->bottom;
        *extent = frame->content_height;
    } else {
        *origin = frame->x + pad->left;
        *track = frame->width - pad->left - pad->right;
        *extent = frame->content_width;
    }

    if (*track < 0.0f) {
        *track = 0.0f;
    }
}

/* Geometry for one scroll axis. Writes the gutter the thumb lives in and the
 * thumb itself; the thumb is zero-size when the axis fits. */
static void scrollbar_geometry(
    const UI_Frame *frame,
    bool vertical,
    float scroll,
    SDL_FRect *gutter,
    SDL_FRect *thumb
) {
    float origin;
    float track;
    float extent;
    scroll_viewport(frame, vertical, &origin, &track, &extent);

    float thickness = SCROLLBAR_SIZE - 2.0f;

    *thumb = (SDL_FRect){ 0.0f, 0.0f, 0.0f, 0.0f };

    if (vertical) {
        *gutter = (SDL_FRect){
            frame->x + frame->width - SCROLLBAR_SIZE, origin,
            SCROLLBAR_SIZE, track
        };
    } else {
        *gutter = (SDL_FRect){
            origin, frame->y + frame->height - SCROLLBAR_SIZE,
            track, SCROLLBAR_SIZE
        };
    }

    if (extent <= track) {
        return;
    }

    float length = track * track / extent;

    if (length < 24.0f) {
        length = 24.0f;
    }

    float frac = scroll / (extent - track);

    if (frac < 0.0f) {
        frac = 0.0f;
    } else if (frac > 1.0f) {
        frac = 1.0f;
    }

    if (vertical) {
        *thumb = (SDL_FRect){
            gutter->x + 1.0f, origin + frac * (track - length),
            thickness, length
        };
    } else {
        *thumb = (SDL_FRect){
            origin + frac * (track - length), gutter->y + 1.0f,
            length, thickness
        };
    }
}

static bool point_in(SDL_FRect rect, float x, float y) {
    return x >= rect.x && x < rect.x + rect.w
        && y >= rect.y && y < rect.y + rect.h;
}

/* Sets a scrollable's offset so its thumb sits `grab` px below the cursor on
 * the given axis. Clamps to the content extent so a click past the ends stops
 * at the extreme. */
static void scroll_to_cursor(
    UI_Frame *frame,
    bool vertical,
    float cursor,
    float grab
) {
    float origin;
    float track;
    float extent;
    scroll_viewport(frame, vertical, &origin, &track, &extent);

    if (extent <= track) {
        return;
    }

    float length = track * track / extent;

    if (length < 24.0f) {
        length = 24.0f;
    }

    float frac = (cursor - origin - grab) / (track - length);

    if (frac < 0.0f) {
        frac = 0.0f;
    } else if (frac > 1.0f) {
        frac = 1.0f;
    }

    float max_scroll = extent - track;

    if (vertical) {
        frame->scroll_y = frac * max_scroll;
    } else {
        frame->scroll_x = frac * max_scroll;
    }
}

/* Nearest scrollable ancestor of the frame under (x, y), or NULL. */
static UI_Frame *scrollable_under(UI_Context *ctx, float x, float y) {
    UI_Frame *frame = ui_hit_test(ctx, x, y);

    while (frame != NULL && !frame->config.scrollable) {
        frame = frame->parent;
    }

    return frame;
}

static void draw_scrollbar(
    SDL_Renderer *renderer,
    const UI_Frame *frame,
    bool vertical,
    float scroll,
    UI_Color color
) {
    SDL_FRect gutter;
    SDL_FRect thumb;
    scrollbar_geometry(frame, vertical, scroll, &gutter, &thumb);

    if (thumb.w <= 0.0f || thumb.h <= 0.0f) {
        return;
    }

    set_color(renderer, color);
    fill_round_rect(renderer, thumb, (SCROLLBAR_SIZE - 2.0f) * 0.5f);
}

/* Recurses into children, clipping to the frame rect when it scrolls and
 * overlaying a thumb on whichever axis overflows. */
static void draw_children(SDL_Renderer *renderer, UI_Frame *frame) {
    const UI_FrameConfig *config = &frame->config;

    if (!config->scrollable) {
        for (int i = 0; i < frame->child_count; i++) {
            ui_draw_frame(renderer, frame->children[i]);
        }

        return;
    }

    SDL_Rect clip = {
        (int)frame->x, (int)frame->y, (int)frame->width, (int)frame->height
    };
    SDL_Rect previous;
    bool had_clip = push_clip(renderer, clip, &previous);

    for (int i = 0; i < frame->child_count; i++) {
        ui_draw_frame(renderer, frame->children[i]);
    }

    pop_clip(renderer, had_clip, &previous);

    UI_Color thumb_color = config->border_color;

    if (thumb_color.a == 0) {
        thumb_color = default_scrollbar_color;
    }

    draw_scrollbar(renderer, frame, true, frame->scroll_y, thumb_color);
    draw_scrollbar(renderer, frame, false, frame->scroll_x, thumb_color);
}

static void measure_texture(SDL_Texture *texture, int *width, int *height) {
    float w = 0.0f;
    float h = 0.0f;

    *width = 0;
    *height = 0;

    if (SDL_GetTextureSize(texture, &w, &h)) {
        *width = (int)w;
        *height = (int)h;
    }
}

/* Decodes a local path, retrying relative paths against the executable
 * directory before the working directory. */
static SDL_Texture *texture_from_path(
    SDL_Renderer *renderer,
    const char *src,
    int *width,
    int *height
) {
    SDL_Texture *texture = IMG_LoadTexture(renderer, src);

    if (texture == NULL) {
        const char *base = SDL_GetBasePath();

        if (base != NULL) {
            char full[1024];
            SDL_snprintf(full, sizeof full, "%s%s", base, src);
            texture = IMG_LoadTexture(renderer, full);
        }
    }

    if (texture != NULL) {
        measure_texture(texture, width, height);
    }

    return texture;
}

/* Decodes an image that has already been downloaded. SDL_IOFromConstMem does
 * not copy, but the loader keeps the buffer alive until ui_http_shutdown(),
 * which runs after every texture is destroyed, so this stays valid. */
static SDL_Texture *texture_from_bytes(
    SDL_Renderer *renderer,
    UI_HttpDownload *download,
    int *width,
    int *height
) {
    size_t len = 0;
    const unsigned char *bytes = ui_http_bytes(download, &len);
    SDL_Texture *texture;
    SDL_IOStream *io;

    if (bytes == NULL) {
        return NULL;
    }

    io = SDL_IOFromConstMem(bytes, len);

    if (io == NULL) {
        return NULL;
    }

    /* closeio is true, so the stream is released on both paths. */
    texture = IMG_LoadTexture_IO(renderer, io, true);

    if (texture != NULL) {
        measure_texture(texture, width, height);
    }

    return texture;
}

/* Returns the cached entry for an image source, which may be a local path or
 * an http(s) URL. A failed local load keeps a NULL texture so the path is not
 * retried on every frame; a remote source keeps a download handle instead and
 * drops it once the transfer reaches a terminal state. */
static ImageEntry *get_image(SDL_Renderer *renderer, const char *src) {
    ImageEntry *slot = NULL;

    for (int i = 0; i < g_image_cache_count; i++) {
        if (g_image_cache[i].src == src) {
            slot = &g_image_cache[i];
            break;
        }
    }

    /* Done means a texture, a permanent local miss, or a finished download. */
    if (slot != NULL && slot->download == NULL) {
        return slot;
    }

    if (slot == NULL) {
        if (g_image_cache_count >= IMAGE_CACHE_CAPACITY) {
            return NULL;
        }

        slot = &g_image_cache[g_image_cache_count++];
        slot->src = src;
        slot->texture = NULL;
        slot->download = NULL;
        slot->width = 0;
        slot->height = 0;

        if (ui_http_is_url(src)) {
            slot->download = ui_http_start(src);
        } else {
            slot->texture = texture_from_path(
                renderer, src, &slot->width, &slot->height);
        }
    }

    if (slot->download != NULL) {
        UI_HttpStatus status = ui_http_status(slot->download);

        if (status == UI_HTTP_READY) {
            slot->texture = texture_from_bytes(
                renderer, slot->download, &slot->width, &slot->height);
            slot->download = NULL;
        } else if (status == UI_HTTP_FAILED) {
            /* The loader already logged the reason. Clearing the handle stops
             * the retry; the frame draws its background in the meantime. */
            slot->download = NULL;
        }
    }

    return slot;
}

static void draw_image(
    SDL_Renderer *renderer,
    const UI_Frame *frame,
    const UI_FrameConfig *config
) {
    ImageEntry *img = get_image(renderer, config->image_src);

    if (img == NULL || img->texture == NULL) {
        return;
    }

    float cx = frame->x + config->padding.left;
    float cy = frame->y + config->padding.top;
    float cw = frame->width - config->padding.left - config->padding.right;
    float ch = frame->height - config->padding.top - config->padding.bottom;

    if (cw <= 0.0f || ch <= 0.0f) {
        return;
    }

    /* Texture dims are device pixels; convert to logical points so the fit
     * math lives in the same space as the content box. UI_IMAGE_CENTER then
     * lands 1:1 on the backbuffer once SDL applies the render scale. */
    float iw = (float)img->width / g_display_scale;
    float ih = (float)img->height / g_display_scale;
    SDL_FRect dst;

    switch (config->image_fit) {
    case UI_IMAGE_FILL:
        dst = (SDL_FRect){ cx, cy, cw, ch };
        break;
    case UI_IMAGE_CENTER:
        dst = (SDL_FRect){ cx + (cw - iw) * 0.5f, cy + (ch - ih) * 0.5f, iw, ih };
        break;
    case UI_IMAGE_FIT:
    default: {
        float scale = fminf(cw / iw, ch / ih);
        float w = iw * scale;
        float h = ih * scale;
        dst = (SDL_FRect){ cx + (cw - w) * 0.5f, cy + (ch - h) * 0.5f, w, h };
        break;
    }
    }

    SDL_Rect clip = { (int)cx, (int)cy, (int)cw, (int)ch };
    SDL_Rect previous;
    bool had_clip = push_clip(renderer, clip, &previous);

    SDL_RenderTexture(renderer, img->texture, NULL, &dst);

    pop_clip(renderer, had_clip, &previous);
}

static void draw_text(
    SDL_Renderer *renderer,
    const UI_Frame *frame,
    const UI_FrameConfig *config
) {
    float size = config->font_size > 0.0f ? config->font_size : DEFAULT_FONT_SIZE;
    UI_Color color = config->text_color;

    if (color.a == 0) {
        color = default_text_color;
    }

    float content_x = frame->x + config->padding.left;
    float content_y = frame->y + config->padding.top;
    float content_w = frame->width - config->padding.left - config->padding.right;
    float content_h = frame->height - config->padding.top - config->padding.bottom;

    if (content_w <= 0.0f || content_h <= 0.0f) {
        return;
    }

    int wrap_width = config->wrap_text ? (int)content_w : 0;

    int w = 0;
    int h = 0;
    SDL_Texture *texture = get_text_texture(
        renderer, config->text, size, color, config->font_style, wrap_width,
        &w, &h);

    if (texture == NULL) {
        return;
    }

    SDL_FRect dst;

    if (wrap_width > 0) {
        dst = (SDL_FRect){ content_x, content_y, (float)w, (float)h };
    } else {
        dst = (SDL_FRect){
            content_x + (content_w - (float)w) * 0.5f,
            content_y + (content_h - (float)h) * 0.5f,
            (float)w,
            (float)h
        };
    }

    /* A label keeps the size it was measured at: an unwrapped one is as wide as
     * the string needs, a wrapped one as tall as its line count needs, so either
     * can leave the box the layout handed it — a FILL box narrows with the
     * window, and a fixed height stops counting lines. Clipping to the content
     * box keeps a label inside the space layout reserved for it, exactly as
     * draw_image does, instead of overprinting the frames stacked below it. */
    SDL_Rect clip = { (int)content_x, (int)content_y, (int)content_w, (int)content_h };
    SDL_Rect previous;
    bool had_clip = push_clip(renderer, clip, &previous);

    SDL_RenderTexture(renderer, texture, NULL, &dst);

    pop_clip(renderer, had_clip, &previous);
}

static void ui_draw_frame(
    SDL_Renderer *renderer,
    UI_Frame *frame
) {
    const UI_FrameConfig *config = &frame->config;

    SDL_FRect outer = {
        .x = frame->x,
        .y = frame->y,
        .w = frame->width,
        .h = frame->height
    };

    UI_Color bg = pick_background(frame);

    if (config->border_width > 0.0f) {
        set_color(renderer, config->border_color);
        fill_round_rect(renderer, outer, config->corner_radius);

        float bw = config->border_width;
        SDL_FRect inner = {
            .x = outer.x + bw,
            .y = outer.y + bw,
            .w = outer.w - 2.0f * bw,
            .h = outer.h - 2.0f * bw
        };
        float inner_radius = config->corner_radius > bw
            ? config->corner_radius - bw
            : 0.0f;

        set_color(renderer, bg);
        fill_round_rect(renderer, inner, inner_radius);
    } else {
        set_color(renderer, bg);
        fill_round_rect(renderer, outer, config->corner_radius);
    }

    if (config->image_src != NULL) {
        draw_image(renderer, frame, config);
    }

    if (config->text != NULL) {
        draw_text(renderer, frame, config);
    }

    draw_children(renderer, frame);

    if (frame->focused) {
        set_color(renderer, default_focus_color);
        SDL_FRect ring = {
            outer.x - 1.0f, outer.y - 1.0f, outer.w + 2.0f, outer.h + 2.0f
        };
        SDL_RenderRect(renderer, &ring);
        ring = (SDL_FRect){
            outer.x - 2.0f, outer.y - 2.0f, outer.w + 4.0f, outer.h + 4.0f
        };
        SDL_RenderRect(renderer, &ring);
    }
}

static void shutdown_text(void) {
    for (int i = 0; i < g_text_cache_count; i++) {
        SDL_DestroyTexture(g_text_cache[i].texture);
    }
    g_text_cache_count = 0;

    for (int i = 0; i < g_font_cache_count; i++) {
        TTF_CloseFont(g_font_cache[i].font);
    }
    g_font_cache_count = 0;
    g_font_path = NULL;

    for (int i = 0; i < g_image_cache_count; i++) {
        if (g_image_cache[i].texture != NULL) {
            SDL_DestroyTexture(g_image_cache[i].texture);
        }

        /* A download that never finished must not outlive the textures that
         * were copied from it. */
        g_image_cache[i].download = NULL;
    }
    g_image_cache_count = 0;

    ui_http_shutdown();

    TTF_Quit();
}

/* If (x, y) lands on a scrollable's visible scrollbar, start a thumb drag and
 * return that frame; a click on the gutter (not the thumb) pages toward the
 * click first. Returns NULL when no scrollbar was hit, so the caller can route
 * a normal press. */
static UI_Frame *begin_scrollbar_drag(
    UI_Context *ctx,
    float x,
    float y,
    bool *out_vertical,
    float *out_grab
) {
    UI_Frame *frame = scrollable_under(ctx, x, y);

    if (frame == NULL) {
        return NULL;
    }

    for (int axis = 0; axis < 2; axis++) {
        bool vertical = axis == 0;
        float scroll = vertical ? frame->scroll_y : frame->scroll_x;
        SDL_FRect gutter;
        SDL_FRect thumb;
        scrollbar_geometry(frame, vertical, scroll, &gutter, &thumb);

        if (thumb.w <= 0.0f || thumb.h <= 0.0f || !point_in(gutter, x, y)) {
            continue;
        }

        float cursor = vertical ? y : x;
        *out_vertical = vertical;

        if (point_in(thumb, x, y)) {
            *out_grab = cursor - (vertical ? thumb.y : thumb.x);
        } else {
            *out_grab = (vertical ? thumb.h : thumb.w) * 0.5f;
            scroll_to_cursor(frame, vertical, cursor, *out_grab);
        }

        return frame;
    }

    return NULL;
}

/* Maps a key press to focus movement, activation of the focused frame, or
 * scrolling of the nearest scrollable region. */
static void handle_key(
    UI_Context *ctx,
    const SDL_KeyboardEvent *key,
    float viewport
) {
    float step = 40.0f;
    float page = viewport * 0.8f;

    switch (key->key) {
    case SDLK_TAB:
        if (!key->repeat) {
            ui_focus_next(ctx, (key->mod & SDL_KMOD_SHIFT) != 0);
        }
        break;
    case SDLK_RETURN:
    case SDLK_RETURN2:
    case SDLK_KP_ENTER:
    case SDLK_SPACE:
        if (!key->repeat) {
            UI_Frame *activated = ui_activate_focused(ctx);

            if (activated != NULL && activated->config.link_url != NULL) {
                SDL_OpenURL(activated->config.link_url);
            }
        }
        break;
    /* The navigation cluster and the NumLock-off numpad share these roles. */
    case SDLK_UP:
    case SDLK_KP_8:
        ui_scroll_by(ctx, 0.0f, -step);
        break;
    case SDLK_DOWN:
    case SDLK_KP_2:
        ui_scroll_by(ctx, 0.0f, step);
        break;
    case SDLK_LEFT:
    case SDLK_KP_4:
        ui_scroll_by(ctx, -step, 0.0f);
        break;
    case SDLK_RIGHT:
    case SDLK_KP_6:
        ui_scroll_by(ctx, step, 0.0f);
        break;
    case SDLK_PAGEUP:
    case SDLK_KP_9:
        ui_scroll_by(ctx, 0.0f, -page);
        break;
    case SDLK_PAGEDOWN:
    case SDLK_KP_3:
        ui_scroll_by(ctx, 0.0f, page);
        break;
    case SDLK_HOME:
    case SDLK_KP_7:
        ui_scroll_extreme(ctx, false);
        break;
    case SDLK_END:
    case SDLK_KP_1:
        ui_scroll_extreme(ctx, true);
        break;
    case SDLK_ESCAPE:
        ui_clear_focus(ctx);
        break;
    default:
        break;
    }
}

void ui_render(UI_Context *ctx, UI_Frame *root) {
    if (ctx == NULL || root == NULL) {
        return;
    }

    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;

    /* HIGH_PIXEL_DENSITY gives the backbuffer the display's real pixel grid.
     * Without it, SDL hands us a point-sized surface that the compositor
     * stretches to fit — the source of the reported blur on Windows at
     * fractional scaling and on every browser canvas above devicePixelRatio
     * of 1. Layout continues to run in logical points; SDL_SetRenderScale
     * bridges the two on every draw. */
    if (SDL_CreateWindowAndRenderer(
            "cform-ui",
            ctx->window_width,
            ctx->window_height,
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY,
            &window,
            &renderer) == false) {
        SDL_Log("Failed to create window/renderer: %s", SDL_GetError());
        return;
    }

    /* Default is BLENDMODE_NONE, which ignores the draw color's alpha and
     * paints transparent backgrounds solid black. Blending must be on for
     * zero-alpha frames to stay transparent and for the scrollbar thumb. */
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    float initial_scale = SDL_GetWindowDisplayScale(window);

    if (initial_scale > 0.0f) {
        g_display_scale = initial_scale;
    }

    SDL_SetRenderScale(renderer, g_display_scale, g_display_scale);

    if (TTF_Init() == true) {
        probe_font();
    } else {
        SDL_Log("Failed to init SDL3_ttf: %s", SDL_GetError());
    }

    ctx->root = root;

    bool running = true;
    UI_Frame *drag_frame = NULL;
    bool drag_vertical = false;
    float drag_grab = 0.0f;

    while (running) {
        int width = 0;
        int height = 0;
        SDL_GetWindowSize(window, &width, &height);

        float display_scale = SDL_GetWindowDisplayScale(window);

        if (display_scale > 0.0f && display_scale != g_display_scale) {
            g_display_scale = display_scale;
            SDL_SetRenderScale(renderer, display_scale, display_scale);
        }

        SDL_Event event;

        while (SDL_PollEvent(&event) != 0) {
            /* Motion and button coordinates arrive in device pixels whenever
             * the renderer carries a scale. Convert once so the rest of the
             * loop, and the hit tests below, can assume logical points. */
            SDL_ConvertEventToRenderCoordinates(renderer, &event);

            switch (event.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_KEY_DOWN:
                handle_key(ctx, &event.key, (float)height);
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (drag_frame != NULL) {
                    scroll_to_cursor(drag_frame, drag_vertical,
                        drag_vertical ? event.motion.y : event.motion.x,
                        drag_grab);
                } else {
                    ui_pointer_move(ctx, event.motion.x, event.motion.y);
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    drag_frame = begin_scrollbar_drag(ctx,
                        event.button.x, event.button.y,
                        &drag_vertical, &drag_grab);

                    if (drag_frame == NULL) {
                        ui_pointer_down(ctx, event.button.x, event.button.y);
                    }
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (drag_frame != NULL) {
                    drag_frame = NULL;
                } else if (event.button.button == SDL_BUTTON_LEFT) {
                    UI_Frame *clicked =
                        ui_pointer_up(ctx, event.button.x, event.button.y);

                    if (clicked != NULL && clicked->config.link_url != NULL) {
                        SDL_OpenURL(clicked->config.link_url);
                    }
                }
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                ui_pointer_wheel(ctx, event.wheel.mouse_x, event.wheel.mouse_y,
                    event.wheel.x, event.wheel.y);
                break;
            case SDL_EVENT_WINDOW_MOUSE_LEAVE:
                ui_pointer_leave(ctx);
                break;
            default:
                break;
            }
        }

        ui_layout_frame(root, 0.0f, 0.0f, (float)width, (float)height);

        SDL_SetRenderDrawColor(renderer, 245, 245, 245, 255);
        SDL_RenderClear(renderer);
        ui_draw_frame(renderer, root);
        SDL_RenderPresent(renderer);

        SDL_Delay(16);
    }

    ui_pointer_leave(ctx);
    ctx->root = NULL;

    shutdown_text();

    SDL_DestroyWindow(window);
    SDL_Quit();
}
