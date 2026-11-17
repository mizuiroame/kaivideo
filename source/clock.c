#include "clock.h"

void smoothClockReset(SmoothClock *c)
{
    *c = (SmoothClock){0};
}

int64_t smoothClockRead(SmoothClock *c, int64_t raw_ns, int64_t now_ns, int64_t step_ns, bool running)
{
    if (!c->have || raw_ns != c->raw_ns) {
        c->raw_ns = raw_ns;
        c->changed_at_ns = now_ns;
    }
    int64_t ahead = running ? now_ns - c->changed_at_ns : 0;
    if (ahead > step_ns)
        ahead = step_ns;  // The sound stalled: wait for it.
    int64_t out = raw_ns + ahead;
    // A step that comes a little early would take the clock back: hold it.
    // A bigger jump back (a seek) is taken as it is.
    if (c->have && out < c->out_ns && c->out_ns - out <= 2 * step_ns)
        out = c->out_ns;
    c->have = true;
    c->out_ns = out;
    return out;
}
