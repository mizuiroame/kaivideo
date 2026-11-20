// Settings and About, drawn over the library's screens the way KaiPlayer
// draws its own: a plain panel and a Back pill on a page (the help line at
// the bottom is the app's).
#include "ui_settings.h"

#include <stdio.h>
#include <string.h>

#include "font.h"
#include "gfx.h"
#include "ui_buttons.h"
#include "ui_library.h"
#include "ui_style.h"

#define SETTINGS_W 760
#define ROW_H 48
#define PANEL_PAD 32
#define HEAD_H 76
#define FOOT_H 60
#define ABOUT_W 700
#define ABOUT_H 470

// Darkens what's behind, and makes a tap there close the panel.
static void dimScreen(void)
{
    gfxBlendRect(0, 0, GFX_WIDTH, GFX_HEIGHT, GFX_RGB(0, 0, 0), UI_DIM_ALPHA);
    uiLibraryAddHit(0, 0, GFX_WIDTH, GFX_HEIGHT, LIBHIT_CLOSE, 0, 0);
}

static void panel(int x, int y, int w, int h)
{
    gfxFillRoundRect(x, y, w, h, 16, COLOR_SURFACE);
    uiLibraryAddHit(x, y, w, h, LIBHIT_PANEL, 0, 0);  // Taps on the panel itself do nothing.
}

// "◀ Back", as KaiPlayer's pages have it. Returns its width.
static int backButton(int x, int y)
{
    static const char LABEL[] = "Back";
    int w = fontTextWidth(FONT_BOLD, 17, LABEL) + 44;
    float mid = (float)y + 15.0f;
    gfxFillRoundRect(x, y, w, 30, 15, COLOR_SURFACE_HI);
    gfxTriangle((float)x + 12.0f, mid, (float)x + 21.0f, mid - 6.5f, (float)x + 21.0f, mid + 6.5f, COLOR_TEXT);
    fontDrawMiddle(FONT_BOLD, 17, x + 28, mid, LABEL, COLOR_TEXT);
    uiLibraryAddHit(x - 8, y - 10, w + 16, 50, LIBHIT_PAGE_BACK, 0, 0);
    return w;
}

static void rowHighlight(int x, int y, int w, int h)
{
    gfxFillRoundRect(x, y, w, h, 10, COLOR_SURFACE_HI);
    gfxFillRect(x, y + 10, 3, h - 20, COLOR_ACCENT);
}

static const char *g_jf_server = "", *g_jf_user = "";

void uiSettingsSetJellyfin(const char *server, const char *user)
{
    g_jf_server = server;
    g_jf_user = user;
}

// What a row shows on its right: a choice's value, the folder's name, the version.
static void rowValue(SettingRow row, const Config *c, const char *library_root, const char *version, char *out,
                     size_t size)
{
    if (row == SETTING_LIBRARY) {
        const char *slash = strrchr(library_root, '/');
        snprintf(out, size, "%s", !library_root[0] ? "Not chosen" : slash && slash[1] ? slash + 1 : library_root);
    } else if (row == SETTING_ABOUT) {
        snprintf(out, size, "KaiVideo %s", version);
    } else if (row == SETTING_JF_SERVER || row == SETTING_PLEX_SERVER) {
        snprintf(out, size, "%s", g_jf_server[0] ? g_jf_server : "Not set");
    } else if (row == SETTING_JF_SIGN_OUT || row == SETTING_OPEN_JELLYFIN) {
        snprintf(out, size, "%s", g_jf_user);
    } else {
        settingValue(c, row, out, size);
    }
}

