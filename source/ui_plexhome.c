#include "ui_plexhome.h"

#include <stdio.h>
#include <string.h>

#include "font.h"
#include "gfx.h"
#include "lang.h"
#include "thumbs.h"
#include "ui_library.h"
#include "ui_library_shared.h"
#include "ui_style.h"

#define PANEL_PAD 32
#define PANEL_R 16
#define HEAD_H 96
#define TILE_W 184
#define TILE_H 168
#define AVATAR_D 104
#define FOCUS_RING 5
#define NAME_PX 19
#define PAD_W 520
#define PAD_H 580
#define KEY_W 120
#define KEY_H 76
#define KEY_GAP 12
#define KEY_R 14
#define DOT_R 11
#define DOT_GAP 40
#define POSTER_PREFIX "poster:"  // thumbgen.c: a picture at an address, filling its box.

// --- Pieces ---

static void dimScreen(void)
{
    gfxBlendRect(0, 0, GFX_WIDTH, GFX_HEIGHT, GFX_RGB(0, 0, 0), UI_DIM_ALPHA);
    uiLibraryAddHit(0, 0, GFX_WIDTH, GFX_HEIGHT, LIBHIT_CLOSE, 0, 0);
}

static void panel(int x, int y, int w, int h)
{
    gfxFillRoundRect(x, y, w, h, PANEL_R, COLOR_SURFACE);
    uiLibraryAddHit(x, y, w, h, LIBHIT_PANEL, 0, 0);
}

// The first letter of a name (one UTF-8 character), for an avatar without a picture.
static void initial(const char *name, char out[8])
{
    size_t n = 1;
    unsigned char c = (unsigned char)name[0];
    if (c >= 0xF0)
        n = 4;
    else if (c >= 0xE0)
        n = 3;
    else if (c >= 0xC0)
        n = 2;
    if (!c || strnlen(name, n) < n)
        n = c ? 1 : 0;
    snprintf(out, 8, "%.*s", (int)n, name);
    if (n == 1 && out[0] >= 'a' && out[0] <= 'z')
        out[0] = (char)(out[0] - 'a' + 'A');
}

// Plex's picture of the profile (fetched from plex.tv, kept with the posters), or its first letter.
static void drawAvatar(const PlexHomeUser *u, int x, int y, int d)
{
    char art[PLEX_URI_MAX + sizeof(POSTER_PREFIX)];
    const uint32_t *pixels = NULL;
    if (u->thumb[0]) {
        snprintf(art, sizeof(art), POSTER_PREFIX "%s", u->thumb);
        pixels = thumbGetPoster(art);
    }
    if (pixels) {  // Posters are 2:3: the middle of it, a square.
        float scale = (float)d / THUMB_POSTER_W;
        float h = THUMB_POSTER_H * scale;
        gfxSetClipRound(x, y, d, d, d / 2);
        gfxDrawImageScaledFb(x * GFX_SCALE, (y + (d - h) / 2) * GFX_SCALE, d * GFX_SCALE, h * GFX_SCALE,
                             THUMB_POSTER_W, THUMB_POSTER_H, pixels);
        gfxResetClip();
        return;
    }
    Color top, bottom;
    uiLibCardColors(u->title, &top, &bottom);
    gfxFillCircle(x + d / 2, y + d / 2, d / 2, top);
    char letter[8];
    initial(u->title, letter);
    int px = d / 2, w = fontTextWidth(FONT_BOLD, px, letter);
    fontDrawMiddle(FONT_BOLD, px, x + (d - w) / 2, (float)(y + d / 2), letter, COLOR_TEXT);
}

// A padlock at (x, y), its body w wide: the profile asks for its PIN.
static void drawLock(int x, int y, int w)
{
    int body_h = w * 3 / 4;
    gfxFillCircle(x + w / 2, y + w / 2 + body_h / 3, w * 3 / 4, COLOR_SURFACE);
    gfxArc((float)(x + w / 2), (float)(y + w / 2), w * 0.32f, 3.0f, 0.0f, 6.28318f, COLOR_TEXT);  // The shackle:
    gfxFillRoundRect(x, y + w / 2, w, body_h, 3, COLOR_TEXT);  // its lower half under the body.
}

// --- The picker ---

static void drawTile(const PlexHomeUser *u, int x, int y, bool focused, bool current)
{
    int ax = x + (TILE_W - AVATAR_D) / 2, ay = y + 6;
    if (focused)
        gfxArc((float)(ax + AVATAR_D / 2), (float)(ay + AVATAR_D / 2), AVATAR_D / 2 + FOCUS_RING + 1.0f,
               (float)FOCUS_RING, 0.0f, 6.28318f, COLOR_ACCENT);
    drawAvatar(u, ax, ay, AVATAR_D);
    if (u->protected_pin)
        drawLock(ax + AVATAR_D - 22, ay + AVATAR_D - 28, 20);
    int name_w = fontTextWidth(focused ? FONT_BOLD : FONT_REGULAR, NAME_PX, u->title);
    if (name_w > TILE_W - 8)
        name_w = TILE_W - 8;
    fontDrawClipped(focused ? FONT_BOLD : FONT_REGULAR, NAME_PX, x + (TILE_W - name_w) / 2, ay + AVATAR_D + 10,
                    TILE_W - 8, u->title, COLOR_TEXT);
    if (current) {
        const char *in_use = T("In use");
        int w = fontTextWidth(FONT_REGULAR, 15, in_use);
        fontDraw(FONT_REGULAR, 15, x + (TILE_W - w) / 2, ay + AVATAR_D + 36, in_use, COLOR_ACCENT);
    }
}

