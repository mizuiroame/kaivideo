#include "gfx.h"

#include <math.h>
#include <string.h>

#ifndef KAI_GPU
static uint32_t *g_pixels;
static uint32_t g_stride;  // In pixels.
static int g_clip_x0, g_clip_y0, g_clip_x1 = GFX_FB_WIDTH, g_clip_y1 = GFX_FB_HEIGHT;  // Framebuffer pixels.

void gfxSetTarget(uint32_t *pixels, uint32_t stride)
{
    g_pixels = pixels;
    g_stride = stride;
}

#endif

Color gfxMix(Color a, Color b, unsigned t)
{
    if (t > 255)
        t = 255;
    unsigned inv = 255 - t;
    unsigned r = ((a & 0xFF) * inv + (b & 0xFF) * t) / 255;
    unsigned g = (((a >> 8) & 0xFF) * inv + ((b >> 8) & 0xFF) * t) / 255;
    unsigned bl = (((a >> 16) & 0xFF) * inv + ((b >> 16) & 0xFF) * t) / 255;
    return GFX_RGB(r, g, bl);
}

#ifndef KAI_GPU

// Clips a framebuffer rectangle to the screen. False if nothing is left.
static bool clip(int *x, int *y, int *w, int *h)
{
    int x0 = *x < g_clip_x0 ? g_clip_x0 : *x, y0 = *y < g_clip_y0 ? g_clip_y0 : *y;
    int x1 = *x + *w > g_clip_x1 ? g_clip_x1 : *x + *w;
    int y1 = *y + *h > g_clip_y1 ? g_clip_y1 : *y + *h;
    if (!g_pixels || x1 <= x0 || y1 <= y0)
        return false;
    *x = x0, *y = y0, *w = x1 - x0, *h = y1 - y0;
    return true;
}

// color over *p with opacity alpha (0-255), in 8.8 fixed point (no divisions:
// text, edges and layers blend this way, many pixels a frame).
static inline void blendPixel(uint32_t *p, Color color, unsigned alpha)
{
    if (alpha >= 255) {
        *p = color;
    } else if (alpha > 0) {
        uint32_t a = alpha + (alpha >> 7), inv = 256 - a, q = *p;
        *p = 0xFF000000u | (((q & 0xFF00FF) * inv + (color & 0xFF00FF) * a) >> 8 & 0xFF00FF) |
             (((q & 0x00FF00) * inv + (color & 0x00FF00) * a) >> 8 & 0x00FF00);
    }
}

void gfxClear(Color color)
{
    gfxFillRect(0, 0, GFX_WIDTH, GFX_HEIGHT, color);
}

// Layout rectangle to framebuffer pixels. Both edges are converted, so
// rectangles that touch in the layout still touch.
static void toFb(int *x, int *y, int *w, int *h)
{
    int x1 = gfxToFb(*x + *w), y1 = gfxToFb(*y + *h);
    *x = gfxToFb(*x), *y = gfxToFb(*y);
    *w = x1 - *x, *h = y1 - *y;
}

static void fillFb(int x, int y, int w, int h, Color color)
{
    if (!clip(&x, &y, &w, &h))
        return;
    for (int py = y; py < y + h; py++) {
        uint32_t *row = g_pixels + (size_t)py * g_stride + x;
        for (int i = 0; i < w; i++)
            row[i] = color;
    }
}

void gfxFillRect(int x, int y, int w, int h, Color color)
{
    toFb(&x, &y, &w, &h);
    fillFb(x, y, w, h, color);
}

void gfxBlendRect(int x, int y, int w, int h, Color color, unsigned alpha)
{
    toFb(&x, &y, &w, &h);
    if (!clip(&x, &y, &w, &h) || alpha == 0)
        return;
    // Red and blue blended together, then green, in 8.8 fixed point: no
    // divisions, as this covers the whole screen under menus.
    uint32_t a = alpha >= 255 ? 256 : alpha, inv = 256 - a;
    uint32_t rb = (color & 0xFF00FF) * a, g = (color & 0x00FF00) * a;
    for (int py = y; py < y + h; py++) {
        uint32_t *row = g_pixels + (size_t)py * g_stride + x;
        for (int i = 0; i < w; i++) {
            uint32_t p = row[i];
            row[i] = 0xFF000000u | (((p & 0xFF00FF) * inv + rb) >> 8 & 0xFF00FF) |
                     (((p & 0x00FF00) * inv + g) >> 8 & 0x00FF00);
        }
    }
}

