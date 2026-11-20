#include "scroll.h"

static int clamp(int value, int low, int high)
{
    return value < low ? low : value > high ? high : value;
}

bool scrollPx(Scroll *s, int px, int row_h, int visible, int cols, int count)
{
    int rows = cols > 0 ? (count + cols - 1) / cols : 0;
    int end = rows > visible ? (rows - visible) * row_h : 0;
    int at = s->first * row_h + s->offset, to = clamp(at + px, 0, end);
    s->first = to / row_h;
    s->offset = to % row_h;
    if (count > 0) {  // The cursor stays on a row mostly in view, in its column.
        int top = s->first + (s->offset * 2 > row_h ? 1 : 0);
        int bottom = clamp(top + visible - 1, 0, rows - 1);
        int row = s->cursor / cols, col = s->cursor % cols;
        if (row < top)
            s->cursor = clamp(top * cols + col, 0, count - 1);
        else if (row > bottom)
            s->cursor = clamp(bottom * cols + col, 0, count - 1);
    }
    return to != at && to == at + px;
}