void uiDrawSettings(const Config *c, SettingsPage page, int cursor, const char *library_root, const char *version)
{
    SettingRow rows[SETTINGS_PAGE_MAX_ROWS];
    int count = settingsPageRows(page, g_jf_user[0] != '\0', c->use_plex, rows);
    int h = HEAD_H + count * ROW_H + FOOT_H;
    int x = (GFX_WIDTH - SETTINGS_W) / 2, y = (GFX_HEIGHT - h) / 2;
    dimScreen();
    panel(x, y, SETTINGS_W, h);
    int title_x = x + PANEL_PAD;
    if (page != SETTINGS_PAGE_MAIN)  // A page: back to the main one.
        title_x += backButton(x + PANEL_PAD - 4, y + 18) + 12;
    fontDraw(FONT_BOLD, 24, title_x, y + 22, settingsPageTitle(page), COLOR_TEXT);

    char value[200];
    for (int n = 0; n < count; n++) {
        SettingRow row = rows[n];
        int row_y = y + HEAD_H + n * ROW_H;
        bool selected = (int)row == cursor, choice = settingIsChoice(row);
        bool opens = row == SETTING_OPEN_DISPLAY || row == SETTING_OPEN_JELLYFIN;
        if (selected)
            rowHighlight(x + 16, row_y, SETTINGS_W - 32, ROW_H - 6);
        fontDraw(selected ? FONT_BOLD : FONT_REGULAR, 21, x + PANEL_PAD, row_y + 8, settingLabel(row), COLOR_TEXT);
        rowValue(row, c, library_root, version, value, sizeof(value));
        int value_w = fontTextWidth(FONT_BOLD, 20, value);
        if (value_w > 300)
            value_w = 300;
        int right = x + SETTINGS_W - PANEL_PAD;
        int value_x = right - value_w - (choice || opens ? 24 : 0);
        if (value[0])
            fontDrawClipped(FONT_BOLD, 20, value_x, row_y + 9, value_w, value, choice ? COLOR_ACCENT : COLOR_DIM);
        if (opens) {  // A page: an arrow into it.
            gfxFillTriangleRight(right - 12, row_y + 13, 10, 16, selected ? COLOR_TEXT : COLOR_DIM);
        } else if (choice) {
            gfxFillTriangleLeft(value_x - 22, row_y + 13, 10, 16, COLOR_DIM);
            gfxFillTriangleRight(right - 12, row_y + 13, 10, 16, COLOR_DIM);
        } else if (selected) {  // A opens or does it.
            uiDrawButton(value_x - (value[0] ? 34 : 22), (float)row_y + 21.0f, "A", COLOR_SURFACE, COLOR_TEXT);
        }
        if (choice) {  // The arrows step it; the rest of the row too.
            uiLibraryAddHit(x + 16, row_y, SETTINGS_W - 32, ROW_H - 6, LIBHIT_SETTING, row, 0);
            uiLibraryAddHit(value_x - 40, row_y, value_w / 2 + 40, ROW_H - 6, LIBHIT_SETTING_PREV, row, 0);
            uiLibraryAddHit(value_x + value_w / 2, row_y, right - value_x - value_w / 2 + 16, ROW_H - 6,
                            LIBHIT_SETTING_NEXT, row, 0);
        } else {
            uiLibraryAddHit(x + 16, row_y, SETTINGS_W - 32, ROW_H - 6, LIBHIT_SETTING, row, 0);
        }
    }
    fontDrawClipped(FONT_REGULAR, 16, x + PANEL_PAD, y + h - 40, SETTINGS_W - 2 * PANEL_PAD,
                    "Open source (MIT)  \xC2\xB7  github.com/mizuiroame/kaivideo", COLOR_FAINT);
}

// A screen with a play symbol: KaiVideo's mark.
static void drawMark(int x, int y, int size)
{
    gfxFillRoundRect(x, y, size, size, 20, COLOR_SURFACE_HI);
    gfxFillRoundRect(x + size / 6, y + size / 4, size * 2 / 3, size / 2, 6, COLOR_ACCENT);
    float cx = x + size / 2.0f + 3, cy = y + size / 2.0f;
    gfxTriangle(cx - 9, cy - 11, cx - 9, cy + 11, cx + 11, cy, COLOR_SURFACE);
}

void uiDrawSignInCode(bool plex, const char *code, const char *server, bool linked)
{
    int w = 680, h = 340, x = (GFX_WIDTH - w) / 2, y = (GFX_HEIGHT - h) / 2;
    dimScreen();
    panel(x, y, w, h);
    fontDraw(FONT_BOLD, 26, x + PANEL_PAD, y + 26, plex ? "Sign in to Plex" : "Sign in to Jellyfin", COLOR_TEXT);
    fontDrawClipped(FONT_REGULAR, 18, x + PANEL_PAD, y + 62, w - 2 * PANEL_PAD, plex ? "plex.tv" : server, COLOR_DIM);
    if (!code[0] || linked) {
        const char *doing = linked ? "Linked. Finding your Plex server..."
                            : plex ? "Contacting plex.tv..." : "Contacting the server...";
        fontDraw(FONT_REGULAR, 21, x + PANEL_PAD, y + 150, doing, COLOR_TEXT);
        return;
    }
    int code_w = fontTextWidth(FONT_BOLD, 64, code);
    fontDraw(FONT_BOLD, 64, x + (w - code_w) / 2, y + 104, code, COLOR_ACCENT);
    fontDraw(FONT_REGULAR, 19, x + PANEL_PAD, y + 216,
             plex ? "On your phone or computer, go to plex.tv/link" : "In Jellyfin on your phone or computer, open your",
             COLOR_TEXT);
    fontDraw(FONT_REGULAR, 19, x + PANEL_PAD, y + 244,
             plex ? "and enter this code." : "profile, choose Quick Connect and enter this code.", COLOR_TEXT);
}

void uiDrawAbout(const char *version)
{
    static const char *const LINES[] = {
        "A video player for Switch 2 homebrew on kaiHEN.",
        "Plays MP4, MKV and WebM from your SD card or a Jellyfin or Plex server, with",
        "sound tracks, subtitles and a library of your films and series.",
        "",
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
}
