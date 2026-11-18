// Settings and About, drawn over the library's screens.
#include "ui_settings.h"

#include <stdio.h>
#include <string.h>

#include "font.h"
#include "gfx.h"
#include "ui_library.h"
#include "ui_style.h"

#define PANEL_W 780
#define PANEL_PAD 32
#define HEAD_H 76
#define ROW_H 52
#define FOOT_H 52
#define ABOUT_W 720
#define ABOUT_H 480

// Darkens what's behind, and makes a tap there close the panel.
static void dimScreen(void)
{
    gfxBlendRect(0, 0, GFX_WIDTH, GFX_HEIGHT, GFX_RGB(0, 0, 0), 170);
    uiLibraryAddHit(0, 0, GFX_WIDTH, GFX_HEIGHT, LIBHIT_CLOSE, 0, 0);
}

static void panel(int x, int y, int w, int h)
{
    gfxFillRoundRect(x - 2, y - 2, w + 4, h + 4, 20, COLOR_SURFACE_HI);
    gfxFillRoundRect(x, y, w, h, 18, COLOR_SURFACE);
    uiLibraryAddHit(x, y, w, h, LIBHIT_PANEL, 0, 0);
}

static void backArrow(int x, int y)
{
    gfxLine((float)x + 10, (float)y, (float)x, (float)y + 11, 3, COLOR_TEXT);
    gfxLine((float)x, (float)y + 11, (float)x + 10, (float)y + 22, 3, COLOR_TEXT);
    uiLibraryAddHit(x - 16, y - 16, 48, 54, LIBHIT_CLOSE, 0, 0);
}

static void rowValue(SettingRow row, const Config *c, const char *library_root, char *out, size_t size)
{
    if (row == SETTING_LIBRARY)
        snprintf(out, size, "%s", library_root[0] ? library_root : "Not chosen");
    else
        settingValue(c, row, out, size);
}

void uiDrawSettings(const Config *c, int cursor, const char *library_root)
{
    int h = HEAD_H + SETTING_COUNT * ROW_H + FOOT_H;
    int x = (GFX_WIDTH - PANEL_W) / 2, y = (GFX_HEIGHT - h) / 2;
    dimScreen();
    panel(x, y, PANEL_W, h);
    backArrow(x + PANEL_PAD, y + 28);
    fontDraw(FONT_BOLD, 26, x + PANEL_PAD + 30, y + 22, "Settings", COLOR_TEXT);

    char value[200];
    for (int i = 0; i < SETTING_COUNT; i++) {
        int row_y = y + HEAD_H + i * ROW_H;
        bool selected = i == cursor, choice = settingIsChoice((SettingRow)i);
        if (selected) {
            gfxFillRoundRect(x + 14, row_y + 2, PANEL_W - 28, ROW_H - 4, 10, COLOR_SURFACE_HI);
            gfxFillRect(x + 14, row_y + 14, 3, ROW_H - 28, COLOR_ACCENT);
        }
        float cy = row_y + ROW_H / 2.0f;
        fontDrawMiddle(selected ? FONT_BOLD : FONT_REGULAR, 20, x + PANEL_PAD, cy, settingLabel((SettingRow)i),
                       COLOR_TEXT);
        rowValue((SettingRow)i, c, library_root, value, sizeof(value));
        int right = x + PANEL_W - PANEL_PAD;
        if (choice) {  // < value >: the arrows step it.
            gfxFillTriangleRight(right - 10, (int)cy - 8, 10, 16, selected ? COLOR_TEXT : COLOR_DIM);
            int value_w = fontTextWidth(FONT_BOLD, 19, value);
            if (value_w > 260)
                value_w = 260;
            fontDrawClipped(FONT_BOLD, 19, right - 22 - value_w, (int)cy - 11, value_w, value, COLOR_ACCENT);
            gfxFillTriangleLeft(right - 44 - value_w, (int)cy - 8, 10, 16, selected ? COLOR_TEXT : COLOR_DIM);
            uiLibraryAddHit(right - 64 - value_w, row_y, value_w / 2 + 40, ROW_H, LIBHIT_SETTING_PREV, i, 0);
            uiLibraryAddHit(right - 24 - value_w / 2, row_y, value_w / 2 + 40, ROW_H, LIBHIT_SETTING_NEXT, i, 0);
            uiLibraryAddHit(x + 14, row_y, PANEL_W - 28 - value_w - 80, ROW_H, LIBHIT_SETTING, i, 0);
        } else {  // Opens something: an arrow into it.
            int value_w = value[0] ? fontTextWidth(FONT_REGULAR, 18, value) : 0;
            if (value_w > 360)
                value_w = 360;
            if (value_w)
                fontDrawClipped(FONT_REGULAR, 18, right - 22 - value_w, (int)cy - 10, value_w, value, COLOR_DIM);
            gfxFillTriangleRight(right - 10, (int)cy - 8, 10, 16, selected ? COLOR_TEXT : COLOR_FAINT);
            uiLibraryAddHit(x + 14, row_y, PANEL_W - 28, ROW_H, LIBHIT_SETTING, i, 0);
        }
    }
    fontDrawClipped(FONT_REGULAR, 16, x + PANEL_PAD, y + h - 38, PANEL_W - 2 * PANEL_PAD,
                    "Up Down  choose      Left Right  change      A  open      B  close", COLOR_FAINT);
}