// Coverage (0-255) of pixel (px, py) by a circle, with a 1-pixel soft edge.
static unsigned circleCoverage(float px, float py, float cx, float cy, float r)
{
    float d = sqrtf((px - cx) * (px - cx) + (py - cy) * (py - cy));
    float c = r - d + 0.5f;
    if (c <= 0.0f)
        return 0;
    return c >= 1.0f ? 255 : (unsigned)(c * 255.0f);
}

void gfxFillRoundRect(int x, int y, int w, int h, int radius, Color color)
{
    toFb(&x, &y, &w, &h);
    radius = gfxToFb(radius);
    if (radius * 2 > w) radius = w / 2;
    if (radius * 2 > h) radius = h / 2;
    fillFb(x, y + radius, w, h - 2 * radius, color);
    fillFb(x + radius, y, w - 2 * radius, radius, color);
    fillFb(x + radius, y + h - radius, w - 2 * radius, radius, color);
    for (int dy = 0; dy < radius; dy++) {
        for (int dx = 0; dx < radius; dx++) {
            unsigned a = circleCoverage(dx + 0.5f, dy + 0.5f, (float)radius, (float)radius, (float)radius);
            if (a == 0)
                continue;
            int xs[2] = {x + dx, x + w - 1 - dx}, ys[2] = {y + dy, y + h - 1 - dy};
            for (int i = 0; i < 4; i++) {
                int px = xs[i & 1], py = ys[i >> 1];
                if (g_pixels && px >= g_clip_x0 && px < g_clip_x1 && py >= g_clip_y0 && py < g_clip_y1)
                    blendPixel(g_pixels + (size_t)py * g_stride + px, color, a);
            }
        }
    }
}

void gfxFillCircle(int cx, int cy, int radius, Color color)
{
    float fcx = cx * GFX_SCALE, fcy = cy * GFX_SCALE, fr = radius * GFX_SCALE;
    int x = (int)(fcx - fr) - 1, y = (int)(fcy - fr) - 1, w = (int)(fr * 2.0f) + 3, h = w;
    if (!clip(&x, &y, &w, &h))
        return;
    for (int py = y; py < y + h; py++) {
        uint32_t *row = g_pixels + (size_t)py * g_stride;
        for (int px = x; px < x + w; px++)
            blendPixel(&row[px], color, circleCoverage(px + 0.5f, py + 0.5f, fcx, fcy, fr));
    }
}

void gfxGradientV(int x, int y, int w, int h, Color top, Color bottom)
{
    toFb(&x, &y, &w, &h);
    for (int i = 0; i < h; i++)
        fillFb(x, y + i, w, 1, gfxMix(top, bottom, h > 1 ? (unsigned)(i * 255 / (h - 1)) : 0));
}

#define TRIANGLE_SAMPLES 4  // Per axis: 16 samples per pixel for smooth edges.

// An isosceles triangle in a w x h box pointing right (or left), with
// anti-aliased edges from supersampling.
static void fillTriangle(int x, int y, int w, int h, Color color, bool left)
{
    int bx = x, by = y, bw = w, bh = h;
    if (w <= 0 || h <= 0 || !clip(&bx, &by, &bw, &bh))
        return;
    float half = (float)h / 2.0f;
    for (int py = by; py < by + bh; py++) {
        uint32_t *row = g_pixels + (size_t)py * g_stride;
        for (int px = bx; px < bx + bw; px++) {
            unsigned inside = 0;
            for (int sy = 0; sy < TRIANGLE_SAMPLES; sy++) {
                float fy = (float)(py - y) + (sy + 0.5f) / TRIANGLE_SAMPLES;
                float reach = (float)w * (1.0f - fabsf(fy - half) / half);  // Width at this height.
                for (int sx = 0; sx < TRIANGLE_SAMPLES; sx++) {
                    float fx = (float)(px - x) + (sx + 0.5f) / TRIANGLE_SAMPLES;
                    inside += (left ? (float)w - fx : fx) < reach;
                }
            }
            blendPixel(&row[px], color, inside * 255 / (TRIANGLE_SAMPLES * TRIANGLE_SAMPLES));
        }
    }
}

void gfxFillTriangleRight(int x, int y, int w, int h, Color color)
{
    toFb(&x, &y, &w, &h);
    fillTriangle(x, y, w, h, color, false);
}

