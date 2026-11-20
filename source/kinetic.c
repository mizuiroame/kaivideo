#include "kinetic.h"

#include <math.h>
#include <string.h>

#define NS_PER_S 1e9
#define NS_PER_MS 1e6
#define SPEED_WEIGHT 0.6  // How much a new move counts against the speed measured before.
#define MAX_FRAME_NS 100000000ull  // A frame longer than this counts as this long.

void kineticDrag(Kinetic *k, int d, uint64_t now_ns)
{
    if (!k->dragging) {
        memset(k, 0, sizeof(*k));
        k->dragging = true;
        k->last_ns = now_ns;
        return;
    }
    uint64_t dt = now_ns > k->last_ns ? now_ns - k->last_ns : 1;
    if (d == 0 && dt < KINETIC_STILL_NS)
        return;  // A frame without a move: the speed stands until the finger stays still.
    double speed = d / (dt / NS_PER_S);
    k->velocity = k->velocity * (1.0 - SPEED_WEIGHT) + speed * SPEED_WEIGHT;
    k->last_ns = now_ns;
}

void kineticRelease(Kinetic *k, uint64_t now_ns)
{
    bool held = k->dragging && now_ns > k->last_ns && now_ns - k->last_ns >= KINETIC_STILL_NS;
    k->dragging = false;
    k->carry = 0;
    if (held || fabs(k->velocity) < KINETIC_MIN_FLING) {
        k->velocity = 0;
        k->gliding = false;
        return;
    }
    if (k->velocity > KINETIC_MAX_SPEED)
        k->velocity = KINETIC_MAX_SPEED;
    if (k->velocity < -KINETIC_MAX_SPEED)
        k->velocity = -KINETIC_MAX_SPEED;
    k->gliding = true;
    k->last_ns = now_ns;
}

int kineticStep(Kinetic *k, uint64_t now_ns)
{
    if (!k->gliding)
        return 0;
    uint64_t dt = now_ns > k->last_ns ? now_ns - k->last_ns : 0;
    if (dt > MAX_FRAME_NS)
        dt = MAX_FRAME_NS;
    k->last_ns = now_ns;
    double ms = dt / NS_PER_MS;
    // Speed falls as decay^t: the distance over the frame is its integral.
    double rate = log(KINETIC_DECAY_PER_MS);
    double kept = pow(KINETIC_DECAY_PER_MS, ms);
    double distance = k->velocity / 1000.0 * (kept - 1.0) / rate;
    k->velocity *= kept;
    k->carry += distance;
    int whole = (int)k->carry;
    k->carry -= whole;
    if (fabs(k->velocity) < KINETIC_MIN_SPEED)
        kineticStop(k);
    return whole;
}

void kineticStop(Kinetic *k)
{
    k->velocity = 0;
    k->carry = 0;
    k->gliding = false;
    k->dragging = false;
}

bool kineticGliding(const Kinetic *k)
{
    return k->gliding;
}
