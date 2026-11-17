#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "font.h"
#include "gfx.h"
#include "util.h"

#define MARGIN 32
#define BAR_H 120
#define TRACK_Y_OFFSET 60
#define ICON 42

#define COLOR_BG GFX_RGB(0x12, 0x12, 0x14)
#define COLOR_SURFACE_HI GFX_RGB(0x2C, 0x2C, 0x34)
#define COLOR_TEXT GFX_RGB(0xF2, 0xF2, 0xF5)
#define COLOR_DIM GFX_RGB(0xA4, 0xA4, 0xB0)
#define COLOR_FAINT GFX_RGB(0x5E, 0x5E, 0x6A)
#define COLOR_TRACK GFX_RGB(0x40, 0x40, 0x48)
#define COLOR_ACCENT GFX_RGB(0xFF, 0x6B, 0x9A)  // KaiPlayer's pink.
#define COLOR_ERROR GFX_RGB(0xFF, 0x8A, 0x5C)

size_t uiFirstVisible(size_t cursor, size_t count)
{
    if (count <= UI_ROWS || cursor < UI_ROWS / 2)
        return 0;
    size_t first = cursor - UI_ROWS / 2;
    return first + UI_ROWS > count ? count - UI_ROWS : first;
}

// --- The folder list ---

static void drawFolderIcon(int x, int y)
{
    gfxFillRoundRect(x + 4, y + 9, 16, 8, 3, COLOR_ACCENT);    // The tab.
    gfxFillRoundRect(x + 4, y + 13, ICON - 8, ICON - 22, 5, COLOR_ACCENT);
}

static void drawVideoIcon(int x, int y)
{
    gfxFillRoundRect(x + 3, y + 7, ICON - 6, ICON - 14, 7, COLOR_SURFACE_HI);
    float cx = x + ICON / 2.0f + 2, cy = y + ICON / 2.0f;
    gfxTriangle(cx - 7, cy - 9, cx - 7, cy + 9, cx + 9, cy, COLOR_ACCENT);
}

static void formatSize(int64_t bytes, char *out, size_t size)
{
    if (bytes < 0)
        out[0] = '\0';
    else if (bytes >= (int64_t)1 << 30)
        snprintf(out, size, "%.1f GB", bytes / 1073741824.0);
    else
        snprintf(out, size, "%.0f MB", bytes / 1048576.0);
}

void uiDrawBrowser(const Browser *b, const char *message)
{
    gfxClear(COLOR_BG);
    int title_w = fontDraw(FONT_BOLD, 26, MARGIN, 18, "KaiVideo", COLOR_ACCENT);
    // The folder, with an arrow back up when there's one above.
    int x = MARGIN + title_w + 28;
    bool can_go_up = strcmp(b->path, BROWSER_ROOT) != 0;
    if (can_go_up) {
        gfxLine((float)x + 10, 26, (float)x + 3, 33, 3, COLOR_TEXT);
        gfxLine((float)x + 3, 33, (float)x + 10, 40, 3, COLOR_TEXT);
        x += 22;
    }
    fontDrawClipped(FONT_REGULAR, 19, x, 22, GFX_WIDTH - MARGIN - x, b->path, can_go_up ? COLOR_TEXT : COLOR_DIM);

    if (b->count == 0) {
        fontDrawCentered(FONT_BOLD, 24, GFX_WIDTH / 2.0f, 300, "No videos or folders here.", COLOR_TEXT);
        fontDrawCentered(FONT_REGULAR, 19, GFX_WIDTH / 2.0f, 340,
                         "KaiVideo plays MP4, MKV and WebM files (H.264, VP9, AV1, VP8).", COLOR_DIM);
    }
    size_t first = uiFirstVisible(b->cursor, b->count);
    for (size_t i = first; i < b->count && i < first + UI_ROWS; i++) {
        const BrowserEntry *e = &b->items[i];
        int y = UI_LIST_Y + (int)(i - first) * UI_ROW_H;
        if (i == b->cursor) {
            gfxFillRoundRect(MARGIN - 12, y + 2, GFX_WIDTH - 2 * (MARGIN - 12), UI_ROW_H - 4, 10, COLOR_SURFACE_HI);
            gfxFillRect(MARGIN - 12, y + 14, 3, UI_ROW_H - 28, COLOR_ACCENT);
        }
        int iy = y + (UI_ROW_H - ICON) / 2;
        if (e->is_dir)
            drawFolderIcon(MARGIN, iy);
        else
            drawVideoIcon(MARGIN, iy);
        char size[24];
        formatSize(e->size, size, sizeof(size));
        int size_w = size[0] ? fontTextWidth(FONT_REGULAR, 17, size) : 0;
        int text_x = MARGIN + ICON + 18;
        int name_w = GFX_WIDTH - MARGIN - text_x - (size_w ? size_w + 24 : 0);
        fontDrawClipped(FONT_BOLD, 21, text_x, y + (UI_ROW_H - 30) / 2, name_w, e->name, COLOR_TEXT);
        if (size[0])
            fontDrawMiddle(FONT_REGULAR, 17, GFX_WIDTH - MARGIN - size_w, y + UI_ROW_H / 2.0f, size, COLOR_DIM);
    }
    if (b->count > UI_ROWS) {  // A scrollbar.
        int track_h = UI_ROWS * UI_ROW_H, thumb_h = track_h * UI_ROWS / (int)b->count;
        if (thumb_h < 24)
            thumb_h = 24;
        int thumb_y = UI_LIST_Y + (int)((long)(track_h - thumb_h) * (long)first / (long)(b->count - UI_ROWS));
        gfxFillRoundRect(GFX_WIDTH - 12, UI_LIST_Y, 4, track_h, 2, COLOR_TRACK);
        gfxFillRoundRect(GFX_WIDTH - 12, thumb_y, 4, thumb_h, 2, COLOR_FAINT);
    }
    const char *help = message && message[0] ? message : "A  open      B  folder above      +  exit";
    fontDrawClipped(FONT_REGULAR, 18, MARGIN, GFX_HEIGHT - 36, GFX_WIDTH - 2 * MARGIN, help,
                    message && message[0] ? COLOR_ERROR : COLOR_DIM);
}

