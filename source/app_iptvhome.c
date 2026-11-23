// The IPTV view's Home: its rows of channels (iptvhome.h), the channels
// watched last and the favorites kept in IPTVHOME_FILE, and its buttons.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <switch.h>

#include "app.h"
#include "approot.h"
#include "buttons.h"
#include "lang.h"
#include "ui_iptvhome.h"
#include "ui_library.h"

#define MARKS_TEXT_MAX ((IPTVHOME_RECENT_MAX + IPTVHOME_FAVORITES_MAX) * (IPTV_NAME_MAX + 12) + 1)

static void marksPath(char *out, size_t size)
{
    appConfigPath(out, size, IPTVHOME_FILE);
}

void appIptvHomeStart(App *app)
{
    iptvMarksInit(&app->iptv_marks);
    char path[APPROOT_PATH_MAX + 32];
    marksPath(path, sizeof(path));
    FILE *f = fopen(path, "rb");
    char *text = f ? malloc(MARKS_TEXT_MAX) : NULL;
    if (text) {
        size_t n = fread(text, 1, MARKS_TEXT_MAX - 1, f);
        text[n] = '\0';
        iptvMarksRead(&app->iptv_marks, text);
    }
    free(text);
    if (f)
        fclose(f);
}

static void saveMarks(App *app, uint64_t now)
{
    char path[APPROOT_PATH_MAX + 32];
    marksPath(path, sizeof(path));
    char *text = malloc(MARKS_TEXT_MAX);
    bool ok = text && iptvMarksWrite(&app->iptv_marks, text, MARKS_TEXT_MAX);
    FILE *f = ok ? fopen(path, "wb") : NULL;
    ok = f && fputs(text, f) >= 0;
    if (f)
        ok = fclose(f) == 0 && ok;
    free(text);
    if (!ok) {
        appLog("IPTV: %s couldn't be written", IPTVHOME_FILE);
        appNotify(app, NOTICE_ERROR, T("It couldn't be written to the card."), now);
    }
}

void appIptvHomeExit(App *app)
{
    iptvHomeFree(&app->iptv_home);
}

static int specialRows(const IptvHome *h)
{
    int n = 0;
    while (n < h->count && h->rows[n].kind != IPTVROW_GROUP)
        n++;
    return n;
}

void appIptvHomeRebuild(App *app)
{
    IptvHome *h = &app->iptv_home;
    IptvHomeView *v = &app->iptv_home_view;
    int before = specialRows(h), row = v->rows.cursor;
    bool on_group = row >= before && row < h->count;
    iptvHomeBuild(h, &app->iptv, &app->iptv_marks);
    // Recently watched and Favorites come and go above the groups: the groups keep their places.
    int after = specialRows(h), shift = after - before;
    Scroll groups[IPTVHOME_ROWS_MAX];
    memcpy(groups, v->cols, sizeof(groups));
    memset(v->cols, 0, sizeof(v->cols));
    for (int r = before; r < IPTVHOME_ROWS_MAX; r++)
        if (r + shift >= after && r + shift < IPTVHOME_ROWS_MAX)
            v->cols[r + shift] = groups[r];
    if (on_group)
        v->rows.cursor = row + shift;
    else if (row >= after)
        v->rows.cursor = after > 0 ? after - 1 : 0;
    v->rows.offset = 0;
    iptvHomeMove(v, h, 0, 0, UI_IPTVHOME_ROWS, UI_IPTVHOME_COLS);
}

void appIptvHomeReset(App *app)
{
    iptvHomeViewReset(&app->iptv_home_view);
}

void appIptvHomeWatched(App *app, const char *location)
{
    int index = iptvLocationIndex(location);
    if (index < 0 || index >= app->iptv.count)
        return;
    iptvMarksWatched(&app->iptv_marks, app->iptv.channels[index].name);
    saveMarks(app, armTicksToNs(armGetSystemTick()));
    appIptvHomeRebuild(app);
}

bool appIptvIsFavorite(const App *app, int channel)
{
    return channel >= 0 && channel < app->iptv.count &&
           iptvMarksIsFavorite(&app->iptv_marks, app->iptv.channels[channel].name);
}

