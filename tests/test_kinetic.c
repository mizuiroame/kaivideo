// Scrolling as on a phone: the glide after a flick, and lists moved by pixels.
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "kinetic.h"
#include "libview.h"

#define MS 1000000ull
#define FRAME (16 * MS)

// A drag of d pixels a frame for frames frames, from t; returns the time after.
static uint64_t drag(Kinetic *k, int d, int frames, uint64_t t)
{
    kineticDrag(k, 0, t);
    for (int i = 0; i < frames; i++) {
        t += FRAME;
        kineticDrag(k, d, t);
    }
    return t;
}

// The glide's whole way, and how many frames it took.
static int glide(Kinetic *k, uint64_t t, int *frames)
{
    int total = 0;
    *frames = 0;
    while (kineticGliding(k) && *frames < 10000) {
        t += FRAME;
        total += kineticStep(k, t);
        (*frames)++;
    }
    return total;
}

static void testGlide(void)
{
    Kinetic k;
    memset(&k, 0, sizeof(k));
    uint64_t t = drag(&k, 30, 6, 1000 * MS);  // A flick up: 30 pixels a frame.
    kineticRelease(&k, t);
    CHECK(kineticGliding(&k) && k.velocity > 1500 && k.velocity < 2000);
    int frames, way = glide(&k, t, &frames);
    CHECK(way > 300 && way < 1200);           // It goes on, then slows down and stops.
    CHECK(frames > 30 && frames < 300 && !kineticGliding(&k));

    t = drag(&k, -20, 6, 5000 * MS);          // Down: the other way.
    kineticRelease(&k, t);
    CHECK(glide(&k, t, &frames) < 0);

    t = drag(&k, 30, 6, 9000 * MS);           // Held still before letting go: no glide.
    kineticRelease(&k, t + 200 * MS);
    CHECK(!kineticGliding(&k));

    t = drag(&k, 1, 6, 12000 * MS);           // A slow drag: no glide either.
    kineticRelease(&k, t);
    CHECK(!kineticGliding(&k));

    t = drag(&k, 30, 6, 15000 * MS);          // A press stops it where it is.
    kineticRelease(&k, t);
    kineticStep(&k, t + FRAME);
    kineticStop(&k);
    CHECK(!kineticGliding(&k) && kineticStep(&k, t + 2 * FRAME) == 0);

    t = drag(&k, 4000, 3, 20000 * MS);        // A wild flick has a top speed.
    kineticRelease(&k, t);
    CHECK(k.velocity <= KINETIC_MAX_SPEED);
}

static void testScrollPx(void)
{
    // 23 items, 5 a row: 5 rows of 100 pixels, 3 in view.
    Scroll s = {0};
    CHECK(!libviewScrollPx(&s, -10, 100, 3, 5, 23));  // At the top already.
    CHECK(libviewScrollPx(&s, 130, 100, 3, 5, 23) && s.first == 1 && s.offset == 30);
    CHECK(s.cursor == 5);                              // Off the top row: onto the first in view, same column.
    s.cursor = 7;
    CHECK(libviewScrollPx(&s, 50, 100, 3, 5, 23) && s.first == 1 && s.offset == 80);
    CHECK(s.cursor == 12);                             // Row 1 is mostly gone: row 2.
    CHECK(!libviewScrollPx(&s, 500, 100, 3, 5, 23) && s.first == 2 && s.offset == 0);  // Stops at the end.
    s.cursor = 22;
    CHECK(libviewScrollPx(&s, -150, 100, 3, 5, 23) && s.first == 0 && s.offset == 50);
    CHECK(s.cursor == 12);                             // Below the rows in view: up onto the last mostly shown.
    Scroll few = {0};                                  // All in view: nothing to scroll.
    CHECK(!libviewScrollPx(&few, 40, 100, 3, 5, 9) && few.first == 0 && few.offset == 0);
    Scroll none = {0};
    CHECK(!libviewScrollPx(&none, 40, 100, 3, 5, 0) && none.cursor == 0);
}

void testKinetic(void)
{
    testGlide();
    testScrollPx();
}