void uiDrawProfiles(const PlexHomeUser *users, int count, int cursor, const char *current, const char *busy)
{
    int columns = count < UI_PROFILE_COLUMNS ? (count > 0 ? count : 1) : UI_PROFILE_COLUMNS;
    int rows = busy ? 1 : (count + UI_PROFILE_COLUMNS - 1) / UI_PROFILE_COLUMNS;
    int w = columns * TILE_W + 2 * PANEL_PAD, h = HEAD_H + rows * TILE_H + PANEL_PAD;
    if (w < 560)
        w = 560;
    int x = (GFX_WIDTH - w) / 2, y = (UI_FOOTER_Y - h) / 2;
    dimScreen();
    panel(x, y, w, h);
    fontDraw(FONT_BOLD, 28, x + PANEL_PAD, y + 26, T("Who's watching?"), COLOR_TEXT);
    fontDraw(FONT_REGULAR, 17, x + PANEL_PAD, y + 64, T("Plex Home"), COLOR_DIM);
    if (busy) {
        fontDrawMiddle(FONT_REGULAR, 21, x + PANEL_PAD, (float)(y + HEAD_H + TILE_H / 2), busy, COLOR_TEXT);
        return;
    }
    int grid_x = x + (w - columns * TILE_W) / 2;
    for (int i = 0; i < count; i++) {
        int tx = grid_x + (i % UI_PROFILE_COLUMNS) * TILE_W, ty = y + HEAD_H + (i / UI_PROFILE_COLUMNS) * TILE_H;
        drawTile(&users[i], tx, ty, i == cursor, !strcmp(users[i].uuid, current));
        uiLibraryAddHit(tx, ty, TILE_W, TILE_H, LIBHIT_PROFILE, i, 0);
    }
}

// --- The PIN pad ---

char uiPinKeyDigit(int key)
{
    if (key >= 0 && key < UI_PIN_KEY_DELETE)
        return (char)('1' + key);
    return key == UI_PIN_KEY_ZERO ? '0' : 0;
}

static void drawKey(int key, int x, int y, bool focused, bool checking)
{
    char digit[2] = {uiPinKeyDigit(key), '\0'};
    const char *label = digit[0] ? digit : key == UI_PIN_KEY_DELETE ? T("Delete") : T("Cancel");
    Color back = focused && !checking ? COLOR_ACCENT : COLOR_SURFACE_HI;
    gfxFillRoundRect(x, y, KEY_W, KEY_H, KEY_R, back);
    int px = digit[0] ? 34 : 19;
    int w = fontTextWidth(FONT_BOLD, px, label);
    fontDrawMiddle(FONT_BOLD, px, x + (KEY_W - w) / 2, (float)(y + KEY_H / 2), label,
                   checking ? COLOR_FAINT : COLOR_TEXT);
    uiLibraryAddHit(x, y, KEY_W, KEY_H, LIBHIT_PIN_KEY, key, 0);
}

void uiDrawPinPad(const PlexHomeUser *user, int digits, int cursor, const char *message, bool checking)
{
    int x = (GFX_WIDTH - PAD_W) / 2, y = (UI_FOOTER_Y - PAD_H) / 2;
    dimScreen();
    panel(x, y, PAD_W, PAD_H);
    drawAvatar(user, x + PANEL_PAD, y + 24, 64);
    fontDrawClipped(FONT_BOLD, 24, x + PANEL_PAD + 80, y + 28, PAD_W - 2 * PANEL_PAD - 80, user->title, COLOR_TEXT);
    fontDraw(FONT_REGULAR, 17, x + PANEL_PAD + 80, y + 62, checking ? T("Checking the PIN...") : T("Enter the PIN"),
             COLOR_DIM);
    int dots_w = (PLEX_HOME_PIN_DIGITS - 1) * DOT_GAP, dot_x = x + (PAD_W - dots_w) / 2, dot_y = y + 128;
    for (int i = 0; i < PLEX_HOME_PIN_DIGITS; i++) {
        if (i < digits)
            gfxFillCircle(dot_x + i * DOT_GAP, dot_y, DOT_R, COLOR_TEXT);
        else
            gfxArc((float)(dot_x + i * DOT_GAP), (float)dot_y, (float)DOT_R, 2.5f, 0.0f, 6.28318f, COLOR_DIM);
    }
    if (message[0]) {
        int w = fontTextWidth(FONT_REGULAR, 17, message);
        if (w > PAD_W - 2 * PANEL_PAD)
            w = PAD_W - 2 * PANEL_PAD;
        fontDrawClipped(FONT_REGULAR, 17, x + (PAD_W - w) / 2, dot_y + 22, PAD_W - 2 * PANEL_PAD, message, COLOR_ERROR);
    }
    int keys_w = UI_PIN_KEY_COLUMNS * KEY_W + (UI_PIN_KEY_COLUMNS - 1) * KEY_GAP;
    int kx = x + (PAD_W - keys_w) / 2, ky = y + 184;
    for (int k = 0; k < UI_PIN_KEYS; k++)
        drawKey(k, kx + (k % UI_PIN_KEY_COLUMNS) * (KEY_W + KEY_GAP), ky + (k / UI_PIN_KEY_COLUMNS) * (KEY_H + KEY_GAP),
                k == cursor, checking);
}
