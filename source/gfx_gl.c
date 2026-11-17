// gfx.h drawn by the GPU with OpenGL ES 3. Every shape is a quad (or a
// triangle) with its parameters in the vertex data, and one shader works out
// each pixel's coverage, as gfx.c does on the CPU: rounded rectangles,
// circles and arcs, lines, triangles, images (rounded, or turned in a disc)
// and glyphs from an atlas. Quads are batched until the image changes.
#ifdef KAI_GPU

#include "gfx_gl.h"

#include <GLES3/gl3.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"

#define FLOATS 16             // Per vertex: position 2, uv 2, color 4, a 4, b 4 (b.w is the kind).
#define MAX_VERTICES (6 * 8192)
#define TEXTURES 96           // Images kept on the GPU (covers, layers, the background).
#define TEXTURE_IDLE_FRAMES 180
#define ATLAS 2048            // Glyph atlas, one channel.
#define ATLAS_SLOTS 8192

enum { KIND_SOLID, KIND_ROUND_RECT, KIND_RING, KIND_LINE, KIND_TRIANGLE, KIND_IMAGE, KIND_GLYPH, KIND_DISC };

static const char *const VERTEX_SHADER =
    "#version 300 es\n"
    "precision highp float;\n"
    "layout(location = 0) in vec2 position;\n"  // Framebuffer pixels (1920 x 1080).
    "layout(location = 1) in vec2 uv;\n"
    "layout(location = 2) in vec4 color;\n"
    "layout(location = 3) in vec4 a;\n"
    "layout(location = 4) in vec4 b;\n"
    "out vec2 v_pos; out vec2 v_uv; out vec4 v_color; flat out vec4 v_a; flat out vec4 v_b;\n"
    "void main() {\n"
    "    v_pos = position; v_uv = uv; v_color = color; v_a = a; v_b = b;\n"
    "    gl_Position = vec4(position.x / 960.0 - 1.0, 1.0 - position.y / 540.0, 0.0, 1.0);\n"
    "}\n";

// Coverage as gfx.c works it out: the distance inside the edge plus half a
// pixel, clamped, in framebuffer pixels.
static const char *const FRAGMENT_SHADER =
    "#version 300 es\n"
    "precision highp float;\n"
    "in vec2 v_pos; in vec2 v_uv; in vec4 v_color; flat in vec4 v_a; flat in vec4 v_b;\n"
    "uniform sampler2D image;\n"
    "uniform sampler2D atlas;\n"
    "uniform vec4 clip_rect;\n"   // x0, y0, x1, y1.
    "uniform float clip_radius;\n"
    "uniform float px;\n"        // Screen pixels per framebuffer pixel: edges a screen pixel wide.
    "out vec4 out_color;\n"
    "float cover(float d) { return clamp(d * px + 0.5, 0.0, 1.0); }\n"
    "float roundBox(vec2 p, vec4 r, float radius) {\n"  // Distance inside a rounded rectangle.
    "    vec2 c = clamp(p, r.xy + radius, r.zw - radius);\n"
    "    vec2 q = p - c;\n"
    "    if (radius <= 0.0 || (q.x == 0.0 || q.y == 0.0))\n"
    "        return min(min(p.x - r.x, r.z - p.x), min(p.y - r.y, r.w - p.y));\n"
    "    return radius - length(q);\n"
    "}\n"
    "float edge(vec2 p, vec2 a, vec2 b) {\n"
    "    vec2 e = b - a; return (e.x * (p.y - a.y) - e.y * (p.x - a.x)) / max(length(e), 1e-4);\n"
    "}\n"
    "void main() {\n"
    "    vec2 p = v_pos;\n"
    "    int kind = int(v_b.w + 0.5);\n"
    "    vec4 c = v_color;\n"
    "    float k = 1.0;\n"
    "    if (kind == 1) {\n"
    "        k = cover(roundBox(p, v_a, v_b.x));\n"
    "    } else if (kind == 2) {\n"  // Ring or arc: center, radius, half thickness; b: start, sweep.
    "        vec2 d = p - v_a.xy;\n"
    "        float ang = atan(d.y, d.x) - v_b.x;\n"
    "        ang = mod(ang, 6.2831853);\n"
    "        k = ang > v_b.y ? 0.0 : cover(v_a.w - abs(length(d) - v_a.z));\n"
    "    } else if (kind == 3) {\n"  // A line with round ends: a = ends, b.x = half thickness.
    "        vec2 e = v_a.zw - v_a.xy;\n"
    "        float t = clamp(dot(p - v_a.xy, e) / max(dot(e, e), 1e-6), 0.0, 1.0);\n"
    "        k = cover(v_b.x - length(p - (v_a.xy + t * e)));\n"
    "    } else if (kind == 4) {\n"  // Triangle a.xy, a.zw, b.xy, either winding.
    "        float e0 = edge(p, v_a.xy, v_a.zw), e1 = edge(p, v_a.zw, v_b.xy), e2 = edge(p, v_b.xy, v_a.xy);\n"
    "        float inside = max(min(e0, min(e1, e2)), min(-e0, min(-e1, -e2)));\n"
    "        k = cover(inside);\n"
    "    } else if (kind == 5) {\n"  // Image, maybe with round corners (b.x).
    "        c *= texture(image, v_uv);\n"
    "        if (v_b.x > 0.0) k = cover(roundBox(p, v_a, v_b.x));\n"
    "    } else if (kind == 6) {\n"
    "        c.a *= texture(atlas, v_uv).r;\n"
    "    } else if (kind == 7) {\n"  // Image turned in a disc: center, radius, hole; b.x angle.
    "        vec2 d = p - v_a.xy;\n"
    "        float r = length(d);\n"
    "        k = cover(v_a.z - r) * cover(r - v_a.w);\n"
    "        float s = sin(v_b.x), co = cos(v_b.x);\n"
    "        vec2 t = vec2(co * d.x + s * d.y, -s * d.x + co * d.y) / (2.0 * v_a.z) + 0.5;\n"
    "        c *= texture(image, t);\n"
    "    }\n"
    "    if (p.x < clip_rect.x || p.y < clip_rect.y || p.x > clip_rect.z || p.y > clip_rect.w) discard;\n"
    "    if (clip_radius > 0.0) k *= cover(roundBox(p, clip_rect, clip_radius));\n"
    "    out_color = vec4(c.rgb, c.a * k);\n"
    "}\n";