void gfxFillTriangleLeft(int x, int y, int w, int h, Color color)
{
    toFb(&x, &y, &w, &h);
    fillTriangle(x, y, w, h, color, true);
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

// Calls shade on every pixel of the box around a shape, blending color by
// the coverage it returns (0-1).
typedef float (*Coverage)(float px, float py, const float *shape);

static void fillShape(float x0, float y0, float x1, float y1, const float *shape, Coverage coverage, Color color)
{
    int x = (int)floorf(x0) - 1, y = (int)floorf(y0) - 1;
    int w = (int)ceilf(x1) + 2 - x, h = (int)ceilf(y1) + 2 - y;
    if (!clip(&x, &y, &w, &h))
        return;
    for (int py = y; py < y + h; py++) {
        uint32_t *row = g_pixels + (size_t)py * g_stride;
        for (int px = x; px < x + w; px++)
            blendPixel(&row[px], color, (unsigned)(coverage(px + 0.5f, py + 0.5f, shape) * 255.0f));
    }
}

// shape: x0, y0, x1, y1, half thickness.
static float lineCoverage(float px, float py, const float *s)
{
    float dx = s[2] - s[0], dy = s[3] - s[1], len2 = dx * dx + dy * dy;
    float t = len2 > 0.0f ? clampf(((px - s[0]) * dx + (py - s[1]) * dy) / len2, 0.0f, 1.0f) : 0.0f;
    float ex = px - (s[0] + t * dx), ey = py - (s[1] + t * dy);
    return clampf(s[4] - sqrtf(ex * ex + ey * ey) + 0.5f, 0.0f, 1.0f);
}

void gfxLine(float x0, float y0, float x1, float y1, float thickness, Color color)
{
    x0 *= GFX_SCALE, y0 *= GFX_SCALE, x1 *= GFX_SCALE, y1 *= GFX_SCALE;
    float r = thickness * GFX_SCALE / 2.0f, s[5] = {x0, y0, x1, y1, r};
    fillShape(fminf(x0, x1) - r, fminf(y0, y1) - r, fmaxf(x0, x1) + r, fmaxf(y0, y1) + r, s, lineCoverage, color);
}

// shape: cx, cy, radius, half thickness, start, sweep (0 to 2 pi).
static float arcCoverage(float px, float py, const float *s)
{
    float dx = px - s[0], dy = py - s[1];
    float angle = atan2f(dy, dx) - s[4];
    angle = fmodf(angle, 2.0f * (float)M_PI);
    if (angle < 0.0f)
        angle += 2.0f * (float)M_PI;
    if (angle > s[5])
        return 0.0f;
    float d = fabsf(sqrtf(dx * dx + dy * dy) - s[2]);
    return clampf(s[3] - d + 0.5f, 0.0f, 1.0f);
}

void gfxArc(float cx, float cy, float radius, float thickness, float start, float end, Color color)
{
    cx *= GFX_SCALE, cy *= GFX_SCALE, radius *= GFX_SCALE, thickness *= GFX_SCALE;
    float r = radius + thickness / 2.0f;
    float s[6] = {cx, cy, radius, thickness / 2.0f, start, clampf(end - start, 0.0f, 2.0f * (float)M_PI)};
    fillShape(cx - r, cy - r, cx + r, cy + r, s, arcCoverage, color);
}

// shape: the three corners. 4x4 samples per pixel.
static float triangleCoverage(float px, float py, const float *s)
{
    int inside = 0;
    for (int i = 0; i < 16; i++) {
        float x = px - 0.5f + ((i & 3) + 0.5f) / 4.0f, y = py - 0.5f + ((i >> 2) + 0.5f) / 4.0f;
        float e0 = (s[2] - s[0]) * (y - s[1]) - (s[3] - s[1]) * (x - s[0]);
        float e1 = (s[4] - s[2]) * (y - s[3]) - (s[5] - s[3]) * (x - s[2]);
        float e2 = (s[0] - s[4]) * (y - s[5]) - (s[1] - s[5]) * (x - s[4]);
        inside += (e0 >= 0 && e1 >= 0 && e2 >= 0) || (e0 <= 0 && e1 <= 0 && e2 <= 0);
    }
    return (float)inside / 16.0f;
}

void gfxTriangle(float x0, float y0, float x1, float y1, float x2, float y2, Color color)
{
    x0 *= GFX_SCALE, y0 *= GFX_SCALE, x1 *= GFX_SCALE, y1 *= GFX_SCALE, x2 *= GFX_SCALE, y2 *= GFX_SCALE;
    float s[6] = {x0, y0, x1, y1, x2, y2};
    fillShape(fminf(x0, fminf(x1, x2)), fminf(y0, fminf(y1, y2)), fmaxf(x0, fmaxf(x1, x2)),
              fmaxf(y0, fmaxf(y1, y2)), s, triangleCoverage, color);
}

void gfxBlit(int x, int y, int size, int image_size, const uint32_t *pixels)
{
    int w = size, h = size;
    toFb(&x, &y, &w, &h);
    if (image_size == gfxToFb(size))
        w = h = image_size;  // Made for this size: copy it 1:1.
    int cx = x, cy = y, cw = w, ch = h;
    if (!pixels || image_size <= 0 || !clip(&cx, &cy, &cw, &ch))
        return;
    for (int row = 0; row < ch; row++) {
        uint32_t *dst = g_pixels + (size_t)(cy + row) * g_stride + cx;
        if (image_size == w && image_size == h) {
            memcpy(dst, pixels + (size_t)(cy - y + row) * image_size + (cx - x), (size_t)cw * sizeof(uint32_t));
            continue;
        }
        // Another size (nearest pixel): only if a cover was decoded for another scale.
        const uint32_t *src = pixels + (size_t)((cy - y + row) * image_size / h) * image_size;
        for (int i = 0; i < cw; i++)
            dst[i] = src[(cx - x + i) * image_size / w];
    }
}

void gfxBlitRounded(int x, int y, int size, int image_size, const uint32_t *pixels, int radius)
{
    if (!pixels || image_size != gfxToFb(size)) {
        gfxBlit(x, y, size, image_size, pixels);
        return;
    }
    int fx = gfxToFb(x), fy = gfxToFb(y), r = gfxToFb(radius);
    r = r * 2 > image_size ? image_size / 2 : r;
    float corner_lo = (float)r, corner_hi = (float)(image_size - r);
    for (int row = 0; row < image_size; row++) {
        int py = fy + row;
        if (!g_pixels || py < 0 || py >= GFX_FB_HEIGHT)
            continue;
        const uint32_t *src = pixels + (size_t)row * image_size;
        uint32_t *dst = g_pixels + (size_t)py * g_stride;
        float cy = row + 0.5f;
        for (int col = 0; col < image_size; col++) {
            int px = fx + col;
            if (px < 0 || px >= GFX_FB_WIDTH)
                continue;
            float cx = col + 0.5f;
            // Inside the corners' squares, coverage comes from the corner circle.
            float ccx = cx < corner_lo ? corner_lo : cx > corner_hi ? corner_hi : cx;
            float ccy = cy < corner_lo ? corner_lo : cy > corner_hi ? corner_hi : cy;
            unsigned a = (ccx == cx || ccy == cy) ? 255 : circleCoverage(cx, cy, ccx, ccy, (float)r);
            blendPixel(&dst[px], src[col], a);
        }
    }
}

void gfxBlendMask(int x, int y, int w, int h, const uint8_t *mask, int stride, Color color)
{
    int cx = x, cy = y, cw = w, ch = h;
    if (!mask || !clip(&cx, &cy, &cw, &ch))
        return;
    for (int row = 0; row < ch; row++) {
        const uint8_t *m = mask + (size_t)(cy - y + row) * stride + (cx - x);
        uint32_t *dst = g_pixels + (size_t)(cy + row) * g_stride + cx;
        for (int i = 0; i < cw; i++)
            blendPixel(&dst[i], color, m[i]);
    }
}

void gfxDrawVideo(const GfxVideoFrame *frame, float x, float y, float w, float h)
{
    (void)frame, (void)x, (void)y, (void)w, (void)h;  // Video is only drawn by the GPU.
}

void gfxCopyFrame(const uint32_t *pixels)
{
    if (!g_pixels)
        return;
    for (int y = 0; y < GFX_FB_HEIGHT; y++)
        memcpy(g_pixels + (size_t)y * g_stride, pixels + (size_t)y * GFX_FB_WIDTH, GFX_FB_WIDTH * sizeof(uint32_t));
}

void gfxSetClip(int x, int y, int w, int h)
{
    toFb(&x, &y, &w, &h);
    g_clip_x0 = x < 0 ? 0 : x;
    g_clip_y0 = y < 0 ? 0 : y;
    g_clip_x1 = x + w > GFX_FB_WIDTH ? GFX_FB_WIDTH : x + w;
    g_clip_y1 = y + h > GFX_FB_HEIGHT ? GFX_FB_HEIGHT : y + h;
}

void gfxResetClip(void)
{
    g_clip_x0 = g_clip_y0 = 0;
    g_clip_x1 = GFX_FB_WIDTH;
    g_clip_y1 = GFX_FB_HEIGHT;
}

void gfxSetClipRound(int x, int y, int w, int h, int radius)
{
    (void)radius;
    gfxSetClip(x, y, w, h);
}

bool gfxCanReadBack(void)
{
    return true;
}

void gfxForgetMasks(void)
{
}

void gfxImageChanged(const uint32_t *pixels)
{
    (void)pixels;  // The CPU reads images straight from their buffers.
}

float gfxDetail(void)
{
    return 1.0f;
}

void gfxBlendMaskScaled(float x, float y, float w, float h, const uint8_t *mask, int mask_w, int mask_h, int stride,
                        Color color)
{
    (void)w, (void)h;
    gfxBlendMask((int)lroundf(x), (int)lroundf(y), mask_w, mask_h, mask, stride, color);
}

void gfxSaveRectFb(int x, int y, int w, int h, uint32_t *out)
{
    for (int py = 0; py < h; py++) {
        for (int px = 0; px < w; px++) {
            int sx = x + px, sy = y + py;
            bool inside = g_pixels && sx >= 0 && sy >= 0 && sx < GFX_FB_WIDTH && sy < GFX_FB_HEIGHT;
            out[(size_t)py * w + px] = inside ? g_pixels[(size_t)sy * g_stride + sx] | 0xFF000000u : 0;
        }
    }
}

void gfxSaveFrame(uint32_t *pixels)
{
    if (!g_pixels)
        return;
    for (int y = 0; y < GFX_FB_HEIGHT; y++)
        memcpy(pixels + (size_t)y * GFX_FB_WIDTH, g_pixels + (size_t)y * g_stride, GFX_FB_WIDTH * sizeof(uint32_t));
}

static unsigned g_image_alpha = 255;

void gfxSetImageAlpha(unsigned alpha)
{
    g_image_alpha = alpha > 255 ? 255 : alpha;
}

void gfxDrawImageFb(int x, int y, int w, int h, const uint32_t *pixels)
{
    int cx = x, cy = y, cw = w, ch = h;
    if (!clip(&cx, &cy, &cw, &ch))
        return;
    for (int py = cy; py < cy + ch; py++) {
        const uint32_t *src = pixels + (size_t)(py - y) * w + (cx - x);
        uint32_t *row = g_pixels + (size_t)py * g_stride + cx;
        for (int i = 0; i < cw; i++)
            blendPixel(&row[i], src[i] | 0xFF000000u, (src[i] >> 24) * g_image_alpha / 255);
    }
}

void gfxDrawImageScaledFb(float x, float y, float w, float h, int image_w, int image_h, const uint32_t *pixels)
{
    int ix = (int)lroundf(x), iy = (int)lroundf(y), iw = (int)lroundf(w), ih = (int)lroundf(h);
    if (iw == image_w && ih == image_h) {
        gfxDrawImageFb(ix, iy, iw, ih, pixels);
        return;
    }
    int cx = ix, cy = iy, cw = iw, ch = ih;
    if (iw <= 0 || ih <= 0 || !clip(&cx, &cy, &cw, &ch))
        return;
    for (int py = cy; py < cy + ch; py++) {
        const uint32_t *src = pixels + (size_t)((long)(py - iy) * image_h / ih) * image_w;
        uint32_t *row = g_pixels + (size_t)py * g_stride;
        for (int px = cx; px < cx + cw; px++) {
            uint32_t p = src[(long)(px - ix) * image_w / iw];
            blendPixel(&row[px], p | 0xFF000000u, (p >> 24) * g_image_alpha / 255);
        }
    }
}

// The pixel at (u, v) in 16.16 fixed point, blending its four neighbours.
static uint32_t sampleBilinear(const uint32_t *pixels, int size, int32_t u, int32_t v)
{
    int x = u >> 16, y = v >> 16;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > size - 2) x = size - 2;
    if (y > size - 2) y = size - 2;
    uint32_t fx = (uint32_t)(u >> 8) & 0xFF, fy = (uint32_t)(v >> 8) & 0xFF;
    const uint32_t *p = pixels + (size_t)y * size + x;
    uint32_t a = p[0], b = p[1], c = p[size], d = p[size + 1];
    uint32_t rb = ((a & 0xFF00FF) * (256 - fx) + (b & 0xFF00FF) * fx) >> 8 & 0xFF00FF;
    uint32_t g = ((a & 0xFF00) * (256 - fx) + (b & 0xFF00) * fx) >> 8 & 0xFF00;
    uint32_t rb2 = ((c & 0xFF00FF) * (256 - fx) + (d & 0xFF00FF) * fx) >> 8 & 0xFF00FF;
    uint32_t g2 = ((c & 0xFF00) * (256 - fx) + (d & 0xFF00) * fx) >> 8 & 0xFF00;
    rb = (rb * (256 - fy) + rb2 * fy) >> 8 & 0xFF00FF;
    g = (g * (256 - fy) + g2 * fy) >> 8 & 0xFF00;
    return 0xFF000000u | rb | g;
}

