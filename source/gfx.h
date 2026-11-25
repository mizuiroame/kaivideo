// Drawing: shapes, gradients and images. Text is drawn by font.h on top of
// these. Two backends: gfx.c draws with the CPU (the host preview and tests,
// and the console without KAI_GPU); gfx_gl.c with the GPU.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// The UI is laid out in a 1280x720 space. The framebuffer is 1.5 times
// that, 1920x1080 (the Switch 2 screen, and a clean 2x for a 4K TV), and
// every shape, glyph and cover is drawn at that full resolution.
#define GFX_WIDTH 1280
#define GFX_HEIGHT 720
#define GFX_FB_WIDTH 1920
#define GFX_FB_HEIGHT 1080
#define GFX_SCALE 1.5f

// A layout coordinate or length in framebuffer pixels (rounded down).
static inline int gfxToFb(int v)
{
    return (v * 3) >> 1;
}

// RGBA8 as libnx stores it: 0xAABBGGRR.
typedef uint32_t Color;
#define GFX_RGB(r, g, b) ((Color)(0xFF000000u | ((b) << 16) | ((g) << 8) | (r)))

// The framebuffer (gfx_switch.c).
bool gfxInit(void);
void gfxExit(void);

// Starts and presents a frame. Drawing is only valid between the two.
void gfxBegin(void);
void gfxEnd(void);

// Sets the GFX_FB_WIDTH x GFX_FB_HEIGHT pixels that drawing goes to; NULL
// turns drawing off. stride is in pixels. gfxBegin calls this; the host preview uses it directly.
void gfxSetTarget(uint32_t *pixels, uint32_t stride);

// Linear blend from a (t = 0) to b (t = 255).
Color gfxMix(Color a, Color b, unsigned t);

void gfxClear(Color color);
void gfxFillRect(int x, int y, int w, int h, Color color);
// Draws color over what is there with opacity alpha (0-255).
void gfxBlendRect(int x, int y, int w, int h, Color color, unsigned alpha);
void gfxFillRoundRect(int x, int y, int w, int h, int radius, Color color);
void gfxFillCircle(int cx, int cy, int radius, Color color);
void gfxGradientV(int x, int y, int w, int h, Color top, Color bottom);

// Triangles in a w x h box, pointing right or left.
void gfxFillTriangleRight(int x, int y, int w, int h, Color color);
void gfxFillTriangleLeft(int x, int y, int w, int h, Color color);

// Anti-aliased strokes for icons. A line with round ends; an arc of a ring
// from angle start to end (radians, clockwise on screen from 3 o'clock);
// and any triangle.
void gfxLine(float x0, float y0, float x1, float y1, float thickness, Color color);
void gfxArc(float cx, float cy, float radius, float thickness, float start, float end, Color color);
void gfxTriangle(float x0, float y0, float x1, float y1, float x2, float y2, Color color);

// Draws an image_size x image_size image into a size x size box. An image
// of gfxToFb(size) pixels is copied 1:1; any other size is scaled.
void gfxBlit(int x, int y, int size, int image_size, const uint32_t *pixels);
// The same with rounded corners (radius; size / 2 makes a circle). Images
// not made for this size fall back to gfxBlit's square.
void gfxBlitRounded(int x, int y, int size, int image_size, const uint32_t *pixels, int radius);
// Draws an image (turned by angle, radians clockwise) into the ring between
// hole and radius around (cx, cy), its center at the center and its edges at
// the edge: a record's label, a CD's print. Layout units.
void gfxBlitDisc(float cx, float cy, float radius, float hole, int image_size, const uint32_t *pixels, float angle);
// Draws a w x h image with its own alpha (0xAABBGGRR) at framebuffer pixels x, y.
void gfxDrawImageFb(int x, int y, int w, int h, const uint32_t *pixels);
// The same for an image_w x image_h image drawn over w x h framebuffer pixels:
// one made gfxDetail() times finer stays sharp on a bigger screen. The CPU
// copies an image of w x h 1:1, and samples others.
void gfxDrawImageScaledFb(float x, float y, float w, float h, int image_w, int image_h, const uint32_t *pixels);
// Images drawn from now on (gfxDrawImageFb, gfxDrawImageScaledFb) are this opaque (0-255) besides
// their own alpha: a picture fading in. 255 again when done.
void gfxSetImageAlpha(unsigned alpha);
// A decoded video frame: NV12 (a Y plane, then interleaved U and V at half
// size), pitch-linear. id changes when the pixels do, so an unchanged frame
// isn't sent to the GPU again.
typedef struct {
    int width, height;
    const uint8_t *y, *uv;
    int y_pitch, uv_pitch;
    unsigned id;
    bool bt709;       // HD colors (BT.709); else SD (BT.601).
    bool full_range;  // 0-255 rather than 16-235.
    int hdr;          // hdr.h's HdrKind (BT.2020, PQ or HLG) to bring down to SDR; 0: none.
    float hdr_peak;   // Its brightest, in nits.
} GfxVideoFrame;

// Draws a video frame into a w x h box at x, y (framebuffer pixels),
// converting its colors on the GPU. The CPU backend draws nothing.
void gfxDrawVideo(const GfxVideoFrame *frame, float x, float y, float w, float h);

// Copies a whole GFX_FB_WIDTH x GFX_FB_HEIGHT image to the screen (a background),
// or the screen into one.
void gfxCopyFrame(const uint32_t *pixels);
void gfxSaveFrame(uint32_t *pixels);
// Copies w x h framebuffer pixels at x, y into out (off-screen ones as 0).
void gfxSaveRectFb(int x, int y, int w, int h, uint32_t *out);
// Limits drawing to a layout rectangle (not gfxBlitDisc or text), until gfxResetClip.
void gfxSetClip(int x, int y, int w, int h);
// The same with round corners of radius (the GPU; the CPU clips to the rectangle).
void gfxSetClipRound(int x, int y, int w, int h, int radius);
void gfxResetClip(void);
// True if what's drawn can be read back (gfxSaveFrame, gfxSaveRectFb): the
// CPU, where keeping finished parts of the screen saves time. Not the GPU.
bool gfxCanReadBack(void);
// The glyph bitmaps given to gfxBlendMask were freed (font.c's cache).
void gfxForgetMasks(void);
// An image's buffer got new pixels (or will be freed): the GPU drops its copy.
void gfxImageChanged(const uint32_t *pixels);
// Screen pixels per framebuffer pixel: 1, or more when the GPU draws at a
// higher resolution (1.33 at 1440p, 2 at 2160p). Text is rasterized this
// much finer and drawn with gfxBlendMaskScaled, so it stays sharp.
float gfxDetail(void);
// A mask_w x mask_h mask drawn over w x h framebuffer pixels at x, y.
void gfxBlendMaskScaled(float x, float y, float w, float h, const uint8_t *mask, int mask_w, int mask_h, int stride,
                        Color color);
// Draws color through an 8-bit coverage mask (used for text). Unlike the
// rest, x, y, w and h are framebuffer pixels.
void gfxBlendMask(int x, int y, int w, int h, const uint8_t *mask, int stride, Color color);
