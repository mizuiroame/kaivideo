// "Skip intro" and the like over a playing video, from the parts the server marked.
#include <switch.h>

#include "app.h"
#include "ui_library.h"

// The part to offer now, -1 for none (or when the next episode's card has ZR).
static int offered(const App *app, bool up_next)
{
    if (!app->player || up_next || app->menu.open || app->app_menu != APP_MENU_NONE)
        return -1;
    return jfSegmentAt(app->segments, app->segment_count, playerPosition(app->player));
}

bool appSkipInput(App *app, uint64_t pressed, TouchEvent touch, bool up_next, uint64_t now)
{
    int segment = offered(app, up_next);
    if (segment < 0)
        return false;
    bool tapped = touch.type == TOUCH_TAP && uiLibraryTap(touch.x, touch.y).kind == LIBHIT_SKIP;
    if (!(pressed & HidNpadButton_ZR) && !tapped)
        return false;
    const JfSegment *s = &app->segments[segment];
    appLog("  %s: %.1f s to %.1f s", jfSegmentLabel(s->kind), playerPosition(app->player) / 1e9, s->end_ns / 1e9);
    playerSeekTo(app->player, s->end_ns, now);
    app->seeks++;
    return true;
}

void appSkipDraw(const App *app, bool bar_shown, bool up_next)
{
    int segment = offered(app, up_next);
    if (segment >= 0)
        uiDrawSkipButton(jfSegmentLabel(app->segments[segment].kind), bar_shown);
}
