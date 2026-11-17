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
#define CC_W 56                 // The subtitles button, left of the time.
#define CC_H 30
#define CC_X (GFX_WIDTH - MARGIN - 200 - CC_W)
#define AUDIO_W 76              // The sound track button, left of CC.
#define AUDIO_X (CC_X - 12 - AUDIO_W)
#define SUB_PX 34
#define SUB_LINE_H 44
#define SUB_MAX_W (GFX_WIDTH - 160)
#define SUB_MAX_LINES 6

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

void uiDrawBrowser(const Browser *b, const ResumeList *resume, const char *message)
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
        // Started before: how far, as a thin bar under the name.
        char path[BROWSER_PATH_MAX];
        int64_t at, len;
        if (!e->is_dir && resume && browserEntryPath(b, i, path, sizeof(path)) &&
            resumeFind(resume, path, &at, &len) && len > 0) {
            int bar_w = name_w < 220 ? name_w : 220, done = (int)(bar_w * ((double)at / len));
            gfxFillRoundRect(text_x, y + UI_ROW_H - 12, bar_w, 4, 2, COLOR_TRACK);
            gfxFillRoundRect(text_x, y + UI_ROW_H - 12, done > 4 ? done : 4, 4, 2, COLOR_ACCENT);
        }
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

void uiDrawPlayerBar(const Player *p, const char *name, const char *note)
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
    if (note && note[0])
        fontDrawClipped(FONT_BOLD, 18, MARGIN, y - 34, GFX_WIDTH - 2 * MARGIN, note, COLOR_ACCENT);
    if (playerPaused(p)) {
        gfxTriangle(MARGIN, y + 18, MARGIN, y + 44, MARGIN + 22, y + 31, COLOR_TEXT);  // Press to play.
    } else {
        gfxFillRoundRect(MARGIN, y + 18, 7, 26, 2, COLOR_TEXT);
        gfxFillRoundRect(MARGIN + 13, y + 18, 7, 26, 2, COLOR_TEXT);
    }
    fontDrawClipped(FONT_BOLD, 22, MARGIN + 40, y + 16, AUDIO_X - MARGIN - 60, name, COLOR_TEXT);
    if (playerAudioTrackCount(p) > 1) {
        gfxFillRoundRect(AUDIO_X, y + 14, AUDIO_W, CC_H, 6, COLOR_SURFACE_HI);
        fontDrawCentered(FONT_BOLD, 18, AUDIO_X + AUDIO_W / 2.0f, y + 14 + CC_H / 2.0f, "Audio", COLOR_TEXT);
    }
    if (playerSubtitleCount(p) > 0) {  // CC: filled when subtitles show.
        bool on = playerSubtitle(p) >= 0;
        gfxFillRoundRect(CC_X, y + 14, CC_W, CC_H, 6, on ? COLOR_TEXT : COLOR_SURFACE_HI);
        fontDrawCentered(FONT_BOLD, 18, CC_X + CC_W / 2.0f, y + 14 + CC_H / 2.0f, "CC",
                         on ? GFX_RGB(0, 0, 0) : COLOR_DIM);
    }
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
    snprintf(info, sizeof(info), "%s  \xC2\xB7  %s      A pause   Left Right 10 s   L R 1 min   ZL start   X sound   Y subtitles   B back",
             playerVideoInfo(p), playerAudioInfo(p));
    fontDrawClipped(FONT_REGULAR, 16, MARGIN, y + 82, track_w, info, COLOR_DIM);
}

TapTarget uiTapPlayer(int x, int y, bool has_subtitles, bool has_tracks, double *fraction)
{
    int bar_y = GFX_HEIGHT - BAR_H;
    if (has_tracks && x >= AUDIO_X - 6 && x < AUDIO_X + AUDIO_W + 6 && y >= bar_y + 4 && y < bar_y + 14 + CC_H + 10)
        return TAP_AUDIO;
    if (has_subtitles && x >= CC_X - 6 && x < CC_X + CC_W + 10 && y >= bar_y + 4 && y < bar_y + 14 + CC_H + 10)
        return TAP_SUBTITLES;
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

// --- Subtitles ---

// Splits text into the lines drawn: its own lines, each wrapped at spaces to
// SUB_MAX_W (a word wider than that stays whole). Returns how many.
static int wrapSubtitle(const char *text, char lines[][256], int max)
{
    int count = 0;
    for (const char *p = text; *p && count < max;) {
        size_t len = strcspn(p, "\n");
        char line[256];
        snprintf(line, sizeof(line), "%.*s", (int)(len < sizeof(line) ? len : sizeof(line) - 1), p);
        // Fill each drawn line with as many words as fit.
        char *rest = line;
        while (*rest && count < max) {
            char *cut = NULL;
            for (char *space = strchr(rest, ' '); space; space = strchr(space + 1, ' ')) {
                *space = '\0';
                bool fits = fontTextWidth(FONT_BOLD, SUB_PX, rest) <= SUB_MAX_W;
                *space = ' ';
                if (!fits)
                    break;
                cut = space;
            }
            if (fontTextWidth(FONT_BOLD, SUB_PX, rest) <= SUB_MAX_W || !cut)
                cut = NULL;
            if (cut)
                *cut = '\0';
            snprintf(lines[count++], 256, "%s", rest);
            if (!cut)
                break;
            rest = cut + 1;
        }
        p += len;
        if (*p == '\n')
            p++;
    }
    return count;
}

void uiDrawSubtitles(const char *text, bool bar_shown)
{
    char lines[SUB_MAX_LINES][256];
    int count = wrapSubtitle(text, lines, SUB_MAX_LINES);
    int bottom = bar_shown ? GFX_HEIGHT - BAR_H - 70 : GFX_HEIGHT - 40;
    int top = bottom - count * SUB_LINE_H;
    static const int OUTLINE[8][2] = {{-2, -2}, {0, -2}, {2, -2}, {-2, 0}, {2, 0}, {-2, 2}, {0, 2}, {2, 2}};
    for (int i = 0; i < count; i++) {
        float cx = GFX_WIDTH / 2.0f, cy = top + i * SUB_LINE_H + SUB_LINE_H / 2.0f;
        for (int o = 0; o < 8; o++)
            fontDrawCentered(FONT_BOLD, SUB_PX, cx + OUTLINE[o][0], cy + OUTLINE[o][1], lines[i], GFX_RGB(0, 0, 0));
        fontDrawCentered(FONT_BOLD, SUB_PX, cx, cy, lines[i], COLOR_TEXT);
    }
}
