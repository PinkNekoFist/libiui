/*
 * ports/port-sw.h - Software Rendering Utilities for Framebuffer-Based Ports
 *
 * Consolidated header providing:
 *   - Color manipulation and alpha blending (ARGB32)
 *   - Pixel-level drawing operations (rasterizer)
 *   - Vector path tessellation (Bezier curves)
 *
 * Architecture:
 *   This header is used by ports that perform software rendering to a
 *   framebuffer (headless.c, wasm.c). Ports using hardware acceleration
 *   (sdl2.c) can still use the path/Bezier utilities but handle primitive
 *   drawing through their native API (SDL_Renderer).
 *
 *   Components:
 *   1. Color functions (iui_color_*, iui_make_color, iui_blend_*)
 *      - Used by all software-rendering ports
 *      - Aliased by headless.h for test API consistency
 *
 *   2. Rasterizer (iui_raster_*)
 *      - Full software rasterizer with clipping and anti-aliasing
 *      - Used by headless.c and wasm.c
 *      - NOT used by sdl2.c (uses SDL_Renderer instead)
 *
 *   3. Path state and Bezier tessellation (iui_path_*)
 *      - Shared by ALL ports for vector font rendering
 *      - sdl2.c uses _scaled variants for HiDPI support
 *      - headless.c/wasm.c use unscaled variants
 *
 * Requirements:
 *   - Framebuffer in ARGB32 format (for rasterizer functions)
 */

#ifndef IUI_PORT_SW_H
#define IUI_PORT_SW_H

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#include "port.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Color Manipulation (ARGB32 format) */

static inline uint8_t iui_color_alpha(uint32_t c)
{
    return (c >> 24) & 0xFF;
}

static inline uint8_t iui_color_red(uint32_t c)
{
    return (c >> 16) & 0xFF;
}

static inline uint8_t iui_color_green(uint32_t c)
{
    return (c >> 8) & 0xFF;
}

static inline uint8_t iui_color_blue(uint32_t c)
{
    return c & 0xFF;
}

static inline uint32_t iui_make_color(uint8_t r,
                                      uint8_t g,
                                      uint8_t b,
                                      uint8_t a)
{
    return ((uint32_t) a << 24) | ((uint32_t) r << 16) | ((uint32_t) g << 8) |
           b;
}

/* Alpha Blending: blend src over dst using standard Porter-Duff "over" */
static inline uint32_t iui_blend_pixel(uint32_t dst, uint32_t src)
{
    uint8_t sa = iui_color_alpha(src);
    if (sa == 0)
        return dst;
    if (sa == 255)
        return src;

    uint8_t da = iui_color_alpha(dst);
    uint8_t sr = iui_color_red(src), sg = iui_color_green(src),
            sb = iui_color_blue(src);
    uint8_t dr = iui_color_red(dst), dg = iui_color_green(dst),
            db = iui_color_blue(dst);

    /* Standard alpha compositing: out = src + dst * (1 - src_alpha) */
    uint32_t inv_sa = 255 - sa;
    uint8_t out_r = (uint8_t) ((sr * sa + dr * inv_sa) / 255);
    uint8_t out_g = (uint8_t) ((sg * sa + dg * inv_sa) / 255);
    uint8_t out_b = (uint8_t) ((sb * sa + db * inv_sa) / 255);
    uint8_t out_a = (uint8_t) (sa + (da * inv_sa) / 255);

    return iui_make_color(out_r, out_g, out_b, out_a);
}

/* Blend pixel with fractional alpha (for anti-aliasing) */
static inline uint32_t iui_blend_aa(uint32_t dst,
                                    uint32_t color,
                                    float brightness)
{
    if (brightness <= 0.0f)
        return dst;
    if (brightness > 1.0f)
        brightness = 1.0f;

    uint8_t base_alpha = iui_color_alpha(color);
    uint8_t new_alpha = (uint8_t) (base_alpha * brightness);
    uint32_t aa_color = (new_alpha << 24) | (color & 0x00FFFFFF);
    return iui_blend_pixel(dst, aa_color);
}

/* Rasterizer Context and Primitives */

/* Rasterizer context - minimal state for drawing operations */
typedef struct {
    uint32_t *framebuffer;
    int width, height;
    int clip_min_x, clip_min_y;
    int clip_max_x, clip_max_y;
    uint64_t pixels_drawn; /* Optional counter for profiling */
} iui_raster_ctx_t;

