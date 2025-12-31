/*
 * SDL2 Backend Implementation for libiui
 *
 * This file implements the iui_port_t interface using SDL2.
 * All HiDPI scaling is handled transparently.
 *
 * Architecture:
 *   Unlike headless.c and wasm.c which use software rendering via port-sw.h's
 *   rasterizer, SDL2 uses hardware-accelerated SDL_Renderer for primitives.
 *   However, vector font path handling (Bezier tessellation) is shared via
 *   iui_path_state_t and the _scaled path functions from port-sw.h.
 *
 *   This design eliminates Bezier curve code duplication while allowing each
 *   port to use its optimal rendering approach for primitives.
 */

#include <SDL.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "port-sw.h"
#include "port.h"

/* SDL2 port context - holds all platform-specific state. */
struct iui_port_ctx {
    SDL_Window *window;
    SDL_Renderer *renderer;
    float scale;       /* HiDPI scale factor */
    float font_height; /* Logical font height for libiui */

    /* Vector path state for font rendering (uses shared state from port-sw.h)
     * Note: Path points are stored in physical (scaled) coordinates.
     */
    iui_path_state_t path;

    /* Timing */
    Uint64 last_frame_ticks;
    float delta_time;

    /* Event state */
    bool running;
    bool exit_requested;

    /* Queued input (from poll_events) */
    iui_port_input queued_input;

    /* Callbacks (stored for get_renderer_callbacks) */
    iui_renderer_t render_ops;
    iui_vector_t vector_ops;
};

/* Internal Helper Functions */

static void set_color(SDL_Renderer *r, uint32_t srgb_color)
{
    uint8_t a = (srgb_color >> 24) & 0xFF;
    uint8_t rv = (srgb_color >> 16) & 0xFF;
    uint8_t g = (srgb_color >> 8) & 0xFF;
    uint8_t b = srgb_color & 0xFF;
    SDL_SetRenderDrawColor(r, rv, g, b, a);
}

static void draw_rounded_rect_scaled(SDL_Renderer *r,
                                     float x,
                                     float y,
                                     float w,
                                     float h,
                                     float radius,
                                     float scale)
{
    /* Scale to physical pixels */
    x *= scale, y *= scale;
    w *= scale, h *= scale;
    radius *= scale;

    int ix = (int) floorf(x), iy = (int) floorf(y);
    int iw = (int) ceilf(x + w) - ix;
    int ih = (int) ceilf(y + h) - iy;

    if (radius <= 0.5f) {
        SDL_Rect rect = {ix, iy, iw, ih};
        SDL_RenderFillRect(r, &rect);
        return;
    }

    int iradius = (int) roundf(radius);
    if (iradius > iw / 2)
        iradius = iw / 2;
    if (iradius > ih / 2)
        iradius = ih / 2;
    if (iradius < 1)
        iradius = 1;

    float r2 = (float) iradius * (float) iradius;

    for (int row = 0; row < ih; row++) {
        int line_y = iy + row;
        int x_start = ix;
        int x_end = ix + iw;

        if (row < iradius) {
            float dy = (float) (iradius - row) - 0.5f;
            if (dy > 0 && dy * dy < r2) {
                float dx = sqrtf(r2 - dy * dy);
                int inset = iradius - (int) floorf(dx);
                if (inset > 0) {
                    x_start = ix + inset;
                    x_end = ix + iw - inset;
                }
            }
        } else if (row >= ih - iradius) {
            float dy = (float) (row - (ih - iradius - 1)) - 0.5f;
            if (dy > 0 && dy * dy < r2) {
                float dx = sqrtf(r2 - dy * dy);
                int inset = iradius - (int) floorf(dx);
                if (inset > 0) {
                    x_start = ix + inset;
                    x_end = ix + iw - inset;
                }
            }
        }

        if (x_end > x_start)
            SDL_RenderDrawLine(r, x_start, line_y, x_end - 1, line_y);
    }
}

/* Renderer Callbacks (iui_renderer_t implementation) */

