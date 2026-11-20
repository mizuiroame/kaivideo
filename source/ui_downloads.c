// The Downloads panel: the videos asked for, how far each has come, and what
// was watched offline waiting for its server.
#include "ui_downloads.h"

#include "font.h"
#include "gfx.h"
#include "ui_buttons.h"
#include "ui_library.h"
#include "ui_style.h"

#define PANEL_W 1040
#define PANEL_H 530
#define PANEL_PAD 28
#define HEADER_H 70
#define ROW_GAP 6
#define BAR_H 6
#define DELETE_W 56

// An arrow down onto a line, size across, centered at cx, cy.
static void drawArrow(float cx, float cy, float size, Color color)
{
    float h = size / 2;
    gfxLine(cx, cy - h, cx, cy + h * 0.45f, 2, color);
    gfxLine(cx - h * 0.55f, cy - h * 0.05f, cx, cy + h * 0.5f, 2, color);
    gfxLine(cx + h * 0.55f, cy - h * 0.05f, cx, cy + h * 0.5f, 2, color);
    gfxLine(cx - h * 0.7f, cy + h, cx + h * 0.7f, cy + h, 2, color);
}

// An SD card, upright, centered at cx, cy in a circle of radius r: its
// corner cut at the top right, and its contacts' lines at the top.
static void drawCard(float cx, float cy, float r, Color color)
{
    float w = r * 0.9f, h = r * 1.15f, x0 = cx - w / 2, y0 = cy - h / 2, cut = w * 0.32f;
    gfxFillRect((int)x0, (int)(y0 + cut), (int)w, (int)(h - cut), color);
    gfxFillRect((int)x0, (int)y0, (int)(w - cut), (int)cut + 1, color);
    gfxTriangle(x0 + w - cut, y0, x0 + w - cut, y0 + cut + 1, x0 + w, y0 + cut + 1, color);
    for (int i = 0; i < 3; i++)  // Contacts: gaps in the top.
        gfxFillRect((int)(x0 + w * (0.18f + 0.2f * i)), (int)(y0 + 1), 1 + (int)(r * 0.06f), (int)(h * 0.28f), COLOR_ACCENT);
}

#define TURN 6.2832f
#define TOP (-1.5708f)  // Rings start at the top.

void uiDrawDownloadMark(float cx, float cy, float r, UiDownloadMark mark, double fraction)
{
    if (mark == UI_DL_NONE)
        return;
    if (mark == UI_DL_DONE) {
        gfxFillCircle((int)cx, (int)cy, (int)r, COLOR_ACCENT);
        drawCard(cx, cy, r, COLOR_BG);
        return;
    }
    Color ring = mark == UI_DL_FAILED ? COLOR_ERROR : COLOR_FAINT;
    gfxArc(cx, cy, r - 1.5f, 2.5f, 0, TURN, ring);
    float done = (float)(fraction < 0 ? 0 : fraction > 1 ? 1 : fraction);
    if (done > 0 && mark != UI_DL_FAILED)
        gfxArc(cx, cy, r - 1.5f, 2.5f, TOP, TOP + TURN * done, mark == UI_DL_ACTIVE ? COLOR_ACCENT : COLOR_DIM);
    if (mark == UI_DL_PAUSED) {
        gfxFillRect((int)(cx - r * 0.35f), (int)(cy - r * 0.4f), 3, (int)(r * 0.8f), COLOR_DIM);
        gfxFillRect((int)(cx + r * 0.35f) - 3, (int)(cy - r * 0.4f), 3, (int)(r * 0.8f), COLOR_DIM);
    } else {
        drawArrow(cx, cy, r * 0.95f,
                  mark == UI_DL_FAILED ? COLOR_ERROR : mark == UI_DL_ACTIVE ? COLOR_ACCENT : COLOR_FAINT);
    }
}

#define PILL_H 32
#define PILL_ICON_R 10

int uiDrawDownloadsPill(int right, const char *label, double fraction)
{
    int w = 2 * PILL_ICON_R + 10 + fontTextWidth(FONT_BOLD, 16, label) + 24;
    int x = right - w, y = 15;
    gfxFillRoundRect(x, y, w, PILL_H, PILL_H / 2, COLOR_SURFACE_HI);
    uiDrawDownloadMark(x + 12.0f + PILL_ICON_R, y + PILL_H / 2.0f, PILL_ICON_R, UI_DL_ACTIVE, fraction);
    fontDrawMiddle(FONT_BOLD, 16, x + 12 + 2 * PILL_ICON_R + 8, y + PILL_H / 2.0f, label, COLOR_TEXT);
    uiLibraryAddHit(x - 4, 4, w + 8, UI_TAB_BAR_H - 8, LIBHIT_DOWNLOADS, 0, 0);
    return x;
}