/* Initialize raster context with full-screen clipping */
static inline void iui_raster_init(iui_raster_ctx_t *r,
                                   uint32_t *fb,
                                   int w,
                                   int h)
{
    r->framebuffer = fb;
    r->width = w;
    r->height = h;
    r->clip_min_x = 0;
    r->clip_min_y = 0;
    r->clip_max_x = w;
    r->clip_max_y = h;
    r->pixels_drawn = 0;
}

/* Set clipping rectangle */
static inline void iui_raster_set_clip(iui_raster_ctx_t *r,
                                       int min_x,
                                       int min_y,
                                       int max_x,
                                       int max_y)
{
    r->clip_min_x = min_x < 0 ? 0 : min_x;
    r->clip_min_y = min_y < 0 ? 0 : min_y;
    r->clip_max_x = max_x > r->width ? r->width : max_x;
    r->clip_max_y = max_y > r->height ? r->height : max_y;
}

/* Reset clipping to full framebuffer */
static inline void iui_raster_reset_clip(iui_raster_ctx_t *r)
{
    r->clip_min_x = 0;
    r->clip_min_y = 0;
    r->clip_max_x = r->width;
    r->clip_max_y = r->height;
}

/* Set pixel with clipping and alpha blending */
static inline void iui_raster_pixel(iui_raster_ctx_t *r,
                                    int x,
                                    int y,
                                    uint32_t color)
{
    if (x < r->clip_min_x || x >= r->clip_max_x || y < r->clip_min_y ||
        y >= r->clip_max_y)
        return;

    size_t idx = (size_t) y * (size_t) r->width + (size_t) x;
    r->framebuffer[idx] = iui_blend_pixel(r->framebuffer[idx], color);
    r->pixels_drawn++;
}

/* Set pixel with anti-aliasing brightness factor */
static inline void iui_raster_pixel_aa(iui_raster_ctx_t *r,
                                       int x,
                                       int y,
                                       uint32_t color,
                                       float brightness)
{
    if (brightness <= 0.0f)
        return;
    if (x < r->clip_min_x || x >= r->clip_max_x || y < r->clip_min_y ||
        y >= r->clip_max_y)
        return;

    size_t idx = (size_t) y * (size_t) r->width + (size_t) x;
    r->framebuffer[idx] = iui_blend_aa(r->framebuffer[idx], color, brightness);
    r->pixels_drawn++;
}

/* Draw horizontal line with clipping */
static inline void iui_raster_hline(iui_raster_ctx_t *r,
                                    int x0,
                                    int x1,
                                    int y,
                                    uint32_t color)
{
    if (y < r->clip_min_y || y >= r->clip_max_y)
        return;

    if (x0 > x1) {
        int tmp = x0;
        x0 = x1;
        x1 = tmp;
    }

    int start = x0 < r->clip_min_x ? r->clip_min_x : x0;
    int end = x1 >= r->clip_max_x ? r->clip_max_x - 1 : x1;

    uint8_t sa = iui_color_alpha(color);
    if (sa == 0)
        return;

    if (start > end)
        return;

    int count = end - start + 1;
    uint32_t *row = &r->framebuffer[(size_t) y * (size_t) r->width];

    if (sa == 255) {
        for (int x = start; x <= end; x++)
            row[x] = color;
    } else {
        for (int x = start; x <= end; x++)
            row[x] = iui_blend_pixel(row[x], color);
    }
    r->pixels_drawn += (uint64_t) count;
}

/* Fill rectangle (no rounding) */
static inline void iui_raster_fill_rect(iui_raster_ctx_t *r,
                                        int x,
                                        int y,
                                        int w,
                                        int h,
                                        uint32_t color)
{
    for (int row = 0; row < h; row++)
        iui_raster_hline(r, x, x + w - 1, y + row, color);
}