// The part of a row inside a ring: from x0 to x1 (exclusive), clipped.
static bool ringSpan(float cx, float dy, float outer, int *x0, int *x1)
{
    float reach = outer * outer - dy * dy;
    if (reach < 0.0f)
        return false;
    float half = sqrtf(reach);
    *x0 = (int)floorf(cx - half) - 1;
    *x1 = (int)ceilf(cx + half) + 1;
    if (*x0 < 0) *x0 = 0;
    if (*x1 > GFX_FB_WIDTH) *x1 = GFX_FB_WIDTH;
    return *x0 < *x1;
}

void gfxBlitDisc(float cx, float cy, float radius, float hole, int image_size, const uint32_t *pixels, float angle)
{
    if (!g_pixels || !pixels || image_size < 2)
        return;
    cx *= GFX_SCALE, cy *= GFX_SCALE, radius *= GFX_SCALE, hole *= GFX_SCALE;
    // The image's center at the disc's center, its edge at the disc's edge, turned by angle.
    // Positions step in 16.16 fixed point; only pixels near an edge work out their coverage.
    float scale = image_size / (2.0f * radius), c = cosf(angle) * scale, s = sinf(angle) * scale;
    float half = image_size / 2.0f - 0.5f;
    float solid_out = (radius - 0.5f) * (radius - 0.5f), solid_in = (hole + 0.5f) * (hole + 0.5f);
    float edge_out = (radius + 0.5f) * (radius + 0.5f), edge_in = hole > 0.5f ? (hole - 0.5f) * (hole - 0.5f) : 0.0f;
    int32_t du = (int32_t)(c * 65536.0f), dv = (int32_t)(-s * 65536.0f);
    int y0 = (int)floorf(cy - radius) - 1, y1 = (int)ceilf(cy + radius) + 1;
    if (y0 < 0) y0 = 0;
    if (y1 > GFX_FB_HEIGHT) y1 = GFX_FB_HEIGHT;
    for (int py = y0; py < y1; py++) {
        float dy = py + 0.5f - cy;
        int x0, x1;
        if (!ringSpan(cx, dy, radius + 0.5f, &x0, &x1))
            continue;
        float dx = x0 + 0.5f - cx;
        int32_t u = (int32_t)((half + c * dx + s * dy) * 65536.0f);
        int32_t v = (int32_t)((half - s * dx + c * dy) * 65536.0f);
        uint32_t *row = g_pixels + (size_t)py * g_stride;
        for (int px = x0; px < x1; px++, dx += 1.0f, u += du, v += dv) {
            float d2 = dx * dx + dy * dy;
            if (d2 >= edge_out || d2 <= edge_in)
                continue;
            uint32_t color = sampleBilinear(pixels, image_size, u, v);
            if (d2 <= solid_out && d2 >= solid_in) {
                row[px] = color;
                continue;
            }
            float d = sqrtf(d2);
            float cover = clampf(radius - d + 0.5f, 0.0f, 1.0f) * clampf(d - hole + 0.5f, 0.0f, 1.0f);
            blendPixel(&row[px], color, (unsigned)(cover * 255.0f));
        }
    }
}

#endif