TapTarget uiTapBrowser(const Browser *b, int x, int y, size_t *row)
{
    (void)x;
    if (y < UI_LIST_Y)
        return strcmp(b->path, BROWSER_ROOT) != 0 ? TAP_UP : TAP_NOTHING;
    size_t first = uiFirstVisible(b->cursor, b->count), index = first + (size_t)((y - UI_LIST_Y) / UI_ROW_H);
    if (y >= UI_LIST_Y + UI_ROWS * UI_ROW_H || index >= b->count)
        return TAP_NOTHING;
    *row = index;
    return TAP_ROW;
}

// --- The player's bar ---

void uiDrawPlayerBar(const Player *p, const char *name)
{
    // The close button, top left.
    gfxFillCircle(MARGIN + 14, 36, 22, GFX_RGB(0, 0, 0));
    gfxLine(MARGIN + 7, 29, MARGIN + 21, 43, 3, COLOR_TEXT);
    gfxLine(MARGIN + 7, 43, MARGIN + 21, 29, 3, COLOR_TEXT);

    int y = GFX_HEIGHT - BAR_H;
    // A fade from the picture into the bar, in bands (gradients are opaque).
    for (int i = 0; i < 12; i++)
        gfxBlendRect(0, y - 60 + i * 5, GFX_WIDTH, 5, GFX_RGB(0, 0, 0), (unsigned)(i * 200 / 12));
    gfxBlendRect(0, y, GFX_WIDTH, BAR_H, GFX_RGB(0, 0, 0), 200);
    if (playerPaused(p)) {
        gfxTriangle(MARGIN, y + 18, MARGIN, y + 44, MARGIN + 22, y + 31, COLOR_TEXT);  // Press to play.
    } else {
        gfxFillRoundRect(MARGIN, y + 18, 7, 26, 2, COLOR_TEXT);
        gfxFillRoundRect(MARGIN + 13, y + 18, 7, 26, 2, COLOR_TEXT);
    }
    fontDrawClipped(FONT_BOLD, 22, MARGIN + 40, y + 16, GFX_WIDTH - 2 * MARGIN - 260, name, COLOR_TEXT);
    char pos[16], len[16], time[40];
    double position = playerPosition(p) / 1e9, duration = playerDuration(p) / 1e9;
    utilFormatTime(pos, sizeof(pos), position);
    utilFormatTime(len, sizeof(len), duration);
    snprintf(time, sizeof(time), "%s / %s", pos, len);
    fontDraw(FONT_REGULAR, 20, GFX_WIDTH - MARGIN - fontTextWidth(FONT_REGULAR, 20, time), y + 18, time, COLOR_TEXT);
    int track_w = GFX_WIDTH - 2 * MARGIN, done = duration > 0 ? (int)(track_w * (position / duration)) : 0;
    if (done > track_w)
        done = track_w;
    gfxFillRoundRect(MARGIN, y + TRACK_Y_OFFSET, track_w, 6, 3, COLOR_TRACK);
    if (done > 0)
        gfxFillRoundRect(MARGIN, y + TRACK_Y_OFFSET, done, 6, 3, COLOR_ACCENT);
    gfxFillCircle(MARGIN + done, y + TRACK_Y_OFFSET + 3, 8, COLOR_ACCENT);
    char info[256];
    snprintf(info, sizeof(info), "%s  \xC2\xB7  %s      A pause   Left Right 10 s   L R 1 min   B back",
             playerVideoInfo(p), playerAudioInfo(p));
    fontDrawClipped(FONT_REGULAR, 16, MARGIN, y + 82, track_w, info, COLOR_DIM);
}

TapTarget uiTapPlayer(int x, int y, double *fraction)
{
    int bar_y = GFX_HEIGHT - BAR_H;
    if (x < MARGIN + 50 && y < 80)
        return TAP_CLOSE;
    if (y >= bar_y + TRACK_Y_OFFSET - 20 && y <= bar_y + TRACK_Y_OFFSET + 26) {
        double f = (double)(x - MARGIN) / (GFX_WIDTH - 2 * MARGIN);
        *fraction = f < 0 ? 0 : f > 1 ? 1 : f;
        return TAP_SEEK;
    }
    if (y >= bar_y && y < bar_y + TRACK_Y_OFFSET - 20 && x < MARGIN + 40)
        return TAP_PAUSE;
    return TAP_PICTURE;
}