static void drawRow(const UiDownloadRow *r, int x, int y, int w, bool selected, bool confirm, int index)
{
    int h = UI_DOWNLOAD_ROW_H - ROW_GAP;
    gfxFillRoundRect(x, y, w, h, 12, selected ? COLOR_SURFACE_HI : COLOR_SURFACE);
    if (selected)
        gfxFillRoundRect(x, y + 12, 4, h - 24, 2, COLOR_ACCENT);
    uiLibraryAddHit(x, y, w - DELETE_W, h, LIBHIT_DOWNLOAD_ROW, index, 0);
    int tx = x + 20, tw = w - DELETE_W - 40;
    int state_w = fontTextWidth(FONT_REGULAR, 17, r->state);
    if (state_w > tw / 2)
        state_w = tw / 2;
    fontDrawClipped(FONT_BOLD, 20, tx, y + 12, tw - state_w - 16, r->title, COLOR_TEXT);
    fontDrawClipped(FONT_REGULAR, 16, tx, y + 40, tw - state_w - 16, r->detail, COLOR_DIM);
    Color state_color = r->failed ? COLOR_ERROR : r->done ? COLOR_ACCENT : COLOR_DIM;
    fontDrawClipped(FONT_REGULAR, 17, tx + tw - state_w, y + 14, state_w, r->state, state_color);
    if (r->progress >= 0 && !r->done) {
        int bw = state_w > 120 ? state_w : 120, bx = tx + tw - bw;
        int done = (int)(bw * (r->progress > 1 ? 1 : r->progress));
        gfxFillRoundRect(bx, y + 46, bw, BAR_H, BAR_H / 2, COLOR_TRACK);
        gfxFillRoundRect(bx, y + 46, done > BAR_H ? done : BAR_H, BAR_H, BAR_H / 2, COLOR_ACCENT);
    }
    // Delete: a cross, red when a second press would delete it.
    float cx = x + w - DELETE_W / 2.0f, cy = y + h / 2.0f;
    gfxFillCircle((int)cx, (int)cy, 17, confirm ? COLOR_ERROR : COLOR_BG);
    gfxLine(cx - 6, cy - 6, cx + 6, cy + 6, 2, COLOR_TEXT);
    gfxLine(cx - 6, cy + 6, cx + 6, cy - 6, 2, COLOR_TEXT);
    uiLibraryAddHit(x + w - DELETE_W, y, DELETE_W, h, LIBHIT_DOWNLOAD_DELETE, index, 0);
}

void uiDrawDownloads(const UiDownloadRow *rows, int count, const Scroll *s, int confirm, const char *summary,
                     const char *empty)
{
    int x = (GFX_WIDTH - PANEL_W) / 2, y = (UI_FOOTER_Y + UI_TAB_BAR_H - PANEL_H) / 2;
    gfxBlendRect(0, 0, GFX_WIDTH, GFX_HEIGHT, GFX_RGB(0, 0, 0), UI_DIM_ALPHA);
    uiLibraryAddHit(0, 0, GFX_WIDTH, GFX_HEIGHT, LIBHIT_CLOSE, 0, 0);
    gfxFillRoundRect(x, y, PANEL_W, PANEL_H, 16, COLOR_BG);
    uiLibraryAddHit(x, y, PANEL_W, PANEL_H, LIBHIT_PANEL, 0, 0);
    fontDraw(FONT_BOLD, 28, x + PANEL_PAD, y + PANEL_PAD - 4, "Downloads", COLOR_TEXT);
    fontDrawClipped(FONT_REGULAR, 17, x + PANEL_PAD + 180, y + PANEL_PAD + 4, PANEL_W - 2 * PANEL_PAD - 180, summary,
                    COLOR_DIM);
    int lx = x + PANEL_PAD, ly = y + HEADER_H, lw = PANEL_W - 2 * PANEL_PAD;
    int list_h = UI_DOWNLOAD_ROWS * UI_DOWNLOAD_ROW_H;
    if (count == 0) {
        fontDrawCentered(FONT_REGULAR, 19, x + PANEL_W / 2.0f, ly + list_h / 2.0f, empty, COLOR_DIM);
        return;
    }
    gfxSetClip(lx, ly, lw, list_h);
    uiLibraryClipHits(ly, ly + list_h);
    for (int i = s->first; i < count && i <= s->first + UI_DOWNLOAD_ROWS; i++) {
        int ry = ly + (i - s->first) * UI_DOWNLOAD_ROW_H - s->offset;
        drawRow(&rows[i], lx, ry, lw, i == s->cursor, i == confirm, i);
    }
    uiLibraryClipHits(0, GFX_HEIGHT);
    gfxResetClip();
}
