#include "menu.h"

#include "scroll.h"

static int listCount(const Menu *m)
{
    return m->count - m->fixed;
}

// List rows that fit with the fixed ones under them: the screen takes
// MENU_ALL_ROWS in all, and the list keeps at least MENU_MIN_ROWS.
static int rowsInView(const Menu *m)
{
    int room = MENU_ALL_ROWS - m->fixed;
    return room > MENU_ROWS ? MENU_ROWS : room < MENU_MIN_ROWS ? MENU_MIN_ROWS : room;
}

static void keepInView(Menu *m)
{
    m->offset = 0;  // A button moved it: the list rests on a row.
    int list = listCount(m), rows = rowsInView(m);
    if (m->cursor < list) {  // The fixed rows at the bottom don't scroll the list.
        if (m->cursor < m->first)
            m->first = m->cursor;
        if (m->cursor >= m->first + rows)
            m->first = m->cursor - rows + 1;
    }
    int last_first = list > rows ? list - rows : 0;
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
    m->first = m->cursor - rowsInView(m) / 2;
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
    return listCount(m) < rowsInView(m) ? listCount(m) : rowsInView(m);
}

bool menuOnFixed(const Menu *m)
{
    return m->cursor >= listCount(m);
}

bool menuScrollPx(Menu *m, int px, int row_h)
{
    int list = listCount(m);
    bool on_list = m->cursor < list;
    Scroll s = {on_list ? m->cursor : m->first, m->first, m->offset};
    bool moved = scrollPx(&s, px, row_h, rowsInView(m), 1, list);
    m->first = s.first;
    m->offset = s.offset;
    if (on_list)  // On a fixed row below, the cursor stays there.
        m->cursor = s.cursor;
    return moved;
}
