// Checks gfx_gl.c's HDR shader on a PC (Mesa's OpenGL ES, offscreen) against
// hdr.c, and times it. Built and run by tools/hdrtest/run.sh:
//   gl_hdr FRAME.yuv WIDTH HEIGHT pq|hlg PEAK OUT_PREFIX
// FRAME.yuv: one raw yuv420p10le frame (ffmpeg -frames:v 1 -f rawvideo). It goes
// to 8-bit NV12 with swframe.c as the app does, is drawn 1:1 (or fitted, if
// bigger) into a 1920x1080 framebuffer with the HDR shader and with the SDR
// one (what the app did before), and both are written as PPMs.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "gfx.h"
#include "gfx_gl.h"
#include "hdr.h"
#include "swframe.h"

#define FB_W 1920
#define FB_H 1080
#define TIMED_DRAWS 20

static double nowMs(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static bool setup(void)
{
    PFNEGLGETPLATFORMDISPLAYEXTPROC get = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay display = get ? get(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL) : EGL_NO_DISPLAY;
    if (!eglInitialize(display, NULL, NULL))
        return false;
    eglBindAPI(EGL_OPENGL_ES_API);
    EGLint config_attributes[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                                  EGL_NONE};
    EGLConfig config;
    EGLint count;
    eglChooseConfig(display, config_attributes, &config, 1, &count);
    EGLint context_attributes[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attributes);
    if (context == EGL_NO_CONTEXT || !eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context))
        return false;
    GLuint color, fbo;
    glGenRenderbuffers(1, &color);
    glBindRenderbuffer(GL_RENDERBUFFER, color);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, FB_W, FB_H);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color);
    printf("GL: %s\n", glGetString(GL_RENDERER));
    return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE && gfxGlSetup();
}

// Draws the frame and reads it back (rows top down, RGBA). The time of one draw, in ms, averaged.
static double draw(const GfxVideoFrame *f, float w, float h, uint8_t *rgba, int times)
{
    double t0 = 0;
    for (int i = 0; i <= times; i++) {
        if (i == 1) {
            glFinish();
            t0 = nowMs();
        }
        GfxVideoFrame again = *f;
        again.id = f->id + (unsigned)i;  // Uploaded again each time, as each new video frame is.
        gfxGlBeginFrame(FB_W, FB_H);
        gfxDrawVideo(&again, 0, 0, w, h);
        gfxGlEndFrame();
    }
    glFinish();
    double ms = times ? (nowMs() - t0) / times : 0;
    uint8_t *rows = malloc((size_t)FB_W * FB_H * 4);
    glReadPixels(0, 0, FB_W, FB_H, GL_RGBA, GL_UNSIGNED_BYTE, rows);
    for (int y = 0; y < FB_H; y++)
        memcpy(rgba + (size_t)y * FB_W * 4, rows + (size_t)(FB_H - 1 - y) * FB_W * 4, FB_W * 4);
    free(rows);
    return ms;
}

static void savePpm(const char *path, const uint8_t *rgba, int w, int h)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    fprintf(f, "P6 %d %d 255\n", w, h);
    for (int i = 0; i < w * h; i++)
        fwrite(rgba + i * 4, 1, 3, f);
    fclose(f);
}

int main(int argc, char **argv)
{
    if (argc < 7) {
        fprintf(stderr, "gl_hdr FRAME.yuv WIDTH HEIGHT pq|hlg PEAK OUT_PREFIX\n");
        return 2;
    }
    int w = atoi(argv[2]), h = atoi(argv[3]);
    HdrKind kind = !strcmp(argv[4], "hlg") ? HDR_HLG : HDR_PQ;
    float peak = (float)atof(argv[5]);
    size_t plane = (size_t)w * h * 2, size = plane * 3 / 2;
    uint8_t *raw = malloc(size), *y8 = malloc((size_t)w * h), *uv8 = malloc((size_t)w * h / 2);
    FILE *in = fopen(argv[1], "rb");
    if (!in || fread(raw, 1, size, in) != size) {
        fprintf(stderr, "can't read %s\n", argv[1]);
        return 1;
    }
    fclose(in);
    const uint8_t *const planes[3] = {raw, raw + plane, raw + plane + plane / 4};
    const ptrdiff_t lines[3] = {w * 2, w, w};
    swframeToNv12(planes, lines, w, h, 10, y8, uv8, w);

    if (!setup()) {
        fprintf(stderr, "no OpenGL ES\n");
        return 1;
    }
    GfxVideoFrame frame = {.width = w, .height = h, .y = y8, .uv = uv8, .y_pitch = w, .uv_pitch = w, .id = 1,
                           .bt709 = true, .hdr = kind, .hdr_peak = peak};
    float dw = w > FB_W ? FB_W : (float)w, dh = h > FB_H ? FB_H : (float)h;
    uint8_t *hdr = malloc((size_t)FB_W * FB_H * 4), *sdr = malloc((size_t)FB_W * FB_H * 4);
    double hdr_ms = draw(&frame, dw, dh, hdr, TIMED_DRAWS);
    GfxVideoFrame plain = frame;
    plain.hdr = HDR_NONE;
    plain.id = 1000;
    double sdr_ms = draw(&plain, dw, dh, sdr, TIMED_DRAWS);
    char path[512];
    snprintf(path, sizeof(path), "%s_hdr.ppm", argv[6]);
    savePpm(path, hdr, FB_W, FB_H);
    snprintf(path, sizeof(path), "%s_sdr.ppm", argv[6]);
    savePpm(path, sdr, FB_W, FB_H);
    printf("%dx%d to %.0fx%.0f, upload + draw: HDR shader %.2f ms, SDR shader %.2f ms (Mesa, on the CPU)\n", w, h,
           dw, dh, hdr_ms, sdr_ms);

    // The same frame by hdr.c, on the CPU: how close the shader is, and what the CPU would pay.
    HdrParams p = hdrParams(kind, peak);
    double t0 = nowMs();
    uint8_t *ref = malloc((size_t)w * h * 3);
    for (int yy = 0; yy < h; yy++)
        for (int x = 0; x < w; x++) {
            const uint8_t *c = uv8 + (size_t)(yy / 2) * w + (x & ~1);
            hdrPixelYuv8(&p, y8[(size_t)yy * w + x], c[0], c[1], ref + ((size_t)yy * w + x) * 3);
        }
    double cpu_ms = nowMs() - t0;
    printf("hdr.c on the CPU, one thread, plain C: %.1f ms a frame\n", cpu_ms);
    if (w <= FB_W && h <= FB_H) {
        enum { BANDS = 8 };
        long long sum = 0, worst = 0, count = 0, over4 = 0, band_over[BANDS] = {0};
        for (int yy = 0; yy < h; yy++)
            for (int x = 0; x < w; x++)
                for (int c = 0; c < 3; c++) {
                    int d = abs((int)hdr[((size_t)yy * FB_W + x) * 4 + c] - (int)ref[((size_t)yy * w + x) * 3 + c]);
                    sum += d;
                    worst = d > worst ? d : worst;
                    over4 += d > 4;
                    band_over[yy * BANDS / h] += d > 4;
                    count++;
                }
        printf("shader against hdr.c: mean difference %.3f, largest %lld (of 255), %.3f%% over 4; by band:",
               (double)sum / count, worst, 100.0 * over4 / count);
        for (int b = 0; b < BANDS; b++)
            printf(" %lld", band_over[b]);
        printf("\n");
    }
    return 0;
}