// A screen with a play symbol: KaiVideo's mark.
static void drawMark(int x, int y, int size)
{
    gfxFillRoundRect(x, y, size, size, size / 4, COLOR_SURFACE_HI);
    gfxFillRoundRect(x + size / 6, y + size / 4, size * 2 / 3, size / 2, 6, COLOR_ACCENT);
    float cx = x + size / 2.0f + 3, cy = y + size / 2.0f;
    gfxTriangle(cx - 9, cy - 11, cx - 9, cy + 11, cx + 11, cy, COLOR_SURFACE);
}

void uiDrawAbout(const char *version)
{
    static const char *const LINES[] = {
        "A video player for Switch 2 homebrew on kaiHEN.",
        "Plays MP4, MKV and WebM files from your card with the console's",
        "own video decoder (H.264, VP9, AV1, VP8), with sound tracks,",
        "subtitles and a library of your films and series.",
        "",
        "Free and open source under the MIT license; FFmpeg under the LGPL.",
        "Not affiliated with or endorsed by Nintendo.",
    };
    int x = (GFX_WIDTH - ABOUT_W) / 2, y = (GFX_HEIGHT - ABOUT_H) / 2;
    dimScreen();
    panel(x, y, ABOUT_W, ABOUT_H);
    drawMark(x + PANEL_PAD, y + 32, 84);
    int text_x = x + PANEL_PAD + 108;
    fontDraw(FONT_BOLD, 34, text_x, y + 36, "KaiVideo", COLOR_TEXT);
    char line[64];
    snprintf(line, sizeof(line), "Version %s", version);
    fontDraw(FONT_REGULAR, 20, text_x, y + 84, line, COLOR_DIM);
    int line_y = y + 150;
    fontDraw(FONT_REGULAR, 18, x + PANEL_PAD, line_y, "Made by", COLOR_DIM);
    fontDraw(FONT_BOLD, 26, x + PANEL_PAD, line_y + 24, "Mizuiro", COLOR_ACCENT);
    line_y += 82;
    for (size_t i = 0; i < sizeof(LINES) / sizeof(LINES[0]); i++, line_y += 28)
        fontDraw(FONT_REGULAR, 19, x + PANEL_PAD, line_y, LINES[i], COLOR_TEXT);
    fontDraw(FONT_BOLD, 18, x + PANEL_PAD, y + ABOUT_H - 46, "github.com/mizuiroame/kaivideo", COLOR_ACCENT);
    fontDrawMiddle(FONT_REGULAR, 16, x + ABOUT_W - PANEL_PAD - fontTextWidth(FONT_REGULAR, 16, "B  close"),
                   y + ABOUT_H - 36, "B  close", COLOR_FAINT);
}
