// The libnx side of gfx: a CPU framebuffer on the default window.
#ifndef KAI_GPU

#include <switch.h>

#include "gfx.h"

#define FRAMEBUFFER_COUNT 2

static Framebuffer g_fb;
static bool g_drawing;

bool gfxInit(void)
{
    if (R_FAILED(framebufferCreate(&g_fb, nwindowGetDefault(), GFX_FB_WIDTH, GFX_FB_HEIGHT,
                                   PIXEL_FORMAT_RGBA_8888, FRAMEBUFFER_COUNT)))
        return false;
    if (R_FAILED(framebufferMakeLinear(&g_fb))) {
        framebufferClose(&g_fb);
        return false;
    }
    return true;
}

void gfxExit(void)
{
    framebufferClose(&g_fb);
}

void gfxBegin(void)
{
    u32 stride_bytes = 0;
    u32 *pixels = framebufferBegin(&g_fb, &stride_bytes);
    gfxSetTarget(pixels, stride_bytes / sizeof(u32));
    g_drawing = pixels != NULL;
}

void gfxEnd(void)
{
    if (g_drawing)
        framebufferEnd(&g_fb);
    gfxSetTarget(NULL, 0);
    g_drawing = false;
}

#endif