typedef struct {
    const void *pixels;
    int w, h;
    uint32_t sig;
    GLuint texture;
    unsigned last_frame;
} Texture;

typedef struct {
    const uint8_t *mask;
    int w, h;
    uint32_t sig;
    float u0, v0, u1, v1;
} Glyph;

static GLuint g_program, g_vao, g_vbo, g_atlas;
static GLint g_u_clip, g_u_clip_radius, g_u_px;
static float g_detail = 1.0f;
static float *g_vertices;
static int g_count;               // Vertices batched.
static const Texture *g_bound;    // The image the batch uses, if any.
static Texture g_textures[TEXTURES];
static unsigned g_frame;
static float g_clip[4] = {0, 0, GFX_FB_WIDTH, GFX_FB_HEIGHT}, g_clip_radius;
static Glyph g_glyphs[ATLAS_SLOTS];
static int g_shelf_x, g_shelf_y, g_shelf_h, g_glyph_count;

// --- The batch ---

static void flush(void)
{
    if (!g_count)
        return;
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)g_count * FLOATS * sizeof(float), g_vertices, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, g_count);
    g_count = 0;
}

static void useImage(const Texture *t)
{
    if (t == g_bound)
        return;
    flush();
    g_bound = t;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, t ? t->texture : 0);
}

static void colorOf(Color c, unsigned alpha, float out[4])
{
    out[0] = (c & 0xFF) / 255.0f;
    out[1] = (c >> 8 & 0xFF) / 255.0f;
    out[2] = (c >> 16 & 0xFF) / 255.0f;
    out[3] = (alpha > 255 ? 255 : alpha) / 255.0f;
}

static void vertex(float x, float y, float u, float v, const float color[4], const float a[4], const float b[4])
{
    float *p = g_vertices + (size_t)g_count++ * FLOATS;
    p[0] = x, p[1] = y, p[2] = u, p[3] = v;
    memcpy(p + 4, color, 4 * sizeof(float));
    memcpy(p + 8, a, 4 * sizeof(float));
    memcpy(p + 12, b, 4 * sizeof(float));
}

// A quad over x0..x1, y0..y1 (framebuffer pixels), uv 0..1 across it, with
// a color for its top and its bottom.
static void quad2(float x0, float y0, float x1, float y1, const float top[4], const float bottom[4], const float a[4],
                  const float b[4])
{
    if (x1 <= x0 || y1 <= y0)
        return;
    if (g_count + 6 > MAX_VERTICES)
        flush();
    vertex(x0, y0, 0, 0, top, a, b);
    vertex(x1, y0, 1, 0, top, a, b);
    vertex(x0, y1, 0, 1, bottom, a, b);
    vertex(x1, y0, 1, 0, top, a, b);
    vertex(x1, y1, 1, 1, bottom, a, b);
    vertex(x0, y1, 0, 1, bottom, a, b);
}

