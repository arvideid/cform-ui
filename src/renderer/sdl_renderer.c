#include <math.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "internal.h"

#define DEFAULT_FONT_SIZE 16.0f
#define TEXT_CACHE_CAPACITY 64
#define FONT_CACHE_CAPACITY 8

/* Foreground used when a labelled frame leaves text_color unset. */
static const UI_Color default_text_color = { 250, 250, 250, 255 };

/* Probed in order at startup; the first that opens wins. Emscripten has no
 * system font paths, so text silently stays hidden in the browser build. */
static const char *const font_candidates[] = {
    "C:/Windows/Fonts/segoeui.ttf",
    "C:/Windows/Fonts/arial.ttf",
    "/System/Library/Fonts/Helvetica.ttc",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
};

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
    SDL_Texture *texture;
    int width;
    int height;
} TextEntry;

/* Renderer-owned caches. The path is NULL until a font loads; entries live
 * for the whole ui_render() call and are dropped before the window closes. */
static const char *g_font_path;
static FontEntry g_font_cache[FONT_CACHE_CAPACITY];
static int g_font_cache_count;
static TextEntry g_text_cache[TEXT_CACHE_CAPACITY];
static int g_text_cache_count;

static void set_color(SDL_Renderer *renderer, UI_Color color) {
    SDL_SetRenderDrawColor(
        renderer,
        color.r,
        color.g,
        color.b,
        color.a
    );
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

static TTF_Font *get_font(float size) {
    if (g_font_path == NULL) {
        return NULL;
    }

    for (int i = 0; i < g_font_cache_count; i++) {
        if (g_font_cache[i].size == size) {
            return g_font_cache[i].font;
        }
    }

    if (g_font_cache_count >= FONT_CACHE_CAPACITY) {
        return NULL;
    }

    TTF_Font *font = TTF_OpenFont(g_font_path, size);

    if (font == NULL) {
        return NULL;
    }

    g_font_cache[g_font_cache_count].size = size;
    g_font_cache[g_font_cache_count].font = font;
    g_font_cache_count++;

    return font;
}

/* Picks the first readable system font and keeps it open in the cache at the
 * default size. Silent no-op if none exists on this machine. */
static void probe_font(void) {
    const size_t count = sizeof(font_candidates) / sizeof(font_candidates[0]);

    for (size_t i = 0; i < count; i++) {
        TTF_Font *font = TTF_OpenFont(font_candidates[i], DEFAULT_FONT_SIZE);

        if (font != NULL) {
            g_font_path = font_candidates[i];
            g_font_cache[0].size = DEFAULT_FONT_SIZE;
            g_font_cache[0].font = font;
            g_font_cache_count = 1;
            return;
        }
    }
}

/* Returns the cached texture for (text, size, color), rebuilding on any
 * change. Cache exhaustion returns NULL rather than evicting. */
static SDL_Texture *get_text_texture(
    SDL_Renderer *renderer,
    const char *text,
    float size,
    UI_Color color,
    int *out_w,
    int *out_h
) {
    for (int i = 0; i < g_text_cache_count; i++) {
        TextEntry *e = &g_text_cache[i];

        if (e->text == text && e->size == size &&
            memcmp(&e->color, &color, sizeof color) == 0) {
            *out_w = e->width;
            *out_h = e->height;
            return e->texture;
        }
    }

    if (g_text_cache_count >= TEXT_CACHE_CAPACITY) {
        return NULL;
    }

    TTF_Font *font = get_font(size);

    if (font == NULL) {
        return NULL;
    }

    SDL_Color fg = { color.r, color.g, color.b, color.a };
    SDL_Surface *surface = TTF_RenderText_Blended(font, text, 0, fg);

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
    slot->size = size;
    slot->color = color;
    slot->texture = texture;
    slot->width = w;
    slot->height = h;

    *out_w = w;
    *out_h = h;

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

    int w = 0;
    int h = 0;
    SDL_Texture *texture = get_text_texture(
        renderer, config->text, size, color, &w, &h);

    if (texture == NULL) {
        return;
    }

    float content_x = frame->x + config->padding.left;
    float content_y = frame->y + config->padding.top;
    float content_w = frame->width - config->padding.left - config->padding.right;
    float content_h = frame->height - config->padding.top - config->padding.bottom;

    SDL_FRect dst = {
        content_x + (content_w - (float)w) * 0.5f,
        content_y + (content_h - (float)h) * 0.5f,
        (float)w,
        (float)h
    };

    SDL_RenderTexture(renderer, texture, NULL, &dst);
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

    if (config->text != NULL) {
        draw_text(renderer, frame, config);
    }

    for (int i = 0; i < frame->child_count; i++) {
        ui_draw_frame(renderer, frame->children[i]);
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

    TTF_Quit();
}

void ui_render(UI_Context *ctx, UI_Frame *root) {
    if (ctx == NULL || root == NULL) {
        return;
    }

    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;

    if (SDL_CreateWindowAndRenderer(
            "cform-ui",
            ctx->window_width,
            ctx->window_height,
            0,
            &window,
            &renderer) == false) {
        SDL_Log("Failed to create window/renderer: %s", SDL_GetError());
        return;
    }

    if (TTF_Init() == true) {
        probe_font();
    } else {
        SDL_Log("Failed to init SDL3_ttf: %s", SDL_GetError());
    }

    ctx->root = root;

    bool running = true;

    while (running) {
        SDL_Event event;

        while (SDL_PollEvent(&event) != 0) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_MOUSE_MOTION:
                ui_pointer_move(ctx, event.motion.x, event.motion.y);
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    ui_pointer_down(ctx, event.button.x, event.button.y);
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    ui_pointer_up(ctx, event.button.x, event.button.y);
                }
                break;
            case SDL_EVENT_WINDOW_MOUSE_LEAVE:
                ui_pointer_leave(ctx);
                break;
            default:
                break;
            }
        }

        int width = 0;
        int height = 0;
        SDL_GetWindowSize(window, &width, &height);

        ui_layout_frame(root, 0.0f, 0.0f, (float)width, (float)height);

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
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