static void sdl2_draw_box(iui_rect_t rect,
                          float radius,
                          uint32_t srgb_color,
                          void *user)
{
    iui_port_ctx *ctx = (iui_port_ctx *) user;
    set_color(ctx->renderer, srgb_color);
    draw_rounded_rect_scaled(ctx->renderer, rect.x, rect.y, rect.width,
                             rect.height, radius, ctx->scale);
}

static void sdl2_set_clip_rect(uint16_t min_x,
                               uint16_t min_y,
                               uint16_t max_x,
                               uint16_t max_y,
                               void *user)
{
    iui_port_ctx *ctx = (iui_port_ctx *) user;

    if (min_x == 0 && min_y == 0 && max_x == UINT16_MAX &&
        max_y == UINT16_MAX) {
        SDL_RenderSetClipRect(ctx->renderer, NULL);
    } else {
        SDL_Rect clip = {(int) (min_x * ctx->scale), (int) (min_y * ctx->scale),
                         (int) ((max_x - min_x) * ctx->scale),
                         (int) ((max_y - min_y) * ctx->scale)};
        SDL_RenderSetClipRect(ctx->renderer, &clip);
    }
}

static void sdl2_draw_line(float x0,
                           float y0,
                           float x1,
                           float y1,
                           float width,
                           uint32_t srgb_color,
                           void *user)
{
    iui_port_ctx *ctx = (iui_port_ctx *) user;
    set_color(ctx->renderer, srgb_color);

    float sx0 = x0 * ctx->scale, sy0 = y0 * ctx->scale;
    float sx1 = x1 * ctx->scale, sy1 = y1 * ctx->scale;
    float sw = width * ctx->scale;

    if (sw <= 1.5f) {
        SDL_RenderDrawLineF(ctx->renderer, sx0, sy0, sx1, sy1);
    } else {
        float dx = sx1 - sx0, dy = sy1 - sy0;
        float len = sqrtf(dx * dx + dy * dy);
        if (len < 0.001f)
            return;

        float px = -dy / len, py = dx / len;

        int thickness = (int) (sw + 0.5f);
        for (int i = -thickness / 2; i <= thickness / 2; i++) {
            float offset = (float) i;
            SDL_RenderDrawLineF(ctx->renderer, sx0 + px * offset,
                                sy0 + py * offset, sx1 + px * offset,
                                sy1 + py * offset);
        }
    }
}

static void sdl2_draw_circle(float cx,
                             float cy,
                             float radius,
                             uint32_t fill_color,
                             uint32_t stroke_color,
                             float stroke_width,
                             void *user)
{
    iui_port_ctx *ctx = (iui_port_ctx *) user;

    float scx = cx * ctx->scale;
    float scy = cy * ctx->scale;
    float sr = radius * ctx->scale;
    float sw = stroke_width * ctx->scale;

    if (fill_color != 0) {
        set_color(ctx->renderer, fill_color);
        int ir = (int) (sr + 0.5f);
        for (int y = -ir; y <= ir; y++) {
            float fy = (float) y;
            float half_width = sqrtf(sr * sr - fy * fy);
            int x_start = (int) (scx - half_width);
            int x_end = (int) (scx + half_width);
            SDL_RenderDrawLine(ctx->renderer, x_start, (int) scy + y, x_end,
                               (int) scy + y);
        }
    }

    if (stroke_color != 0 && sw > 0.f) {
        set_color(ctx->renderer, stroke_color);

        int segments = IUI_CIRCLE_SEGMENTS(sr);

        float angle_step = (float) IUI_PORT_PI * 2.f / (float) segments;
        float prev_x = scx + sr;
        float prev_y = scy;

        for (int i = 1; i <= segments; i++) {
            float angle = angle_step * (float) i;
            float curr_x = scx + cosf(angle) * sr;
            float curr_y = scy + sinf(angle) * sr;

            if (sw <= 1.5f) {
                SDL_RenderDrawLineF(ctx->renderer, prev_x, prev_y, curr_x,
                                    curr_y);
            } else {
                int thickness = (int) (sw + 0.5f);
                float dx = curr_x - prev_x;
                float dy = curr_y - prev_y;
                float len = sqrtf(dx * dx + dy * dy);
                if (len > 0.001f) {
                    float px = -dy / len;
                    float py = dx / len;
                    for (int j = -thickness / 2; j <= thickness / 2; j++) {
                        float offset = (float) j;
                        SDL_RenderDrawLineF(ctx->renderer, prev_x + px * offset,
                                            prev_y + py * offset,
                                            curr_x + px * offset,
                                            curr_y + py * offset);
                    }
                }
            }
            prev_x = curr_x;
            prev_y = curr_y;
        }
    }
}

