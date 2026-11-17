// The console side of the GPU backend: OpenGL ES 3 through EGL on libnx's
// default window (OunceVK), at the console's resolution: 1920x1080 handheld,
// the TV's docked. gfx_gl.c draws the 1920x1080 framebuffer space scaled to it.
#if defined(KAI_GPU) && defined(__SWITCH__)

#include <EGL/egl.h>
#include <switch.h>

#include "gfx.h"
#include "gfx_gl.h"

static EGLDisplay g_display = EGL_NO_DISPLAY;
static EGLContext g_context = EGL_NO_CONTEXT;
static EGLSurface g_surface = EGL_NO_SURFACE;

bool gfxInit(void)
{
    nwindowSetModeDimensions(nwindowGetDefault(), 0, 0, 0, 0);  // Follow the console's resolution.
    g_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(g_display, NULL, NULL))
        return false;
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint config_attributes[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE,
    };
    EGLConfig config;
    EGLint count = 0;
    if (!eglChooseConfig(g_display, config_attributes, &config, 1, &count) || count == 0)
        goto fail;
    g_surface = eglCreateWindowSurface(g_display, config, nwindowGetDefault(), NULL);
    const EGLint context_attributes[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE};
    g_context = eglCreateContext(g_display, config, EGL_NO_CONTEXT, context_attributes);
    if (g_surface == EGL_NO_SURFACE || g_context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(g_display, g_surface, g_surface, g_context))
        goto fail;
    eglSwapInterval(g_display, 1);
    if (gfxGlSetup())
        return true;
fail:
    gfxExit();
    return false;
}

void gfxExit(void)
{
    if (g_display == EGL_NO_DISPLAY)
        return;
    gfxGlTeardown();
    eglMakeCurrent(g_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (g_context != EGL_NO_CONTEXT)
        eglDestroyContext(g_display, g_context);
    if (g_surface != EGL_NO_SURFACE)
        eglDestroySurface(g_display, g_surface);
    eglTerminate(g_display);
    g_display = EGL_NO_DISPLAY;
    g_context = EGL_NO_CONTEXT;
    g_surface = EGL_NO_SURFACE;
}

void gfxBegin(void)
{
    EGLint width = GFX_FB_WIDTH, height = GFX_FB_HEIGHT;
    eglQuerySurface(g_display, g_surface, EGL_WIDTH, &width);
    eglQuerySurface(g_display, g_surface, EGL_HEIGHT, &height);
    gfxGlBeginFrame(width, height);
}

void gfxEnd(void)
{
    gfxGlEndFrame();
    if (!eglSwapBuffers(g_display, g_surface)) {
        // The window went away (going to or from the dock, sleep): make the surface again.
        eglMakeCurrent(g_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(g_display, g_surface);
        EGLConfig config;
        EGLint count = 0, id = 0;
        eglQueryContext(g_display, g_context, EGL_CONFIG_ID, &id);
        const EGLint attributes[] = {EGL_CONFIG_ID, id, EGL_NONE};
        eglChooseConfig(g_display, attributes, &config, 1, &count);
        g_surface = count ? eglCreateWindowSurface(g_display, config, nwindowGetDefault(), NULL) : EGL_NO_SURFACE;
        eglMakeCurrent(g_display, g_surface, g_surface, g_context);
    }
}

void gfxSetTarget(uint32_t *pixels, uint32_t stride)
{
    (void)pixels, (void)stride;  // The GPU draws to the window; nothing to point at.
}

#endif