static void quad(float x0, float y0, float x1, float y1, const float color[4], const float a[4], const float b[4])
{
    quad2(x0, y0, x1, y1, color, color, a, b);
}

// A shape that needs no image: unbinds the batch's image only if it must.
static void shape(float x0, float y0, float x1, float y1, Color color, unsigned alpha, int kind, const float a[4],
                  float b0, float b1, float b2)
{
    float c[4], bb[4] = {b0, b1, b2, (float)kind};
    colorOf(color, alpha, c);
    quad(x0 - 1.0f, y0 - 1.0f, x1 + 1.0f, y1 + 1.0f, c, a, bb);
}

// Layout rectangle to framebuffer pixels, as gfx.c's toFb.
static void toFb(int *x, int *y, int *w, int *h)
{
    int x1 = gfxToFb(*x + *w), y1 = gfxToFb(*y + *h);
    *x = gfxToFb(*x), *y = gfxToFb(*y);
    *w = x1 - *x, *h = y1 - *y;
}

static void solidFb(float x, float y, float w, float h, Color color, unsigned alpha)
{
    float c[4], zero[4] = {0}, b[4] = {0, 0, 0, KIND_SOLID};
    colorOf(color, alpha, c);
    quad(x, y, x + w, y + h, c, zero, b);
}

// --- Images ---

// A few pixels' worth of fingerprint, so a buffer reused for another image
// gets a new texture.
static uint32_t signature(const uint32_t *pixels, int w, int h)
{
    uint32_t s = 2166136261u ^ (uint32_t)(w * 31 + h);
    size_t n = (size_t)w * h;
    for (size_t i = 0; i < 16; i++)
        s = (s ^ pixels[(n - 1) * i / 15]) * 16777619u;
    return s;
}

void gfxImageChanged(const uint32_t *pixels)
{
    for (int i = 0; i < TEXTURES; i++) {
        Texture *t = &g_textures[i];
        if (t->texture && t->pixels == pixels) {
            flush();
            if (g_bound == t)
                g_bound = NULL;
            glDeleteTextures(1, &t->texture);
            *t = (Texture){0};
        }
    }
}

static const Texture *textureFor(const uint32_t *pixels, int w, int h)
{
    if (!pixels || w <= 0 || h <= 0)
        return NULL;
    uint32_t sig = signature(pixels, w, h);
    Texture *slot = NULL;
    for (int i = 0; i < TEXTURES; i++) {
        Texture *t = &g_textures[i];
        if (t->texture && t->pixels == pixels && t->w == w && t->h == h && t->sig == sig) {
            t->last_frame = g_frame;
            return t;
        }
        if (!slot || (!t->texture && slot->texture) || (t->texture && slot->texture && t->last_frame < slot->last_frame))
            slot = t;  // An empty slot, or the one used least recently.
    }
    flush();
    if (slot->texture) {
        if (g_bound == slot)
            g_bound = NULL;
        glDeleteTextures(1, &slot->texture);
    }
    *slot = (Texture){pixels, w, h, sig, 0, g_frame};
    glGenTextures(1, &slot->texture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, slot->texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    g_bound = NULL;
    useImage(slot);
    return slot;
}

// An image over the rectangle; covers are opaque, layers carry their own alpha.
static void image(float x0, float y0, float x1, float y1, const uint32_t *pixels, int w, int h, float radius)
{
    const Texture *t = textureFor(pixels, w, h);
    if (!t)
        return;
    useImage(t);
    float white[4] = {1, 1, 1, 1}, a[4] = {x0, y0, x1, y1}, b[4] = {radius, 0, 0, KIND_IMAGE};
    quad(x0, y0, x1, y1, white, a, b);
}

// --- Glyphs ---

// Empties the atlas, pixels too: the GPU blends a glyph's edge with the
// texel beside it, which must be blank, not part of a glyph from before (as
// after going from handheld to docked, when every glyph comes at a new size).
static void clearAtlas(void)
{
    uint8_t *blank = calloc((size_t)ATLAS * ATLAS, 1);
    if (!blank)
        return;
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, g_atlas);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, ATLAS, ATLAS, GL_RED, GL_UNSIGNED_BYTE, blank);
    glActiveTexture(GL_TEXTURE0);
    free(blank);
}

