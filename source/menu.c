#include "menu.h"

static void keepInView(Menu *m)
{
    if (m->cursor < m->first)
        m->first = m->cursor;
    if (m->cursor >= m->first + MENU_ROWS)
        m->first = m->cursor - MENU_ROWS + 1;
    int last_first = m->count > MENU_ROWS ? m->count - MENU_ROWS : 0;
    if (m->first > last_first)
        m->first = last_first;
    if (m->first < 0)
        m->first = 0;
}

void menuOpen(Menu *m, int count, int current)
{
    m->open = count > 0;
    m->count = count > 0 ? count : 0;
    m->cursor = current < 0 ? 0 : current >= m->count ? m->count - 1 : current;
    if (m->cursor < 0)
        m->cursor = 0;
    // The current row in the middle when the list scrolls.
    m->first = m->cursor - MENU_ROWS / 2;
    keepInView(m);
}

void menuClose(Menu *m)
{
    m->open = false;
}

void menuMove(Menu *m, int delta)
{
    if (m->count == 0)
        return;
    int next = m->cursor + delta;
    m->cursor = next < 0 ? 0 : next >= m->count ? m->count - 1 : next;
    keepInView(m);
}

int menuVisible(const Menu *m)
{
    return m->count < MENU_ROWS ? m->count : MENU_ROWS;
}
