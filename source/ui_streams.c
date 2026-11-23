#include "ui_streams.h"

#include <stdio.h>

#include "font.h"
#include "gfx.h"
#include "lang.h"
#include "ui.h"
#include "ui_library.h"
#include "ui_style.h"

#define PANEL_W 900
#define ROW_H 64
#define TOP_H 64
#define PANEL_H (TOP_H + UI_STREAM_ROWS * ROW_H + 20)

static void drawRow(const StremioStream *s, int index, int x, int y, int w, bool focused)
{
    if (focused) {
        gfxFillRoundRect(x + 12, y + 2, w - 24, ROW_H - 4, 10, COLOR_SURFACE_HI);
        gfxFillRect(x + 12, y + 14, 3, ROW_H - 28, COLOR_ACCENT);
    }
    int tx = x + 32, right = x + w - 32;
    int addon_w = fontTextWidth(FONT_REGULAR, 15, s->addon);
    fontDraw(FONT_REGULAR, 15, right - addon_w, y + 10, s->addon, COLOR_FAINT);
    char name[160];
    snprintf(name, sizeof(name), "%s", s->name ? s->name : T("Stream"));
    fontDrawClipped(FONT_BOLD, 19, tx, y + 8, right - addon_w - 16 - tx, name, focused ? COLOR_TEXT : COLOR_DIM);
    if (s->detail)
        fontDrawClipped(FONT_REGULAR, 15, tx, y + 36, right - tx, s->detail, COLOR_FAINT);
    uiLibraryAddHit(x + 12, y, w - 24, ROW_H, LIBHIT_STREAM_ROW, index, 0);
}

void uiDrawStreams(const char *title, const StremioStream *streams, int count, int cursor, int first, bool loading,
                   const char *note)
{
    gfxBlendRect(0, 0, GFX_WIDTH, GFX_HEIGHT, GFX_RGB(0, 0, 0), UI_DIM_ALPHA);
    uiLibraryAddHit(0, 0, GFX_WIDTH, GFX_HEIGHT, LIBHIT_CLOSE, 0, 0);
    int x = (GFX_WIDTH - PANEL_W) / 2, y = (UI_TAB_BAR_H + UI_FOOTER_Y - PANEL_H) / 2;
    gfxFillRoundRect(x, y, PANEL_W, PANEL_H, 16, COLOR_SURFACE);
    uiLibraryAddHit(x, y, PANEL_W, PANEL_H, LIBHIT_PANEL, 0, 0);
    // "< Title": back without playing.
    int bx = x + 28, by = y + 22;
    gfxLine(bx + 8, by + 4, bx, by + 13, 3, COLOR_TEXT);
    gfxLine(bx, by + 13, bx + 8, by + 22, 3, COLOR_TEXT);
    fontDrawClipped(FONT_BOLD, 21, bx + 22, by + 1, PANEL_W - 300, title, COLOR_TEXT);
    uiLibraryAddHit(x, y, 300, TOP_H, LIBHIT_CLOSE, 0, 0);
    const char *heading = T("Choose a stream");
    int hw = fontTextWidth(FONT_REGULAR, 16, heading);
    fontDraw(FONT_REGULAR, 16, x + PANEL_W - 32 - hw, by + 4, heading, COLOR_FAINT);
    int list_y = y + TOP_H;
    float middle = list_y + UI_STREAM_ROWS * ROW_H / 2.0f;
    if (count == 0) {
        const char *line = loading ? T("Asking the addons for streams...")
                                   : T("No stream KaiVideo can play: torrents and outside links are left out.");
        fontDrawCentered(FONT_BOLD, 20, GFX_WIDTH / 2.0f, middle - 14, line, loading ? COLOR_DIM : COLOR_TEXT);
        if (!loading && note && note[0])
            fontDrawCentered(FONT_REGULAR, 15, GFX_WIDTH / 2.0f, middle + 18, note, COLOR_FAINT);
        return;
    }
    for (int i = first; i < count && i < first + UI_STREAM_ROWS; i++)
        drawRow(&streams[i], i, x, list_y + (i - first) * ROW_H, PANEL_W, i == cursor);
    if (count > UI_STREAM_ROWS) {  // How far down the list.
        int track_h = UI_STREAM_ROWS * ROW_H - 16, thumb_h = track_h * UI_STREAM_ROWS / count;
        int thumb_y = list_y + 8 + (track_h - thumb_h) * first / (count - UI_STREAM_ROWS);
        gfxFillRoundRect(x + PANEL_W - 10, list_y + 8, 4, track_h, 2, COLOR_TRACK);
        gfxFillRoundRect(x + PANEL_W - 10, thumb_y, 4, thumb_h, 2, COLOR_FAINT);
    }
}