static void resetAtlas(void)
{
    flush();
    memset(g_glyphs, 0, sizeof(g_glyphs));
    g_shelf_x = g_shelf_y = g_shelf_h = g_glyph_count = 0;
    clearAtlas();
}

void gfxForgetMasks(void)
{
    resetAtlas();
}

static const Glyph *glyphFor(const uint8_t *mask, int w, int h, int stride)
{
    if (w + 1 > ATLAS || h + 1 > ATLAS)
        return NULL;  // Never fits: skip it rather than empty the atlas for nothing.
    uint32_t sig = (uint32_t)mask[0] | (uint32_t)mask[(size_t)(h - 1) * stride + w - 1] << 8 |
                   (uint32_t)mask[(size_t)(h / 2) * stride + w / 2] << 16;
    size_t slot = ((uintptr_t)mask >> 3 ^ (uintptr_t)w * 131 ^ (uintptr_t)h * 7919) % ATLAS_SLOTS;
    for (int probe = 0; probe < ATLAS_SLOTS; probe++, slot = (slot + 1) % ATLAS_SLOTS) {
        Glyph *g = &g_glyphs[slot];
        if (g->mask == mask && g->w == w && g->h == h && g->sig == sig)
            return g;
        if (g->mask)
            continue;
        // Not there yet: find room on the shelves, starting over when full.
        if (g_shelf_x + w + 1 > ATLAS) {
            g_shelf_x = 0;
            g_shelf_y += g_shelf_h + 1;
            g_shelf_h = 0;
        }
        if (g_shelf_y + h + 1 > ATLAS || probe > 64 || g_glyph_count > ATLAS_SLOTS / 2) {
            resetAtlas();
            return glyphFor(mask, w, h, stride);
        }
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, g_atlas);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, stride);
        glTexSubImage2D(GL_TEXTURE_2D, 0, g_shelf_x, g_shelf_y, w, h, GL_RED, GL_UNSIGNED_BYTE, mask);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glActiveTexture(GL_TEXTURE0);
        g_glyph_count++;
        *g = (Glyph){mask, w, h, sig, (float)g_shelf_x / ATLAS, (float)g_shelf_y / ATLAS,
                     (float)(g_shelf_x + w) / ATLAS, (float)(g_shelf_y + h) / ATLAS};
        g_shelf_x += w + 1;
        if (h > g_shelf_h)
            g_shelf_h = h;
        return g;
    }
    return NULL;
}

float gfxDetail(void)
{
    return g_detail;
}

void gfxBlendMask(int x, int y, int w, int h, const uint8_t *mask, int stride, Color color)
{
    gfxBlendMaskScaled((float)x, (float)y, (float)w, (float)h, mask, w, h, stride, color);
}

void gfxBlendMaskScaled(float x, float y, float w, float h, const uint8_t *mask, int mask_w, int mask_h, int stride,
                        Color color)
{
    if (!mask || mask_w <= 0 || mask_h <= 0)
        return;
    const Glyph *g = glyphFor(mask, mask_w, mask_h, stride);
    if (!g)
        return;
    if (g_count + 6 > MAX_VERTICES)
        flush();
    float c[4], a[4] = {0}, b[4] = {0, 0, 0, KIND_GLYPH};
    colorOf(color, 255, c);
    float x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    vertex(x0, y0, g->u0, g->v0, c, a, b);
    vertex(x1, y0, g->u1, g->v0, c, a, b);
    vertex(x0, y1, g->u0, g->v1, c, a, b);
    vertex(x1, y0, g->u1, g->v0, c, a, b);
    vertex(x1, y1, g->u1, g->v1, c, a, b);
    vertex(x0, y1, g->u0, g->v1, c, a, b);
}

// --- gfx.h ---

void gfxSetTarget(uint32_t *pixels, uint32_t stride);  // The host check defines it (gfx_gl_host.c).

Color gfxMix(Color a, Color b, unsigned t);  // In gfx.c, shared.

bool gfxCanReadBack(void)
{
    return false;
}

void gfxClear(Color color)
{
    solidFb(0, 0, GFX_FB_WIDTH, GFX_FB_HEIGHT, color, 255);
}

void gfxFillRect(int x, int y, int w, int h, Color color)
{
    toFb(&x, &y, &w, &h);
    solidFb((float)x, (float)y, (float)w, (float)h, color, 255);
}

void gfxBlendRect(int x, int y, int w, int h, Color color, unsigned alpha)
{
    toFb(&x, &y, &w, &h);
    solidFb((float)x, (float)y, (float)w, (float)h, color, alpha);
}