void appIptvToggleFavorite(App *app, int channel, uint64_t now)
{
    if (channel < 0 || channel >= app->iptv.count)
        return;
    const char *name = app->iptv.channels[channel].name;
    bool was = iptvMarksIsFavorite(&app->iptv_marks, name);
    bool is = iptvMarksToggleFavorite(&app->iptv_marks, name);
    if (!was && !is) {
        appNotify(app, NOTICE_ERROR, T("There are too many favorites."), now);
        return;
    }
    saveMarks(app, now);
    appIptvHomeRebuild(app);
    appNotify(app, NOTICE_DONE, is ? T("Added to Favorites.") : T("Removed from Favorites."), now);
}

static int selected(const App *app)
{
    const IptvHomeView *v = &app->iptv_home_view;
    return iptvHomeChannel(&app->iptv_home, v->rows.cursor, v->cols[v->rows.cursor].cursor);
}

void appIptvHomeInput(App *app, uint64_t pressed, TouchEvent touch, uint64_t now)
{
    IptvHomeView *v = &app->iptv_home_view;
    int dx = (pressed & BUTTONS_LEFT) ? -1 : (pressed & BUTTONS_RIGHT) ? 1 : 0;
    int dy = (pressed & BUTTONS_UP) ? -1 : (pressed & BUTTONS_DOWN) ? 1 : 0;
    if (dx || dy)
        iptvHomeMove(v, &app->iptv_home, dx, dy, UI_IPTVHOME_ROWS, UI_IPTVHOME_COLS);
    if (pressed & HidNpadButton_A)
        appLivePlayIptv(app, selected(app), now);
    if (touch.type == TOUCH_TAP) {
        LibHit hit = uiLibraryTap(touch.x, touch.y);
        if (hit.kind == LIBHIT_HOME_CARD && iptvHomeChannel(&app->iptv_home, hit.a, hit.b) >= 0) {
            v->rows.cursor = hit.a;
            v->cols[hit.a].cursor = hit.b;
            iptvHomeMove(v, &app->iptv_home, 0, 0, UI_IPTVHOME_ROWS, UI_IPTVHOME_COLS);
            appLivePlayIptv(app, iptvHomeChannel(&app->iptv_home, hit.a, hit.b), now);
        } else if (hit.kind == LIBHIT_FAVORITE) {
            appIptvToggleFavorite(app, selected(app), now);
        }
    } else if (touch.type == TOUCH_DRAG) {
        appLibraryDrag(app, touch, now);
    }
}

// What a channel is on now, from the list's guide.
static const char *nowTitle(const void *ctx, int channel)
{
    const App *app = ctx;
    const IptvGuide *g = &app->iptv_guide;
    if (channel < 0 || channel >= g->count)
        return NULL;
    int64_t now_s = (int64_t)time(NULL);
    for (int j = 0; j < g->counts[channel]; j++) {
        const LiveProgram *p = &g->programs[channel][j];
        if (p->start <= now_s && p->end > now_s)
            return p->title;
    }
    return NULL;
}

void appIptvHomeDraw(const App *app)
{
    IptvLoadStatus s;
    iptvLoadStatus(&s);
    bool loading = s.state == IPTV_LOAD_LIST || (app->iptv.count == 0 && s.state == IPTV_LOAD_GUIDE);
    uiDrawIptvHome(&app->iptv_home, &app->iptv_home_view, &app->iptv, &app->iptv_marks, nowTitle, app, loading);
}

bool appIptvHomeScrollPx(App *app, int px)
{
    IptvHomeView *v = &app->iptv_home_view;
    Scroll s = v->rows;
    bool moved = scrollPx(&s, px, UI_IPTVHOME_ROW_PX, UI_IPTVHOME_ROWS, 1, app->iptv_home.count);
    v->rows = s;
    return moved;
}

bool appIptvHomeRowScrollPx(App *app, int row, int px)
{
    if (row < 0 || row >= app->iptv_home.count)
        return false;
    app->iptv_home_view.rows.cursor = row;  // The row a finger moves along is the one chosen.
    return scrollPx(&app->iptv_home_view.cols[row], px, UI_IPTVHOME_COL_PX, UI_IPTVHOME_COLS, 1,
                    app->iptv_home.rows[row].count);
}