/* Fill rounded rectangle with anti-aliased corners */
static inline void iui_raster_rounded_rect(iui_raster_ctx_t *r,
                                           float fx,
                                           float fy,
                                           float fw,
                                           float fh,
                                           float radius,
                                           uint32_t color)
{
    int x = (int) floorf(fx), y = (int) floorf(fy);
    int w = (int) ceilf(fx + fw) - x, h = (int) ceilf(fy + fh) - y;

    if (w <= 0 || h <= 0)
        return;

    if (radius <= 0.5f) {
        iui_raster_fill_rect(r, x, y, w, h, color);
        return;
    }

    /* Clamp radius to half of smaller dimension */
    if (radius > (float) w / 2.0f)
        radius = (float) w / 2.0f;
    if (radius > (float) h / 2.0f)
        radius = (float) h / 2.0f;

    float r2 = radius * radius;
    int ir = (int) ceilf(radius);

    for (int row = 0; row < h; row++) {
        int line_y = y + row;
        int x_start = x;
        int x_end = x + w - 1;
        float aa_left = 0.0f, aa_right = 0.0f;

        if (row < ir) {
            /* Top rounded corners */
            float dy = radius - (float) row - 0.5f;
            if (dy > 0.0f) {
                float dy2 = dy * dy;
                if (dy2 < r2) {
                    float dx = sqrtf(r2 - dy2);
                    float inset_f = radius - dx;
                    int inset = (int) floorf(inset_f);
                    aa_left = inset_f - (float) inset;
                    aa_right = aa_left;
                    if (inset >= 0) {
                        x_start = x + inset + 1;
                        x_end = x + w - 1 - inset - 1;
                    }
                } else {
                    continue;
                }
            }
        } else if (row >= h - ir) {
            /* Bottom rounded corners */
            float dy = (float) row - (float) (h - 1) + radius - 0.5f;
            if (dy > 0.0f) {
                float dy2 = dy * dy;
                if (dy2 < r2) {
                    float dx = sqrtf(r2 - dy2);
                    float inset_f = radius - dx;
                    int inset = (int) floorf(inset_f);
                    aa_left = inset_f - (float) inset;
                    aa_right = aa_left;
                    if (inset >= 0) {
                        x_start = x + inset + 1;
                        x_end = x + w - 1 - inset - 1;
                    }
                } else {
                    continue;
                }
            }
        }

        if (x_start <= x_end)
            iui_raster_hline(r, x_start, x_end, line_y, color);

        if (aa_left > 0.01f && x_start > x)
            iui_raster_pixel_aa(r, x_start - 1, line_y, color, 1.0f - aa_left);
        if (aa_right > 0.01f && x_end < x + w - 1)
            iui_raster_pixel_aa(r, x_end + 1, line_y, color, 1.0f - aa_right);
    }
}

/* Xiaolin Wu's anti-aliased line algorithm (single pixel width) */
static inline void iui_raster_line_aa(iui_raster_ctx_t *r,
                                      float x0,
                                      float y0,
                                      float x1,
                                      float y1,
                                      uint32_t color)
{
    float dx = x1 - x0;
    float dy = y1 - y0;

    if (fabsf(dx) < 0.001f && fabsf(dy) < 0.001f) {
        iui_raster_pixel_aa(r, (int) roundf(x0), (int) roundf(y0), color, 1.0f);
        return;
    }

    int steep = fabsf(dy) > fabsf(dx);

    if (steep) {
        float t;
        t = x0;
        x0 = y0;
        y0 = t;
        t = x1;
        x1 = y1;
        y1 = t;
        t = dx;
        dx = dy;
        dy = t;
    }

    if (x0 > x1) {
        float t;
        t = x0;
        x0 = x1;
        x1 = t;
        t = y0;
        y0 = y1;
        y1 = t;
    }

    dx = x1 - x0;
    float gradient = (fabsf(dx) < 0.001f) ? 0.0f : dy / dx;

    /* First endpoint */
    float xend = roundf(x0);
    float yend = y0 + gradient * (xend - x0);
    float xgap = 1.0f - (x0 + 0.5f - floorf(x0 + 0.5f));
    int xpxl1 = (int) xend;
    int ypxl1 = (int) floorf(yend);
    float fpart1 = yend - floorf(yend);

    if (steep) {
        iui_raster_pixel_aa(r, ypxl1, xpxl1, color, (1.0f - fpart1) * xgap);
        iui_raster_pixel_aa(r, ypxl1 + 1, xpxl1, color, fpart1 * xgap);
    } else {
        iui_raster_pixel_aa(r, xpxl1, ypxl1, color, (1.0f - fpart1) * xgap);
        iui_raster_pixel_aa(r, xpxl1, ypxl1 + 1, color, fpart1 * xgap);
    }

    float intery = yend + gradient;

    /* Second endpoint */
    xend = roundf(x1);
    yend = y1 + gradient * (xend - x1);
    xgap = x1 + 0.5f - floorf(x1 + 0.5f);
    int xpxl2 = (int) xend;
    int ypxl2 = (int) floorf(yend);
    float fpart2 = yend - floorf(yend);

    if (steep) {
        iui_raster_pixel_aa(r, ypxl2, xpxl2, color, (1.0f - fpart2) * xgap);
        iui_raster_pixel_aa(r, ypxl2 + 1, xpxl2, color, fpart2 * xgap);
    } else {
        iui_raster_pixel_aa(r, xpxl2, ypxl2, color, (1.0f - fpart2) * xgap);
        iui_raster_pixel_aa(r, xpxl2, ypxl2 + 1, color, fpart2 * xgap);
    }

    /* Main loop */
    if (steep) {
        for (int px = xpxl1 + 1; px < xpxl2; px++) {
            int iy = (int) floorf(intery);
            float fpart = intery - (float) iy;
            iui_raster_pixel_aa(r, iy, px, color, 1.0f - fpart);
            iui_raster_pixel_aa(r, iy + 1, px, color, fpart);
            intery += gradient;
        }
    } else {
        for (int px = xpxl1 + 1; px < xpxl2; px++) {
            int iy = (int) floorf(intery);
            float fpart = intery - (float) iy;
            iui_raster_pixel_aa(r, px, iy, color, 1.0f - fpart);
            iui_raster_pixel_aa(r, px, iy + 1, color, fpart);
            intery += gradient;
        }
    }
}