void gfxFillRoundRect(int x, int y, int w, int h, int radius, Color color)
{
    toFb(&x, &y, &w, &h);
    float r = (float)gfxToFb(radius);
    if (r * 2 > w) r = w / 2.0f;
    if (r * 2 > h) r = h / 2.0f;
    float a[4] = {(float)x, (float)y, (float)(x + w), (float)(y + h)};
    shape(a[0], a[1], a[2], a[3], color, 255, KIND_ROUND_RECT, a, r, 0, 0);
}

void gfxFillCircle(int cx, int cy, int radius, Color color)
{
    float fr = radius * GFX_SCALE, fcx = cx * GFX_SCALE, fcy = cy * GFX_SCALE;
    // gfx.c's circle: full coverage inside the radius, not a ring's dip at the center.
    float a[4] = {fcx, fcy, 0.0f, fr};
    shape(fcx - fr, fcy - fr, fcx + fr, fcy + fr, color, 255, KIND_RING, a, 0.0f, 6.2831853f, 0);
}

void gfxGradientV(int x, int y, int w, int h, Color top, Color bottom)
{
    toFb(&x, &y, &w, &h);
    float t[4], bo[4], zero[4] = {0}, b[4] = {0, 0, 0, KIND_SOLID};
    colorOf(top, 255, t);
    colorOf(bottom, 255, bo);
    quad2((float)x, (float)y, (float)(x + w), (float)(y + h), t, bo, zero, b);
}

static void triangle(float x0, float y0, float x1, float y1, float x2, float y2, Color color)
{
    float a[4] = {x0, y0, x1, y1};
    shape(fminf(x0, fminf(x1, x2)), fminf(y0, fminf(y1, y2)), fmaxf(x0, fmaxf(x1, x2)), fmaxf(y0, fmaxf(y1, y2)),
          color, 255, KIND_TRIANGLE, a, x2, y2, 0);
}

void gfxFillTriangleRight(int x, int y, int w, int h, Color color)
{
    toFb(&x, &y, &w, &h);
    triangle((float)x, (float)y, (float)(x + w), y + h / 2.0f, (float)x, (float)(y + h), color);
}

void gfxFillTriangleLeft(int x, int y, int w, int h, Color color)
{
    toFb(&x, &y, &w, &h);
    triangle((float)(x + w), (float)y, (float)x, y + h / 2.0f, (float)(x + w), (float)(y + h), color);
}

void gfxLine(float x0, float y0, float x1, float y1, float thickness, Color color)
{
    x0 *= GFX_SCALE, y0 *= GFX_SCALE, x1 *= GFX_SCALE, y1 *= GFX_SCALE;
    float r = thickness * GFX_SCALE / 2.0f, a[4] = {x0, y0, x1, y1};
    shape(fminf(x0, x1) - r, fminf(y0, y1) - r, fmaxf(x0, x1) + r, fmaxf(y0, y1) + r, color, 255, KIND_LINE, a, r,
          0, 0);
}

void gfxArc(float cx, float cy, float radius, float thickness, float start, float end, Color color)
{
    cx *= GFX_SCALE, cy *= GFX_SCALE, radius *= GFX_SCALE, thickness *= GFX_SCALE;
    float r = radius + thickness / 2.0f, sweep = end - start;
    sweep = sweep < 0.0f ? 0.0f : sweep > 6.2831853f ? 6.2831853f : sweep;
    float a[4] = {cx, cy, radius, thickness / 2.0f};
    shape(cx - r, cy - r, cx + r, cy + r, color, 255, KIND_RING, a, start, sweep, 0);
}

void gfxTriangle(float x0, float y0, float x1, float y1, float x2, float y2, Color color)
{
    triangle(x0 * GFX_SCALE, y0 * GFX_SCALE, x1 * GFX_SCALE, y1 * GFX_SCALE, x2 * GFX_SCALE, y2 * GFX_SCALE, color);
}

void gfxBlit(int x, int y, int size, int image_size, const uint32_t *pixels)
{
    int w = size, h = size;
    toFb(&x, &y, &w, &h);
    if (image_size == gfxToFb(size))
        w = h = image_size;
    image((float)x, (float)y, (float)(x + w), (float)(y + h), pixels, image_size, image_size, 0.0f);
}

