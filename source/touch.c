#include "touch.h"

#include <stdlib.h>
#include <string.h>

void touchReset(TouchTracker *t)
{
    memset(t, 0, sizeof(*t));
}

TouchEvent touchUpdate(TouchTracker *t, bool touching, int x, int y, uint64_t now_ns)
{
    TouchEvent e = {.type = TOUCH_NONE};
    if (touching && !t->down) {
        *t = (TouchTracker){.down = true, .start_x = x, .start_y = y, .last_x = x, .last_y = y, .start_ns = now_ns};
        return e;
    }
    if (touching) {
        if (!t->dragging && (abs(x - t->start_x) > TOUCH_TAP_SLOP || abs(y - t->start_y) > TOUCH_TAP_SLOP))
            t->dragging = true;
        if (t->dragging) {
            e = (TouchEvent){TOUCH_DRAG, x, y, t->start_x, t->start_y, y - t->last_y};
        }
        t->last_x = x;
        t->last_y = y;
        return e;
    }
    if (t->down && t->dragging)
        e = (TouchEvent){TOUCH_DRAG_END, t->last_x, t->last_y, t->start_x, t->start_y, 0};
    else if (t->down && now_ns - t->start_ns <= TOUCH_TAP_MAX_NS)
        e = (TouchEvent){TOUCH_TAP, t->start_x, t->start_y, t->start_x, t->start_y, 0};
    touchReset(t);
    return e;
}