static void sdl2_draw_arc(float cx,
                          float cy,
                          float radius,
                          float start_angle,
                          float end_angle,
                          float width,
                          uint32_t srgb_color,
                          void *user)
{
    iui_port_ctx *ctx = (iui_port_ctx *) user;
    set_color(ctx->renderer, srgb_color);

    float scx = cx * ctx->scale, scy = cy * ctx->scale;
    float sr = radius * ctx->scale;
    float sw = width * ctx->scale;

    float arc_angle = end_angle - start_angle;
    if (arc_angle < 0)
        arc_angle += (float) IUI_PORT_PI * 2.f;

    int segments = IUI_ARC_SEGMENTS(sr, arc_angle);

    float angle_step = arc_angle / (float) segments;
    float prev_x = scx + cosf(start_angle) * sr;
    float prev_y = scy + sinf(start_angle) * sr;

    for (int i = 1; i <= segments; i++) {
        float angle = start_angle + angle_step * (float) i;
        float curr_x = scx + cosf(angle) * sr;
        float curr_y = scy + sinf(angle) * sr;

        if (sw <= 1.5f) {
            SDL_RenderDrawLineF(ctx->renderer, prev_x, prev_y, curr_x, curr_y);
        } else {
            int thickness = (int) (sw + 0.5f);
            float dx = curr_x - prev_x;
            float dy = curr_y - prev_y;
            float len = sqrtf(dx * dx + dy * dy);
            if (len > 0.001f) {
                float px = -dy / len;
                float py = dx / len;
                for (int j = -thickness / 2; j <= thickness / 2; j++) {
                    float offset = (float) j;
                    SDL_RenderDrawLineF(ctx->renderer, prev_x + px * offset,
                                        prev_y + py * offset,
                                        curr_x + px * offset,
                                        curr_y + py * offset);
                }
            }
        }
        prev_x = curr_x;
        prev_y = curr_y;
    }
}

/* Vector Font Callbacks (iui_vector_t implementation)
 *
 * Uses shared path state and Bezier tessellation from port-sw.h.
 * Path points are stored in physical (scaled) coordinates for direct
 * rendering to SDL_Renderer without per-point scaling overhead.
 */

static void sdl2_path_move(float x, float y, void *user)
{
    iui_port_ctx *ctx = (iui_port_ctx *) user;
    iui_path_move_to_scaled(&ctx->path, x, y, ctx->scale);
}

static void sdl2_path_line(float x, float y, void *user)
{
    iui_port_ctx *ctx = (iui_port_ctx *) user;
    iui_path_line_to_scaled(&ctx->path, x, y, ctx->scale);
}

static void sdl2_path_curve(float x1,
                            float y1,
                            float x2,
                            float y2,
                            float x3,
                            float y3,
                            void *user)
{
    iui_port_ctx *ctx = (iui_port_ctx *) user;
    iui_path_curve_to_scaled(&ctx->path, x1, y1, x2, y2, x3, y3, ctx->scale);
}