void gfxBlitRounded(int x, int y, int size, int image_size, const uint32_t *pixels, int radius)
{
    if (!pixels) {
        gfxBlit(x, y, size, image_size, pixels);
        return;
    }
    // The GPU scales and rounds in one go, so any image size works here.
    float fx = (float)gfxToFb(x), fy = (float)gfxToFb(y), r = (float)gfxToFb(radius);
    float side = (float)gfxToFb(size);
    if (r * 2 > side)
        r = side / 2.0f;
    image(fx, fy, fx + side, fy + side, pixels, image_size, image_size, r);
}

void gfxBlitDisc(float cx, float cy, float radius, float hole, int image_size, const uint32_t *pixels, float angle)
{
    const Texture *t = textureFor(pixels, image_size, image_size);
    if (!t)
        return;
    useImage(t);
    cx *= GFX_SCALE, cy *= GFX_SCALE, radius *= GFX_SCALE, hole *= GFX_SCALE;
    float white[4] = {1, 1, 1, 1}, a[4] = {cx, cy, radius, hole}, b[4] = {angle, 0, 0, KIND_DISC};
    quad(cx - radius - 1.0f, cy - radius - 1.0f, cx + radius + 1.0f, cy + radius + 1.0f, white, a, b);
}

void gfxDrawImageFb(int x, int y, int w, int h, const uint32_t *pixels)
{
    image((float)x, (float)y, (float)(x + w), (float)(y + h), pixels, w, h, 0.0f);
}

void gfxDrawImageScaledFb(float x, float y, float w, float h, int image_w, int image_h, const uint32_t *pixels)
{
    image(x, y, x + w, y + h, pixels, image_w, image_h, 0.0f);
}

// --- Video ---

static GLuint compile(GLenum type, const char *source);

// NV12 to RGB on the GPU: Y from one texture, U and V from a half-size one.
static const char *const VIDEO_VERTEX_SHADER =
    "#version 300 es\n"
    "layout(location = 0) in vec2 position;\n"
    "layout(location = 1) in vec2 uv_in;\n"
    "out vec2 uv;\n"
    "void main() {\n"
    "    uv = uv_in;\n"
    "    gl_Position = vec4(position.x / 960.0 - 1.0, 1.0 - position.y / 540.0, 0.0, 1.0);\n"
    "}\n";

static const char *const VIDEO_FRAGMENT_SHADER =
    "#version 300 es\n"
    "precision mediump float;\n"
    "uniform sampler2D plane_y;\n"
    "uniform sampler2D plane_uv;\n"
    "uniform mat3 to_rgb;\n"     // Columns: what Y, U and V add to R, G and B.
    "uniform vec3 offset;\n"     // Y's black level and U and V's middle.
    "uniform vec3 scale;\n"      // From the coded range to 0-1.
    "in vec2 uv;\n"
    "out vec4 color;\n"
    "void main() {\n"
    "    vec3 yuv = (vec3(texture(plane_y, uv).r, texture(plane_uv, uv).rg) - offset) * scale;\n"
    "    color = vec4(clamp(to_rgb * yuv, 0.0, 1.0), 1.0);\n"
    "}\n";

static GLuint g_video_program, g_video_vao, g_video_vbo, g_video_tex[2];
static GLint g_video_u_matrix, g_video_u_offset, g_video_u_scale;
static int g_video_w, g_video_h;
static unsigned g_video_id;
static bool g_video_has_frame;

