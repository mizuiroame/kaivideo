// The server's trickplay pictures over a playing video: above the bar while
// a finger drags it, and over the screen while a jump's picture is awaited.
#include "app.h"
#include "gfx.h"
#include "tpfetch.h"
#include "ui_library.h"

// The picture of at_ns, or NULL; told to the GPU when it's another.
static const uint32_t *pictureAt(int64_t at_ns, int *w, int *h)
{
    bool changed;
    const uint32_t *pixels = tpfetchPicture(at_ns, w, h, &changed);
    if (pixels && changed)
        gfxImageChanged(pixels);
    return pixels;
}

void appTrickplayBehind(const App *app, bool waiting)
{
    int w, h;
    const uint32_t *pixels = app->player && waiting && !app->scrubbing ? pictureAt(playerPosition(app->player), &w, &h)
                                                                       : NULL;
    if (pixels)
        uiDrawPreviewBehind(pixels, w, h);
}

void appTrickplayAboveBar(const App *app, bool bar)
{
    if (!app->player || !bar || !app->scrubbing)
        return;
    int w, h;
    const uint32_t *pixels = pictureAt((int64_t)(app->scrub_fraction * playerDuration(app->player)), &w, &h);
    if (pixels)
        uiDrawSeekPreview(pixels, w, h, app->scrub_fraction);
}
