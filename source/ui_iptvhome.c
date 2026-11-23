#include "ui_iptvhome.h"

#include <stdio.h>

#include "font.h"
#include "gfx.h"
#include "lang.h"
#include "thumbs.h"
#include "ui_buttons.h"
#include "ui_library.h"
#include "ui_library_shared.h"
#include "ui_style.h"

#define HOME_Y 76
#define TITLE_H 34
#define CARD_W 206
#define CARD_H 116
#define CARD_RADIUS 10
#define CLIP_ABOVE 8      // Room above the first row for a focused card's ring.
#define PILL_H 30

static const char *rowTitle(const IptvHomeRow *row, const IptvList *list)
{
    switch (row->kind) {
    case IPTVROW_RECENT: return T("Recently watched");
    case IPTVROW_FAVORITES: return T("Favorites");
    default: return row->group >= 0 && row->group < list->group_count ? list->groups[row->group].name
                                                                      : T("All channels");
    }
}

static void drawCentered(const char *title, const char *line)
{
    int cy = (UI_TAB_BAR_H + UI_FOOTER_Y) / 2;
    fontDrawCentered(FONT_BOLD, 26, GFX_WIDTH / 2.0f, cy - 18.0f, title, COLOR_TEXT);
    fontDrawCentered(FONT_REGULAR, 19, GFX_WIDTH / 2.0f, cy + 20.0f, line, COLOR_DIM);
}

// A channel's card: its logo (on the cards' dark color), or its name on a colored tile without one.
static void drawChannelCard(const IptvChannel *c, const char *on_now, int x, int y, bool focused)
{
    if (focused)
        gfxFillRoundRect(x - 4, y - 4, CARD_W + 8, CARD_H + 8, CARD_RADIUS + 4, COLOR_TEXT);
    const uint32_t *logo = c->logo[0] ? thumbGet(c->logo, 0) : NULL;
    bool no_logo = !c->logo[0] || thumbIsNone(c->logo, 0);
    if (logo) {
        uiLibDrawPicture(logo, x, y, CARD_W, CARD_H, CARD_RADIUS);
    } else if (no_logo) {
        Color top, bottom;
        uiLibCardColors(c->name, &top, &bottom);
        gfxSetClipRound(x, y, CARD_W, CARD_H, CARD_RADIUS);
        gfxGradientV(x, y, CARD_W, CARD_H, top, bottom);
        gfxResetClip();
        uiLibDrawWrapped(FONT_BOLD, 20, x + 14, y + 14, CARD_W - 28, 25, 4, c->name, COLOR_TEXT);
    } else {
        gfxFillRoundRect(x, y, CARD_W, CARD_H, CARD_RADIUS, COLOR_SURFACE);  // Its logo is on its way.
    }
    fontDrawClipped(FONT_BOLD, 17, x + 2, y + CARD_H + 8, CARD_W - 4, c->name, focused ? COLOR_TEXT : COLOR_DIM);
    if (on_now && on_now[0])
        fontDrawClipped(FONT_REGULAR, 14, x + 2, y + CARD_H + 30, CARD_W - 4, on_now, COLOR_FAINT);
}

// R: the channel the cursor is on in the favorites, or out of them. At the right of its row's title.
static void drawFavoritePill(int y, bool favorite)
{
    const char *label = favorite ? T("In Favorites") : T("Add to Favorites");
    int w = uiButtonWidth("R") + fontTextWidth(FONT_BOLD, 16, label) + 32;
    int x = GFX_WIDTH - MARGIN - w;
    gfxFillRoundRect(x, y, w, PILL_H, PILL_H / 2, favorite ? COLOR_ACCENT : COLOR_SURFACE_HI);
    uiDrawButton(x + 10, y + PILL_H / 2.0f, "R", COLOR_SURFACE, COLOR_TEXT);
    fontDrawMiddle(FONT_BOLD, 16, x + 20 + uiButtonWidth("R"), y + PILL_H / 2.0f, label,
                   favorite ? COLOR_BG : COLOR_TEXT);
    uiLibraryAddHit(x, y - 4, w, PILL_H + 8, LIBHIT_FAVORITE, 0, 0);
}

static void drawRow(const IptvHome *h, const IptvHomeView *v, const IptvList *list, const IptvMarks *marks,
                    UiIptvNow now, const void *ctx, int r, int y)
{
    bool row_focused = r == v->rows.cursor;
    fontDraw(FONT_BOLD, 22, MARGIN, y, rowTitle(&h->rows[r], list), row_focused ? COLOR_TEXT : COLOR_DIM);
    const Scroll *cols = &v->cols[r];
    for (int c = cols->first; c < h->rows[r].count && c <= cols->first + UI_IPTVHOME_COLS + 1; c++) {
        int x = MARGIN + (c - cols->first) * UI_IPTVHOME_COL_PX - cols->offset;
        if (x >= GFX_WIDTH)
            break;
        int channel = iptvHomeChannel(h, r, c);
        bool focused = row_focused && c == cols->cursor;
        drawChannelCard(&list->channels[channel], now ? now(ctx, channel) : NULL, x, y + TITLE_H, focused);
        int left = x < 0 ? 0 : x, right = x + CARD_W > GFX_WIDTH ? GFX_WIDTH : x + CARD_W;
        if (right > left)
            uiLibraryAddHit(left, y + TITLE_H, right - left, CARD_H + 46, LIBHIT_HOME_CARD, r, c);
        if (focused)
            drawFavoritePill(y - 4, iptvMarksIsFavorite(marks, list->channels[channel].name));
    }
}

void uiDrawIptvHome(const IptvHome *h, const IptvHomeView *v, const IptvList *list, const IptvMarks *marks,
                    UiIptvNow now, const void *ctx, bool loading)
{
    if (h->count == 0) {
        if (loading)
            drawCentered(T("Reading the channels..."), T("They come in a moment."));
        else
            drawCentered(T("No channels"), T("The IPTV list has none KaiVideo can play (Settings > IPTV)."));
        return;
    }
    int top = HOME_Y - CLIP_ABOVE, bottom = HOME_Y + UI_IPTVHOME_ROWS * UI_IPTVHOME_ROW_PX;
    gfxSetClip(0, top, GFX_WIDTH, bottom - top);
    uiLibraryClipHits(top, bottom);
    // A row more than fit when a finger left them between rows.
    for (int r = v->rows.first; r < h->count && r <= v->rows.first + UI_IPTVHOME_ROWS; r++) {
        int y = HOME_Y + (r - v->rows.first) * UI_IPTVHOME_ROW_PX - v->rows.offset;
        if (y >= bottom)
            break;
        drawRow(h, v, list, marks, now, ctx, r, y);
    }
    uiLibraryClipHits(0, GFX_HEIGHT);
    gfxResetClip();
}
