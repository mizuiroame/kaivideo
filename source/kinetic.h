// Scrolling that follows the finger, as on a phone: while it drags, the list
// moves with it; let go moving, and it glides on, slowing down. Pure logic:
// the app feeds the drag's moves and asks each frame how far to glide.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define KINETIC_DECAY_PER_MS 0.998   // How much speed a gliding list keeps each millisecond.
#define KINETIC_MIN_SPEED 40.0       // Pixels a second: slower than this, it stops.
#define KINETIC_MIN_FLING 150.0      // Pixels a second: let go slower than this, it doesn't glide.
#define KINETIC_MAX_SPEED 9000.0     // Pixels a second: a flick never goes faster.
#define KINETIC_STILL_NS 80000000ull // A finger held still this long before letting go: no glide.

typedef struct {
    double velocity;         // Pixels a second; positive moves the list's content up (shows what's below).
    double carry;            // What's left of a pixel from the last frames.
    uint64_t last_ns;        // The last move or frame.
    bool dragging, gliding;
} Kinetic;

// A drag moved the list's content by d pixels (positive: up) at now_ns.
void kineticDrag(Kinetic *k, int d, uint64_t now_ns);
// The finger let go: the list glides on if it was moving.
void kineticRelease(Kinetic *k, uint64_t now_ns);
// While it glides: how many whole pixels to move it this frame (0 when it's still).
int kineticStep(Kinetic *k, uint64_t now_ns);
// A press, a button, or the list's end: it stops where it is.
void kineticStop(Kinetic *k);
bool kineticGliding(const Kinetic *k);