static bool videoSetup(void)
{
    GLuint vs = compile(GL_VERTEX_SHADER, VIDEO_VERTEX_SHADER), fs = compile(GL_FRAGMENT_SHADER, VIDEO_FRAGMENT_SHADER);
    if (!vs || !fs)
        return false;
    g_video_program = glCreateProgram();
    glAttachShader(g_video_program, vs);
    glAttachShader(g_video_program, fs);
    glLinkProgram(g_video_program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint linked = 0;
    glGetProgramiv(g_video_program, GL_LINK_STATUS, &linked);
    if (!linked)
        return false;
    glUseProgram(g_video_program);
    glUniform1i(glGetUniformLocation(g_video_program, "plane_y"), 2);
    glUniform1i(glGetUniformLocation(g_video_program, "plane_uv"), 3);
    g_video_u_matrix = glGetUniformLocation(g_video_program, "to_rgb");
    g_video_u_offset = glGetUniformLocation(g_video_program, "offset");
    g_video_u_scale = glGetUniformLocation(g_video_program, "scale");
    glGenVertexArrays(1, &g_video_vao);
    glBindVertexArray(g_video_vao);
    glGenBuffers(1, &g_video_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_video_vbo);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glGenTextures(2, g_video_tex);
    for (int i = 0; i < 2; i++) {
        glActiveTexture(GL_TEXTURE2 + i);
        glBindTexture(GL_TEXTURE_2D, g_video_tex[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glActiveTexture(GL_TEXTURE0);
    return true;
}

static void videoTeardown(void)
{
    if (g_video_tex[0])
        glDeleteTextures(2, g_video_tex);
    if (g_video_vbo)
        glDeleteBuffers(1, &g_video_vbo);
    if (g_video_vao)
        glDeleteVertexArrays(1, &g_video_vao);
    if (g_video_program)
        glDeleteProgram(g_video_program);
    g_video_tex[0] = g_video_tex[1] = g_video_vbo = g_video_vao = g_video_program = 0;
    g_video_w = g_video_h = 0;
    g_video_has_frame = false;
}

// Sends the frame's planes to the textures, (re)sized to it when it changes size.
static void videoUpload(const GfxVideoFrame *f)
{
    bool resize = f->width != g_video_w || f->height != g_video_h;
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, g_video_tex[0]);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, f->y_pitch);
    if (resize)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, f->width, f->height, 0, GL_RED, GL_UNSIGNED_BYTE, f->y);
    else
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, f->width, f->height, GL_RED, GL_UNSIGNED_BYTE, f->y);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, g_video_tex[1]);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, f->uv_pitch / 2);  // In RG pixels.
    if (resize)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, f->width / 2, f->height / 2, 0, GL_RG, GL_UNSIGNED_BYTE, f->uv);
    else
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, f->width / 2, f->height / 2, GL_RG, GL_UNSIGNED_BYTE, f->uv);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glActiveTexture(GL_TEXTURE0);
    g_video_w = f->width;
    g_video_h = f->height;
    g_video_id = f->id;
    g_video_has_frame = true;
}

