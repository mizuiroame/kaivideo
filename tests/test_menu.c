// The player's drop-down lists: cursor and scrolling.
#include "check.h"
#include "menu.h"

void testMenu(void)
{
    Menu m;
    menuOpen(&m, 3, 1);
    CHECK(m.open && m.cursor == 1 && m.first == 0 && menuVisible(&m) == 3);
    menuMove(&m, -5);
    CHECK(m.cursor == 0);
    menuMove(&m, 9);
    CHECK(m.cursor == 2 && m.first == 0);

    // A long list opens with the current row in view, near the middle.
    menuOpen(&m, 20, 12);
    CHECK(m.cursor == 12 && m.first == 9 && menuVisible(&m) == MENU_ROWS);
    menuMove(&m, 7);
    CHECK(m.cursor == 19 && m.first == 20 - MENU_ROWS);
    menuMove(&m, -19);
    CHECK(m.cursor == 0 && m.first == 0);
    menuOpen(&m, 20, 19);  // The last: the view ends at the end.
    CHECK(m.first == 20 - MENU_ROWS);

    // Nothing to choose: it doesn't open. A current row out of range is clamped.
    menuOpen(&m, 0, 0);
    CHECK(!m.open);
    menuOpen(&m, 4, -1);
    CHECK(m.open && m.cursor == 0);
    menuOpen(&m, 4, 9);
    CHECK(m.cursor == 3);
    menuClose(&m);
    CHECK(!m.open);
}