static void sdl2_path_stroke(float width, uint32_t color, void *user)
{
    iui_port_ctx *ctx = (iui_port_ctx *) user;

    if (ctx->path.count < 2) {
        iui_path_reset(&ctx->path);
        return;
    }

    set_color(ctx->renderer, color);

    float hw = width * ctx->scale * 0.5f;
    if (hw < 0.5f)
        hw = 0.5f;

    int thickness = (int) (hw * 2 + 0.5f);
    if (thickness < 1)
        thickness = 1;

    /* Draw path with thickness using Y-offset parallel lines.
     * Note: Y-only offset works well for mostly-horizontal text strokes.
     * For diagonal/vertical strokes, thickness may appear reduced.
     */
    for (int offset = -thickness / 2; offset <= thickness / 2; offset++) {
        for (int i = 0; i < ctx->path.count - 1; i++) {
            SDL_RenderDrawLineF(ctx->renderer, ctx->path.points_x[i],
                                ctx->path.points_y[i] + (float) offset,
                                ctx->path.points_x[i + 1],
                                ctx->path.points_y[i + 1] + (float) offset);
        }
    }

    iui_path_reset(&ctx->path);
}

/* Port Interface Implementation (iui_port_t) */

static iui_port_ctx *sdl2_init(int width, int height, const char *title)
{
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return NULL;
    }

    SDL_Window *window = SDL_CreateWindow(
        title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height,
        SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return NULL;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(
        window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return NULL;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_StartTextInput();

    iui_port_ctx *ctx = (iui_port_ctx *) calloc(1, sizeof(iui_port_ctx));
    if (!ctx) {
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return NULL;
    }

    ctx->window = window;
    ctx->renderer = renderer;
    ctx->running = true;
    ctx->exit_requested = false;
    ctx->last_frame_ticks = SDL_GetPerformanceCounter();

    return ctx;
}

static void sdl2_shutdown(iui_port_ctx *ctx)
{
    if (!ctx)
        return;

    SDL_StopTextInput();
    if (ctx->renderer)
        SDL_DestroyRenderer(ctx->renderer);
    if (ctx->window)
        SDL_DestroyWindow(ctx->window);
    SDL_Quit();
    free(ctx);
}

static void sdl2_configure(iui_port_ctx *ctx)
{
    if (!ctx)
        return;

    /* Calculate HiDPI scale factor */
    int window_w, window_h, render_w, render_h;
    SDL_GetWindowSize(ctx->window, &window_w, &window_h);
    SDL_GetRendererOutputSize(ctx->renderer, &render_w, &render_h);
    ctx->scale = (float) render_w / (float) window_w;

    /* Set default font height */
    float logical_font_size = 14.0f;
    ctx->font_height = logical_font_size * 1.5f;

    /* Initialize renderer callbacks with port context as user data */
    ctx->render_ops.draw_box = sdl2_draw_box;
    ctx->render_ops.draw_text = NULL;  /* Use vector font */
    ctx->render_ops.text_width = NULL; /* Use vector font */
    ctx->render_ops.set_clip_rect = sdl2_set_clip_rect;
    ctx->render_ops.draw_line = sdl2_draw_line;
    ctx->render_ops.draw_circle = sdl2_draw_circle;
    ctx->render_ops.draw_arc = sdl2_draw_arc;
    ctx->render_ops.user = ctx;

    /* Initialize vector callbacks */
    ctx->vector_ops.path_move = sdl2_path_move;
    ctx->vector_ops.path_line = sdl2_path_line;
    ctx->vector_ops.path_curve = sdl2_path_curve;
    ctx->vector_ops.path_stroke = sdl2_path_stroke;
}

static bool sdl2_poll_events(iui_port_ctx *ctx)
{
    if (!ctx)
        return false;

    /* Reset per-frame input state */
    ctx->queued_input.mouse_pressed = 0;
    ctx->queued_input.mouse_released = 0;
    ctx->queued_input.key = 0;
    ctx->queued_input.text = 0;
    ctx->queued_input.scroll_x = 0;
    ctx->queued_input.scroll_y = 0;
    ctx->queued_input.shift_down = false;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_QUIT:
            ctx->running = false;
            ctx->exit_requested = true;
            break;

        case SDL_MOUSEMOTION:
            ctx->queued_input.mouse_x = (float) event.motion.x;
            ctx->queued_input.mouse_y = (float) event.motion.y;
            break;

        case SDL_MOUSEBUTTONDOWN:
            ctx->queued_input.mouse_x = (float) event.button.x;
            ctx->queued_input.mouse_y = (float) event.button.y;
            if (event.button.button == SDL_BUTTON_LEFT) {
                ctx->queued_input.mouse_pressed |= IUI_MOUSE_LEFT;
            } else if (event.button.button == SDL_BUTTON_RIGHT) {
                ctx->queued_input.mouse_pressed |= IUI_MOUSE_RIGHT;
            } else if (event.button.button == SDL_BUTTON_MIDDLE) {
                ctx->queued_input.mouse_pressed |= IUI_MOUSE_MIDDLE;
            }
            break;

        case SDL_MOUSEBUTTONUP:
            /* Preserve click position on quick clicks (down+up same frame) */
            if (ctx->queued_input.mouse_pressed == 0) {
                ctx->queued_input.mouse_x = (float) event.button.x;
                ctx->queued_input.mouse_y = (float) event.button.y;
            }
            if (event.button.button == SDL_BUTTON_LEFT) {
                ctx->queued_input.mouse_released |= IUI_MOUSE_LEFT;
            } else if (event.button.button == SDL_BUTTON_RIGHT) {
                ctx->queued_input.mouse_released |= IUI_MOUSE_RIGHT;
            } else if (event.button.button == SDL_BUTTON_MIDDLE) {
                ctx->queued_input.mouse_released |= IUI_MOUSE_MIDDLE;
            }
            break;

        case SDL_KEYDOWN: {
            SDL_Keycode key = event.key.keysym.sym;
            if (event.key.keysym.mod & KMOD_SHIFT)
                ctx->queued_input.shift_down = true;

            /* First key per frame only */
            if (ctx->queued_input.key != 0)
                break;

            if (key == SDLK_BACKSPACE) {
                ctx->queued_input.key = IUI_KEY_BACKSPACE;
            } else if (key == SDLK_DELETE) {
                ctx->queued_input.key = IUI_KEY_DELETE;
            } else if (key == SDLK_LEFT) {
                ctx->queued_input.key = IUI_KEY_LEFT;
            } else if (key == SDLK_RIGHT) {
                ctx->queued_input.key = IUI_KEY_RIGHT;
            } else if (key == SDLK_HOME) {
                ctx->queued_input.key = IUI_KEY_HOME;
            } else if (key == SDLK_END) {
                ctx->queued_input.key = IUI_KEY_END;
            } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                ctx->queued_input.key = IUI_KEY_ENTER;
            } else if (key == SDLK_TAB) {
                ctx->queued_input.key = IUI_KEY_TAB;
            } else if (key == SDLK_ESCAPE) {
                ctx->queued_input.key = IUI_KEY_ESCAPE;
            } else if (key == SDLK_UP) {
                ctx->queued_input.key = IUI_KEY_UP;
            } else if (key == SDLK_DOWN) {
                ctx->queued_input.key = IUI_KEY_DOWN;
            }
            break;
        }

        case SDL_TEXTINPUT:
            /* First text event per frame only */
            if (ctx->queued_input.text == 0 && event.text.text[0] != '\0')
                ctx->queued_input.text = (uint32_t) event.text.text[0];
            break;

        case SDL_MOUSEWHEEL:
            /* Accumulate scroll deltas */
            ctx->queued_input.scroll_x += (float) event.wheel.x * 20.f;
            ctx->queued_input.scroll_y += (float) event.wheel.y * -20.f;
            break;
        }
    }

    /* Update delta time */
    Uint64 now = SDL_GetPerformanceCounter();
    ctx->delta_time = (float) (now - ctx->last_frame_ticks) /
                      (float) SDL_GetPerformanceFrequency();
    ctx->last_frame_ticks = now;

    /* Cap delta time to prevent jumps (debugger breakpoints) */
    if (ctx->delta_time > 0.1f)
        ctx->delta_time = 0.016f;

    return ctx->running;
}