void gfxDrawVideo(const GfxVideoFrame *frame, float x, float y, float w, float h)
{
    if (!g_video_program || !frame || !frame->y || !frame->uv || frame->width < 2 || frame->height < 2)
        return;
    flush();  // What's batched goes first, under the video.
    if (!g_video_has_frame || frame->id != g_video_id || frame->width != g_video_w || frame->height != g_video_h)
        videoUpload(frame);
    glUseProgram(g_video_program);
    // BT.709 (HD) or BT.601 (SD), as columns: Y, U, V.
    static const float BT709[9] = {1, 1, 1, 0, -0.1873f, 1.8556f, 1.5748f, -0.4681f, 0};
    static const float BT601[9] = {1, 1, 1, 0, -0.3441f, 1.772f, 1.402f, -0.7141f, 0};
    glUniformMatrix3fv(g_video_u_matrix, 1, GL_FALSE, frame->bt709 ? BT709 : BT601);
    if (frame->full_range) {
        glUniform3f(g_video_u_offset, 0.0f, 128.0f / 255.0f, 128.0f / 255.0f);
        glUniform3f(g_video_u_scale, 1.0f, 1.0f, 1.0f);
    } else {
        glUniform3f(g_video_u_offset, 16.0f / 255.0f, 128.0f / 255.0f, 128.0f / 255.0f);
        glUniform3f(g_video_u_scale, 255.0f / 219.0f, 255.0f / 224.0f, 255.0f / 224.0f);
    }
    float v[24] = {x, y, 0, 0, x + w, y, 1, 0, x, y + h, 0, 1, x + w, y, 1, 0, x + w, y + h, 1, 1, x, y + h, 0, 1};
    glBindVertexArray(g_video_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_video_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    // Back to the shapes' program and buffers.
    glUseProgram(g_program);
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
}

void gfxCopyFrame(const uint32_t *pixels)
{
    image(0, 0, GFX_FB_WIDTH, GFX_FB_HEIGHT, pixels, GFX_FB_WIDTH, GFX_FB_HEIGHT, 0.0f);
}

void gfxSaveFrame(uint32_t *pixels)
{
    (void)pixels;  // The GPU's frame isn't read back: gfxCanReadBack() is false.
}

void gfxSaveRectFb(int x, int y, int w, int h, uint32_t *out)
{
    (void)x, (void)y;
    memset(out, 0, (size_t)w * h * sizeof(uint32_t));
}

static void setClip(float x0, float y0, float x1, float y1, float radius)
{
    flush();
    g_clip[0] = x0, g_clip[1] = y0, g_clip[2] = x1, g_clip[3] = y1, g_clip_radius = radius;
    glUniform4fv(g_u_clip, 1, g_clip);
    glUniform1f(g_u_clip_radius, g_clip_radius);
}

void gfxSetClip(int x, int y, int w, int h)
{
    toFb(&x, &y, &w, &h);
    setClip((float)x, (float)y, (float)(x + w), (float)(y + h), 0.0f);
}

void gfxSetClipRound(int x, int y, int w, int h, int radius)
{
    toFb(&x, &y, &w, &h);
    setClip((float)x, (float)y, (float)(x + w), (float)(y + h), (float)gfxToFb(radius));
}

void gfxResetClip(void)
{
    setClip(0, 0, GFX_FB_WIDTH, GFX_FB_HEIGHT, 0.0f);
}

// --- Setup and frames ---

static GLuint compile(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool gfxGlSetup(void)
{
    g_vertices = malloc((size_t)MAX_VERTICES * FLOATS * sizeof(float));
    GLuint vs = compile(GL_VERTEX_SHADER, VERTEX_SHADER), fs = compile(GL_FRAGMENT_SHADER, FRAGMENT_SHADER);
    if (!g_vertices || !vs || !fs)
        return false;
    g_program = glCreateProgram();
    glAttachShader(g_program, vs);
    glAttachShader(g_program, fs);
    glLinkProgram(g_program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint linked = 0;
    glGetProgramiv(g_program, GL_LINK_STATUS, &linked);
    if (!linked)
        return false;
    glUseProgram(g_program);
    glUniform1i(glGetUniformLocation(g_program, "image"), 0);
    glUniform1i(glGetUniformLocation(g_program, "atlas"), 1);
    g_u_clip = glGetUniformLocation(g_program, "clip_rect");
    g_u_clip_radius = glGetUniformLocation(g_program, "clip_radius");
    g_u_px = glGetUniformLocation(g_program, "px");

    glGenVertexArrays(1, &g_vao);
    glBindVertexArray(g_vao);
    glGenBuffers(1, &g_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    GLsizei stride = FLOATS * sizeof(float);
    static const int SIZES[5] = {2, 2, 4, 4, 4};
    for (int i = 0, offset = 0; i < 5; offset += SIZES[i++]) {
        glVertexAttribPointer(i, SIZES[i], GL_FLOAT, GL_FALSE, stride, (void *)(offset * sizeof(float)));
        glEnableVertexAttribArray(i);
    }

    glGenTextures(1, &g_atlas);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, g_atlas);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, ATLAS, ATLAS, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
    // The same filter both ways: glyphs are drawn about 1:1, where the GPU's
    // choice between the two would depend on where a glyph sits in the atlas.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glActiveTexture(GL_TEXTURE0);
    clearAtlas();  // GL leaves a new texture's contents undefined.

    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    if (!videoSetup())
        return false;
    glUseProgram(g_program);
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    return true;
}

void gfxGlTeardown(void)
{
    videoTeardown();
    for (int i = 0; i < TEXTURES; i++) {
        if (g_textures[i].texture)
            glDeleteTextures(1, &g_textures[i].texture);
    }
    memset(g_textures, 0, sizeof(g_textures));
    if (g_atlas)
        glDeleteTextures(1, &g_atlas);
    if (g_vbo)
        glDeleteBuffers(1, &g_vbo);
    if (g_vao)
        glDeleteVertexArrays(1, &g_vao);
    if (g_program)
        glDeleteProgram(g_program);
    g_atlas = g_vbo = g_vao = g_program = 0;
    free(g_vertices);
    g_vertices = NULL;
}

void gfxGlBeginFrame(int width, int height)
{
    glViewport(0, 0, width, height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);  // Never last frame's leftovers, whatever the view draws.
    glUseProgram(g_program);
    float detail = height > GFX_FB_HEIGHT ? (float)height / GFX_FB_HEIGHT : 1.0f;
    if (detail != g_detail) {
        g_detail = detail;
        gfxForgetMasks();  // Glyphs come at a new size now.
    }
    glUniform1f(g_u_px, (float)height / GFX_FB_HEIGHT);
    glBindVertexArray(g_vao);
    g_bound = NULL;
    g_count = 0;
    gfxResetClip();
}

void gfxGlEndFrame(void)
{
    flush();
    g_frame++;
    // Images not drawn for a while go: songs change, covers come and go.
    for (int i = 0; i < TEXTURES; i++) {
        Texture *t = &g_textures[i];
        if (t->texture && g_frame - t->last_frame > TEXTURE_IDLE_FRAMES) {
            glDeleteTextures(1, &t->texture);
            *t = (Texture){0};
        }
    }
    g_bound = NULL;
}

#endif
