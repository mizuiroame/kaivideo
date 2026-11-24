// The player's drop-down lists: cursor and scrolling.
#include "check.h"
#include "buttons.h"
#include "menu.h"

void testMenu(void)
{
    // In menus and panels the right stick is the d-pad; other buttons stay.
    CHECK(buttonsRightStickAsDpad(HidNpadButton_StickRUp) == HidNpadButton_Up);
    CHECK(buttonsRightStickAsDpad(HidNpadButton_StickRDown | HidNpadButton_A) == (HidNpadButton_Down | HidNpadButton_A));
    CHECK(buttonsRightStickAsDpad(HidNpadButton_StickRLeft) == HidNpadButton_Left);
    CHECK(buttonsRightStickAsDpad(HidNpadButton_StickRRight) == HidNpadButton_Right);
    CHECK(buttonsRightStickAsDpad(HidNpadButton_StickLUp) == HidNpadButton_StickLUp);
    Menu m;
    menuOpen(&m, 3, 0, 1);
    CHECK(m.open && m.cursor == 1 && m.first == 0 && menuVisible(&m) == 3);
    menuMove(&m, -5);
    CHECK(m.cursor == 0);
    menuMove(&m, 9);
    CHECK(m.cursor == 2 && m.first == 0);

    // A long list opens with the current row in view, near the middle.
    menuOpen(&m, 20, 0, 12);
    CHECK(m.cursor == 12 && m.first == 9 && menuVisible(&m) == MENU_ROWS);
    menuMove(&m, 7);
    CHECK(m.cursor == 19 && m.first == 20 - MENU_ROWS);
    menuMove(&m, -19);
    CHECK(m.cursor == 0 && m.first == 0);
    menuOpen(&m, 20, 0, 19);  // The last: the view ends at the end.
    CHECK(m.first == 20 - MENU_ROWS);

    // Nothing to choose: it doesn't open. A current row out of range is clamped.
    menuOpen(&m, 0, 0, 0);
    CHECK(!m.open);
    menuOpen(&m, 4, 0, -1);
    CHECK(m.open && m.cursor == 0);
    menuOpen(&m, 4, 0, 9);
    CHECK(m.cursor == 3);
    menuClose(&m);
    CHECK(!m.open);

    // 12 subtitle rows and 3 fixed ones: going into the fixed rows leaves the list where it was.
    menuOpen(&m, 15, 3, 11);
    CHECK(menuVisible(&m) == MENU_ROWS && m.first == 12 - MENU_ROWS && !menuOnFixed(&m));
    menuMove(&m, 2);
    CHECK(m.cursor == 13 && menuOnFixed(&m) && m.first == 12 - MENU_ROWS);
    menuMove(&m, 9);
    CHECK(m.cursor == 14);
    menuMove(&m, -14);
    CHECK(m.cursor == 0 && m.first == 0);

    // Many fixed rows (the Playback list's options) leave fewer list rows, so all fit the screen.
    menuOpen(&m, 9 + 7, 7, 4);
    CHECK(menuVisible(&m) == MENU_ALL_ROWS - 7 && m.first + menuVisible(&m) > 4 && m.first <= 4);
    menuOpen(&m, 9 + 10, 10, 8);
    CHECK(menuVisible(&m) == MENU_MIN_ROWS && m.first == 9 - MENU_MIN_ROWS);

    // Fewer fixed rows: the list stays, and a cursor on a row that went comes back.
    menuOpen(&m, 5, 3, 1);
    menuSetFixed(&m, 1);
    CHECK(m.count == 3 && m.fixed == 1 && m.cursor == 1 && !menuOnFixed(&m));
    menuSetFixed(&m, 3);
    CHECK(m.count == 5 && m.fixed == 3 && m.cursor == 1);
    menuOpen(&m, 5, 3, 4);
    menuSetFixed(&m, 1);
    CHECK(m.cursor == 2 && menuOnFixed(&m));
}