static bool sdl2_should_exit(iui_port_ctx *ctx)
{
    return ctx ? ctx->exit_requested : true;
}

static void sdl2_request_exit(iui_port_ctx *ctx)
{
    if (ctx)
        iui_port_request_exit(&ctx->running, &ctx->exit_requested);
}

static void sdl2_get_input(iui_port_ctx *ctx, iui_port_input *input)
{
    if (!ctx || !input)
        return;
    *input = ctx->queued_input;
}

static void sdl2_begin_frame(iui_port_ctx *ctx)
{
    if (!ctx)
        return;

    /* Clear with dark background */
    SDL_SetRenderDrawColor(ctx->renderer, 40, 44, 52, 255);
    SDL_RenderClear(ctx->renderer);
}

static void sdl2_end_frame(iui_port_ctx *ctx)
{
    if (!ctx)
        return;
    SDL_RenderPresent(ctx->renderer);
}

static iui_renderer_t sdl2_get_renderer_callbacks(iui_port_ctx *ctx)
{
    if (!ctx) {
        iui_renderer_t empty = {0};
        return empty;
    }
    return ctx->render_ops;
}

static const iui_vector_t *sdl2_get_vector_callbacks(iui_port_ctx *ctx)
{
    if (!ctx)
        return NULL;
    return &ctx->vector_ops;
}

