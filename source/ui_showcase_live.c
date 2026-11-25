// The Showcase's large picture for what has no wide picture (a channel's
// logo, an Xtream poster): a blur of its colors with it shown whole; and a
// channel's text beside it, with what's on now and next.
#include <stdio.h>
#include <string.h>

#include "font.h"
#include "lang.h"
#include "livetv.h"
#include "ui_library_shared.h"
#include "ui_showcase_parts.h"
#include "ui_style.h"

#define BLUR_STRIP 8        // The blur is drawn in strips this wide,
#define PLATE_R 14          // the picture with these corners,
#define PLATE_SHADOW 90     // over a shadow this dark.
#define NAME_PX 30          // A picture-less plate's name.
#define NOW_PX 21
#define NOW_BAR_W 180
#define NEXT_PX 16
#define LIVE_DOT_R 4
#define NEXT_W 440          // What's on next, under the logo: at most this wide,
#define NEXT_DY 16          // this far under it.

// Smoother than straight lines between the grid's cells.
static unsigned easeK(float t)
{
    return (unsigned)(255 * uiShowcaseSmooth(t));
}

void uiShowcaseDrawBlur(const ArtColors *colors)
{
    for (int x = 0; x < HERO_W; x += BLUR_STRIP) {
        float u = (x + BLUR_STRIP / 2.0f) * (ART_GRID_W - 1) / HERO_W;
        int i = (int)u > ART_GRID_W - 2 ? ART_GRID_W - 2 : (int)u;
        unsigned k = easeK(u - (float)i);
        int w = x + BLUR_STRIP > HERO_W ? HERO_W - x : BLUR_STRIP;
        for (int b = 0; b < ART_GRID_H - 1; b++) {
            int y0 = b * HERO_H / (ART_GRID_H - 1), y1 = (b + 1) * HERO_H / (ART_GRID_H - 1);
            Color top = artMix(colors->cells[b][i], colors->cells[b][i + 1], k);
            Color bottom = artMix(colors->cells[b + 1][i], colors->cells[b + 1][i + 1], k);
            gfxGradientV(HERO_X + x, y0, w, y1 - y0, top, bottom);
        }
    }
}

void uiShowcaseDrawPlate(const uint32_t *pixels, int w, int h, bool upright, const char *name, unsigned alpha)
{
    if (alpha == 0)
        return;
    int pw = upright ? PLATE_POSTER_W : PLATE_WIDE_W, ph = upright ? pw * 3 / 2 : pw * 9 / 16;
    int x = PLATE_CX - pw / 2, y = upright ? PLATE_POSTER_Y : PLATE_WIDE_Y;
    gfxBlendRect(x + 8, y + 14, pw, ph, GFX_RGB(0, 0, 0), alpha * PLATE_SHADOW / 255 / 2);
    gfxBlendRect(x + 3, y + 6, pw, ph, GFX_RGB(0, 0, 0), alpha * PLATE_SHADOW / 255);
    if (pixels) {
        gfxSetImageAlpha(alpha);
        uiLibDrawImage(pixels, w, h, x, y, pw, ph, PLATE_R);
        gfxSetImageAlpha(255);
        return;
    }
    // No picture: the name on its colors, as the channel's card has it.
    Color top, bottom;
    uiLibCardColors(name, &top, &bottom);
    gfxSetClipRound(x, y, pw, ph, PLATE_R);
    if (alpha == 255)
        gfxGradientV(x, y, pw, ph, top, bottom);
    else
        gfxBlendRect(x, y, pw, ph, artMix(top, bottom, 128), alpha);
    gfxResetClip();
    if (alpha <= 160)
        return;
    int name_w = fontTextWidth(FONT_BOLD, NAME_PX, name);
    if (name_w <= pw - 48)  // A title card: in the middle.
        fontDrawMiddle(FONT_BOLD, NAME_PX, x + (pw - name_w) / 2, y + ph / 2.0f, name, COLOR_TEXT);
    else
        uiLibDrawWrapped(FONT_BOLD, NAME_PX, x + 24, y + 22, pw - 48, NAME_PX + 6, upright ? 6 : 3, name, COLOR_TEXT);
}

// "20:00 – 21:00" (or with AM and PM, as the clock is set).
static void timeRange(const UiShowcaseChannels *ch, const UiProgram *p, char *out, size_t size)
{
    char from[24], to[24];
    liveClock(p->start, ch->utc_offset_s, ch->clock_24h, from, sizeof(from));
    liveClock(p->end, ch->utc_offset_s, ch->clock_24h, to, sizeof(to));
    snprintf(out, size, "%s \xE2\x80\x93 %s", from, to);
}

