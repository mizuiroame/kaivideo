// The GPU drawing backend (OpenGL ES 3), shared by the console (EGL on the
// default window, gfx_gl_switch.c) and the host check (an offscreen
// framebuffer, tools/gltest). Draws the 1920x1080 framebuffer space of gfx.h
// scaled to whatever size the surface has.
#pragma once

#include <stdbool.h>

// With a current GL context: compiles the shader and makes the buffers.
bool gfxGlSetup(void);
void gfxGlTeardown(void);

// Starts a frame on the bound framebuffer, width x height pixels.
void gfxGlBeginFrame(int width, int height);
// Sends what's batched. The caller then presents (or reads back).
void gfxGlEndFrame(void);