/* Draw line with thickness using parallel anti-aliased lines */
static inline void iui_raster_line(iui_raster_ctx_t *r,
                                   float x0,
                                   float y0,
                                   float x1,
                                   float y1,
                                   float width,
                                   uint32_t color)
{
    /* Single AA line for thin strokes */
    if (width <= 1.0f) {
        iui_raster_line_aa(r, x0, y0, x1, y1, color);
        return;
    }

    float dx = x1 - x0;
    float dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);

    if (len < 0.001f) {
        iui_raster_pixel(r, (int) x0, (int) y0, color);
        return;
    }

    /* Perpendicular unit vector */
    float px = -dy / len;
    float py = dx / len;
    float half_w = width / 2.0f;

    int num_lines = (int) (width + 0.5f);
    if (num_lines < 2)
        num_lines = 2;

    for (int i = 0; i < num_lines; i++) {
        float offset = -half_w + (width * i) / (num_lines - 1);
        float ox = px * offset;
        float oy = py * offset;
        iui_raster_line_aa(r, x0 + ox, y0 + oy, x1 + ox, y1 + oy, color);
    }
}

/* Bresenham line algorithm with thickness (no anti-aliasing, faster) */
static inline void iui_raster_line_bresenham(iui_raster_ctx_t *r,
                                             float fx0,
                                             float fy0,
                                             float fx1,
                                             float fy1,
                                             float width,
                                             uint32_t color)
{
    int x0 = (int) roundf(fx0);
    int y0 = (int) roundf(fy0);
    int x1 = (int) roundf(fx1);
    int y1 = (int) roundf(fy1);

    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;

    int thickness = (int) (width + 0.5f);
    if (thickness < 1)
        thickness = 1;
    int half_thick = thickness / 2;

    while (1) {
        for (int ty = -half_thick; ty <= half_thick; ty++) {
            for (int tx = -half_thick; tx <= half_thick; tx++)
                iui_raster_pixel(r, x0 + tx, y0 + ty, color);
        }

        if (x0 == x1 && y0 == y1)
            break;

        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

/* Fill circle with anti-aliased edges */
static inline void iui_raster_circle_fill(iui_raster_ctx_t *r,
                                          float cx,
                                          float cy,
                                          float radius,
                                          uint32_t color)
{
    if (radius <= 0.5f)
        return;

    float r2 = radius * radius;
    int ir = (int) ceilf(radius);

    for (int y = -ir; y <= ir; y++) {
        float fy = (float) y;
        float dy2 = fy * fy;

        if (dy2 > r2)
            continue;

        float x_extent = sqrtf(r2 - dy2);
        float left_edge = cx - x_extent;
        float right_edge = cx + x_extent;

        int x_left = (int) floorf(left_edge);
        int x_right = (int) ceilf(right_edge);
        int iy = (int) cy + y;

        float left_coverage = 1.0f - (left_edge - (float) x_left);
        float right_coverage = right_edge - floorf(right_edge);

        if (left_coverage > 1.0f)
            left_coverage = 1.0f;
        if (right_coverage > 1.0f)
            right_coverage = 1.0f;

        if (left_coverage > 0.01f)
            iui_raster_pixel_aa(r, x_left, iy, color, left_coverage);

        if (x_left + 1 <= x_right - 1)
            iui_raster_hline(r, x_left + 1, x_right - 1, iy, color);

        if (x_right != x_left && right_coverage > 0.01f)
            iui_raster_pixel_aa(r, x_right, iy, color, right_coverage);
    }
}

/* Stroke circle outline using line segments */
static inline void iui_raster_circle_stroke(iui_raster_ctx_t *r,
                                            float cx,
                                            float cy,
                                            float radius,
                                            float width,
                                            uint32_t color)
{
    int segments = IUI_CIRCLE_SEGMENTS(radius);
    float angle_step = (float) IUI_PORT_PI * 2.f / (float) segments;

    float prev_x = cx + radius;
    float prev_y = cy;

    for (int i = 1; i <= segments; i++) {
        float angle = angle_step * (float) i;
        float curr_x = cx + cosf(angle) * radius;
        float curr_y = cy + sinf(angle) * radius;

        iui_raster_line(r, prev_x, prev_y, curr_x, curr_y, width, color);
        prev_x = curr_x;
        prev_y = curr_y;
    }
}

/* Draw arc using line segments */
static inline void iui_raster_arc(iui_raster_ctx_t *r,
                                  float cx,
                                  float cy,
                                  float radius,
                                  float start_angle,
                                  float end_angle,
                                  float width,
                                  uint32_t color)
{
    float arc_angle = end_angle - start_angle;
    if (arc_angle < 0)
        arc_angle += (float) IUI_PORT_PI * 2.f;

    int segments = IUI_ARC_SEGMENTS(radius, arc_angle);
    float angle_step = arc_angle / (float) segments;

    float prev_x = cx + cosf(start_angle) * radius;
    float prev_y = cy + sinf(start_angle) * radius;

    for (int i = 1; i <= segments; i++) {
        float angle = start_angle + angle_step * (float) i;
        float curr_x = cx + cosf(angle) * radius;
        float curr_y = cy + sinf(angle) * radius;

        iui_raster_line(r, prev_x, prev_y, curr_x, curr_y, width, color);
        prev_x = curr_x;
        prev_y = curr_y;
    }
}

/* Clear framebuffer to a solid color */
static inline void iui_raster_clear(iui_raster_ctx_t *r, uint32_t color)
{
    size_t count = (size_t) r->width * (size_t) r->height;
    for (size_t i = 0; i < count; i++)
        r->framebuffer[i] = color;
}

/* Vector Path State and Bezier Tessellation */

/* Vector path state container - embed in port context structure */
typedef struct {
    float points_x[IUI_PORT_MAX_PATH_POINTS];
    float points_y[IUI_PORT_MAX_PATH_POINTS];
    int count;
    float pen_x, pen_y;
} iui_path_state_t;

/* Initialize/reset path state */
static inline void iui_path_reset(iui_path_state_t *p)
{
    p->count = 0;
    p->pen_x = 0.0f;
    p->pen_y = 0.0f;
}

/* Move pen to position, starting a new subpath */
static inline void iui_path_move_to(iui_path_state_t *p, float x, float y)
{
    p->pen_x = x;
    p->pen_y = y;
    p->count = 0;

    if (p->count < IUI_PORT_MAX_PATH_POINTS) {
        p->points_x[p->count] = x;
        p->points_y[p->count] = y;
        p->count++;
    }
}

/* Add line segment to current position */
static inline void iui_path_line_to(iui_path_state_t *p, float x, float y)
{
    p->pen_x = x;
    p->pen_y = y;

    if (p->count < IUI_PORT_MAX_PATH_POINTS) {
        p->points_x[p->count] = x;
        p->points_y[p->count] = y;
        p->count++;
    }
}

/* Add cubic Bezier curve using adaptive tessellation
 * Control points: p0 (current pen), p1 (x1,y1), p2 (x2,y2), p3 (x3,y3)
 */
static inline void iui_path_curve_to(iui_path_state_t *p,
                                     float x1,
                                     float y1,
                                     float x2,
                                     float y2,
                                     float x3,
                                     float y3)
{
    float p0x = p->pen_x, p0y = p->pen_y;
    float p1x = x1, p1y = y1;
    float p2x = x2, p2y = y2;
    float p3x = x3, p3y = y3;

    /* Adaptive segments based on curve size (Manhattan distance) */
    int segments = IUI_BEZIER_SEGMENTS(p0x, p0y, p1x, p1y, p2x, p2y, p3x, p3y);
    if (segments < 1)
        segments = 1; /* Guard against divide-by-zero */
    float inv_seg = 1.0f / (float) segments;

    for (int i = 1; i <= segments; i++) {
        float t = (float) i * inv_seg;
        float t2 = t * t;
        float t3 = t2 * t;
        float mt = 1.0f - t;
        float mt2 = mt * mt;
        float mt3 = mt2 * mt;

        /* Cubic Bezier formula: B(t) = (1-t)³P0 + 3(1-t)²tP1 + 3(1-t)t²P2 +
         * t³P3
         */
        float px =
            mt3 * p0x + 3.0f * mt2 * t * p1x + 3.0f * mt * t2 * p2x + t3 * p3x;
        float py =
            mt3 * p0y + 3.0f * mt2 * t * p1y + 3.0f * mt * t2 * p2y + t3 * p3y;

        if (p->count < IUI_PORT_MAX_PATH_POINTS) {
            p->points_x[p->count] = px;
            p->points_y[p->count] = py;
            p->count++;
        }
    }

    p->pen_x = p3x;
    p->pen_y = p3y;
}

/* Scaled versions for HiDPI (SDL2 uses these) */

static inline void iui_path_move_to_scaled(iui_path_state_t *p,
                                           float x,
                                           float y,
                                           float scale)
{
    iui_path_move_to(p, x * scale, y * scale);
}

static inline void iui_path_line_to_scaled(iui_path_state_t *p,
                                           float x,
                                           float y,
                                           float scale)
{
    iui_path_line_to(p, x * scale, y * scale);
}

static inline void iui_path_curve_to_scaled(iui_path_state_t *p,
                                            float x1,
                                            float y1,
                                            float x2,
                                            float y2,
                                            float x3,
                                            float y3,
                                            float scale)
{
    /* Scale control points but use unscaled pen position
     * (pen is already in scaled coordinates from previous move/line/curve)
     */
    float p0x = p->pen_x, p0y = p->pen_y;
    float p1x = x1 * scale, p1y = y1 * scale;
    float p2x = x2 * scale, p2y = y2 * scale;
    float p3x = x3 * scale, p3y = y3 * scale;

    int segments = IUI_BEZIER_SEGMENTS(p0x, p0y, p1x, p1y, p2x, p2y, p3x, p3y);
    if (segments < 1)
        segments = 1; /* Guard against divide-by-zero */
    float inv_seg = 1.0f / (float) segments;

    for (int i = 1; i <= segments; i++) {
        float t = (float) i * inv_seg;
        float t2 = t * t;
        float t3 = t2 * t;
        float mt = 1.0f - t;
        float mt2 = mt * mt;
        float mt3 = mt2 * mt;

        float px =
            mt3 * p0x + 3.0f * mt2 * t * p1x + 3.0f * mt * t2 * p2x + t3 * p3x;
        float py =
            mt3 * p0y + 3.0f * mt2 * t * p1y + 3.0f * mt * t2 * p2y + t3 * p3y;

        if (p->count < IUI_PORT_MAX_PATH_POINTS) {
            p->points_x[p->count] = px;
            p->points_y[p->count] = py;
            p->count++;
        }
    }

    p->pen_x = p3x;
    p->pen_y = p3y;
}

#ifdef __cplusplus
}
#endif

#endif /* IUI_PORT_SW_H */
