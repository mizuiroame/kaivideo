#include "menu.h"

static int listCount(const Menu *m)
{
    return m->count - m->fixed;
}

static void keepInView(Menu *m)
{
    int list = listCount(m);
    if (m->cursor < list) {  // The fixed rows at the bottom don't scroll the list.
        if (m->cursor < m->first)
            m->first = m->cursor;
        if (m->cursor >= m->first + MENU_ROWS)
            m->first = m->cursor - MENU_ROWS + 1;
    }
    int last_first = list > MENU_ROWS ? list - MENU_ROWS : 0;
    if (m->first > last_first)
        m->first = last_first;
    if (m->first < 0)
        m->first = 0;
}

void menuOpen(Menu *m, int count, int fixed, int current)
{
    m->count = count > 0 ? count : 0;
    m->fixed = fixed < 0 ? 0 : fixed > m->count ? m->count : fixed;
    m->open = m->count > 0;
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

void menuSetFixed(Menu *m, int fixed)
{
    int list = listCount(m);
    m->fixed = fixed < 0 ? 0 : fixed;
    m->count = list + m->fixed;
    if (m->cursor >= m->count)
        m->cursor = m->count > 0 ? m->count - 1 : 0;
    keepInView(m);
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
    return listCount(m) < MENU_ROWS ? listCount(m) : MENU_ROWS;
}

bool menuOnFixed(const Menu *m)
{
    return m->cursor >= listCount(m);
}
