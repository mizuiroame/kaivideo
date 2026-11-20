// Scrolling as on a phone: the glide after a flick, and lists moved by pixels.
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "kinetic.h"
#include "browser.h"
#include "libview.h"
#include "menu.h"

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
    CHECK(!scrollPx(&s, -10, 100, 3, 5, 23));  // At the top already.
    CHECK(scrollPx(&s, 130, 100, 3, 5, 23) && s.first == 1 && s.offset == 30);
    CHECK(s.cursor == 5);                              // Off the top row: onto the first in view, same column.
    s.cursor = 7;
    CHECK(scrollPx(&s, 50, 100, 3, 5, 23) && s.first == 1 && s.offset == 80);
    CHECK(s.cursor == 12);                             // Row 1 is mostly gone: row 2.
    CHECK(!scrollPx(&s, 500, 100, 3, 5, 23) && s.first == 2 && s.offset == 0);  // Stops at the end.
    s.cursor = 22;
    CHECK(scrollPx(&s, -150, 100, 3, 5, 23) && s.first == 0 && s.offset == 50);
    CHECK(s.cursor == 12);                             // Below the rows in view: up onto the last mostly shown.
    Scroll few = {0};                                  // All in view: nothing to scroll.
    CHECK(!scrollPx(&few, 40, 100, 3, 5, 9) && few.first == 0 && few.offset == 0);
    Scroll none = {0};
    CHECK(!scrollPx(&none, 40, 100, 3, 5, 0) && none.cursor == 0);
}

// The player's lists: only the list moves, not the fixed rows under it.
static void testMenuScroll(void)
{
    Menu m = {0};
    menuOpen(&m, 30, 4, 0);  // 26 rows to scroll, 4 fixed options.
    CHECK(menuScrollPx(&m, 120, 48) && m.first == 2 && m.offset == 24 && m.cursor == 2);
    menuMove(&m, 1);         // A button: the list rests on a row again.
    CHECK(m.offset == 0 && m.cursor == 3);
    m.cursor = 28;           // On an option: it stays there while the list moves.
    CHECK(menuScrollPx(&m, 48, 48) && m.cursor == 28);
    CHECK(!menuScrollPx(&m, 5000, 48) && m.offset == 0);
}

// The file list: free where a finger leaves it, the cursor in its middle again after a button.
static void testBrowserScroll(void)
{
    BrowserEntry items[40];
    memset(items, 0, sizeof(items));
    Browser b = {.items = items, .count = 40, .cursor = 20};
    CHECK(browserFirst(&b, 10) == 15 && !b.free);
    CHECK(browserScrollPx(&b, 70, 58, 10) && b.free && b.first == 16 && b.offset == 12);
    CHECK(browserFirst(&b, 10) == 16 && b.cursor == 20);
    CHECK(browserScrollPx(&b, -16 * 58 - 12, 58, 10) && b.first == 0 && b.cursor == 9);
}

void testKinetic(void)
{
    testGlide();
    testScrollPx();
    testMenuScroll();
    testBrowserScroll();
}