static float sdl2_get_delta_time(iui_port_ctx *ctx)
{
    return ctx ? ctx->delta_time : 0.016f;
}

static void sdl2_get_window_size(iui_port_ctx *ctx, int *width, int *height)
{
    if (!ctx)
        return;
    SDL_GetWindowSize(ctx->window, width, height);
}

static void sdl2_set_window_size(iui_port_ctx *ctx, int width, int height)
{
    if (!ctx)
        return;
    SDL_SetWindowSize(ctx->window, width, height);
}

static float sdl2_get_dpi_scale(iui_port_ctx *ctx)
{
    return ctx ? ctx->scale : 1.0f;
}

static bool sdl2_is_window_focused(iui_port_ctx *ctx)
{
    if (!ctx)
        return false;
    Uint32 flags = SDL_GetWindowFlags(ctx->window);
    return (flags & SDL_WINDOW_INPUT_FOCUS) != 0;
}

static bool sdl2_is_window_visible(iui_port_ctx *ctx)
{
    if (!ctx)
        return false;
    Uint32 flags = SDL_GetWindowFlags(ctx->window);
    return (flags & SDL_WINDOW_MINIMIZED) == 0;
}

static const char *sdl2_get_clipboard_text(iui_port_ctx *ctx)
{
    (void) ctx;
    return SDL_GetClipboardText();
}

static void sdl2_set_clipboard_text(iui_port_ctx *ctx, const char *text)
{
    (void) ctx;
    SDL_SetClipboardText(text);
}

static void *sdl2_get_native_renderer(iui_port_ctx *ctx)
{
    return ctx ? ctx->renderer : NULL;
}

/* Global Backend Instance */
const iui_port_t g_iui_port = {
    .init = sdl2_init,
    .shutdown = sdl2_shutdown,
    .configure = sdl2_configure,
    .poll_events = sdl2_poll_events,
    .should_exit = sdl2_should_exit,
    .request_exit = sdl2_request_exit,
    .get_input = sdl2_get_input,
    .begin_frame = sdl2_begin_frame,
    .end_frame = sdl2_end_frame,
    .get_renderer_callbacks = sdl2_get_renderer_callbacks,
    .get_vector_callbacks = sdl2_get_vector_callbacks,
    .get_delta_time = sdl2_get_delta_time,
    .get_window_size = sdl2_get_window_size,
    .set_window_size = sdl2_set_window_size,
    .get_dpi_scale = sdl2_get_dpi_scale,
    .is_window_focused = sdl2_is_window_focused,
    .is_window_visible = sdl2_is_window_visible,
    .get_clipboard_text = sdl2_get_clipboard_text,
    .set_clipboard_text = sdl2_set_clipboard_text,
    .get_native_renderer = sdl2_get_native_renderer,
};
