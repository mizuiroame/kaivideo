// Turns touch screen samples into taps and drags. Pure logic: the app feeds
// one sample per frame.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define TOUCH_TAP_SLOP 18           // Pixels a tap may move.
#define TOUCH_TAP_MAX_NS 700000000ull

typedef enum {
    TOUCH_NONE,
    TOUCH_TAP,   // Released where it started: x, y.
    TOUCH_DRAG,  // Moving: dy since the last frame; start_x/start_y where it began.
    TOUCH_DRAG_END,  // Released after a drag, at x, y.
} TouchEventType;

typedef struct {
    TouchEventType type;
    int x, y;
    int start_x, start_y;
    int dy;
} TouchEvent;

typedef struct {
    bool down;
    bool dragging;
    int start_x, start_y;
    int last_x, last_y;
    uint64_t start_ns;
} TouchTracker;

void touchReset(TouchTracker *t);
TouchEvent touchUpdate(TouchTracker *t, bool touching, int x, int y, uint64_t now_ns);