// What's on: its title, how far into it (a bar), its times and what's left. Returns the y under it.
static int drawNow(const UiShowcaseChannels *ch, const UiProgram *now, int y)
{
    int tag_w = fontDraw(FONT_BOLD, 14, MARGIN, y + 5, T("ON NOW"), COLOR_ACCENT);
    fontDrawClipped(FONT_BOLD, NOW_PX, MARGIN + tag_w + 12, y, TEXT_W - tag_w - 12, now->title, COLOR_TEXT);
    y += 32;
    double done = now->end > now->start ? (double)(ch->now_s - now->start) / (double)(now->end - now->start) : 0;
    done = done < 0 ? 0 : done > 1 ? 1 : done;
    int fill = (int)(NOW_BAR_W * done);
    gfxFillRoundRect(MARGIN, y + 7, NOW_BAR_W, 4, 2, COLOR_TRACK);
    gfxFillRoundRect(MARGIN, y + 7, fill > 4 ? fill : 4, 4, 2, COLOR_ACCENT);
    char line[96], left[48];
    timeRange(ch, now, line, sizeof(line));
    int minutes = (int)((now->end - ch->now_s + 59) / 60);
    if (minutes > 0) {
        snprintf(left, sizeof(left), T("%d min left"), minutes);
        uiShowcaseAddPart(line, sizeof(line), left);
    }
    fontDrawClipped(FONT_REGULAR, 16, MARGIN + NOW_BAR_W + 14, y, TEXT_W - NOW_BAR_W - 14, line, COLOR_DIM);
    return y + 30;
}

// What's on next, under the logo: "NEXT  21:00  Title", in the middle of it.
static void drawNext(const UiShowcaseChannels *ch, const UiProgram *next)
{
    char at[24], line[160];
    liveClock(next->start, ch->utc_offset_s, ch->clock_24h, at, sizeof(at));
    snprintf(line, sizeof(line), "%s  %s", at, next->title);
    int tag_w = fontTextWidth(FONT_BOLD, 13, T("NEXT")), gap = 12;
    int line_w = fontTextWidth(FONT_REGULAR, NEXT_PX, line), max = NEXT_W - tag_w - gap;
    line_w = line_w < max ? line_w : max;
    int x = PLATE_CX - (tag_w + gap + line_w) / 2, y = PLATE_WIDE_Y + PLATE_WIDE_W * 9 / 16 + NEXT_DY;
    fontDraw(FONT_BOLD, 13, x, y + 2, T("NEXT"), COLOR_DIM);
    fontDrawClipped(FONT_REGULAR, NEXT_PX, x + tag_w + gap, y, line_w, line, SUMMARY_COLOR);
}

void uiShowcaseDrawChannelText(const UiShowcaseChannels *ch, int channel)
{
    const IptvChannel *c = &ch->list->channels[channel];
    char kicker[200];
    snprintf(kicker, sizeof(kicker), "%s", T("LIVE"));
    if (c->group >= 0 && c->group < ch->list->group_count)
        uiShowcaseAddPart(kicker, sizeof(kicker), ch->list->groups[c->group].name);
    if (c->number[0]) {
        char number[IPTV_NUMBER_MAX + 2];
        snprintf(number, sizeof(number), "#%s", c->number);
        uiShowcaseAddPart(kicker, sizeof(kicker), number);
    }
    int y = uiShowcaseDrawTitle(kicker, c->name);
    int kicker_w = fontTextWidth(FONT_BOLD, 15, kicker);  // On the air: a red dot after it.
    if (MARGIN + 32 + kicker_w + 14 < MARGIN + TEXT_W)
        gfxFillCircle(MARGIN + 32 + kicker_w + 12, KICKER_Y + 10, LIVE_DOT_R, GFX_RGB(0xE5, 0x48, 0x4D));
    UiProgram now = {0}, next = {0};
    if (ch->programs)
        ch->programs(ch->ctx, channel, &now, &next);
    bool has_now = now.title && now.title[0], has_next = next.title && next.title[0];
    int lines = 0, bottom = BUTTON_Y - BUTTON_GAP + 6;
    if (has_now) {
        y = drawNow(ch, &now, y);
        int room = (bottom - y) / SUMMARY_LINE;
        if (now.desc && now.desc[0] && room > 0) {
            lines = uiShowcaseSummaryLines(now.desc, room < SUMMARY_LINES ? room : SUMMARY_LINES);
            uiLibDrawWrapped(FONT_REGULAR, SUMMARY_PX, MARGIN, y, SUMMARY_W, SUMMARY_LINE, lines, now.desc,
                             SUMMARY_COLOR);
            y += lines * SUMMARY_LINE + 4;
        }
    } else {
        fontDrawClipped(FONT_REGULAR, INFO_PX, MARGIN, y, TEXT_W, T("The guide has nothing on now for it."), COLOR_DIM);
        y += 34;
    }
    if (has_next && uiLibThumbnailsOn())
        drawNext(ch, &next);
    int button_y = y + BUTTON_GAP - 6 < BUTTON_Y ? y + BUTTON_GAP - 6 : BUTTON_Y;
    int w = uiShowcaseDrawPill(MARGIN, button_y, "A", T("Watch live"), true, LIBHIT_HERO);
    bool favorite = ch->favorite && ch->favorite(ch->ctx, channel);
    uiShowcaseDrawPill(MARGIN + w + 14, button_y, "R", favorite ? T("In Favorites") : T("Add to Favorites"), false,
                       LIBHIT_FAVORITE);
}
