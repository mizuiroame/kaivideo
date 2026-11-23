// Stremio addons in KaiVideo (stremio.h): their addresses (addons.txt), the
// Addons view's library made from their catalogs, a series' episodes read
// when its page opens, searches, and the list of streams to choose one from.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <switch.h>

#include "app.h"
#include "approot.h"
#include "buttons.h"
#include "keyboard_system.h"
#include "lang.h"
#include "net.h"
#include "stremioload.h"
#include "ui_library.h"
#include "ui_settings.h"
#include "ui_streams.h"

#define ADDONS_FILE "addons.txt"
#define REMOVE_ARMED_NS (4ull * 1000000000ull)  // A second A within this removes the addon.

static StremioAddonList g_urls;
static bool g_network;           // Held while there are addons.
static char g_state[96];         // How the load goes, for Settings.
static int g_armed = -1;         // The addon a first A asked to remove.
static uint64_t g_armed_at;
static char g_asked_series[STREMIO_ID_MAX];

static void useNetwork(void)
{
    bool want = g_urls.count > 0;
    if (want && !g_network)
        g_network = netInit();
    else if (!want && g_network) {
        stremioLoadExit();
        netExit();
        g_network = false;
    }
}

static void filePath(char *out, size_t size)
{
    appConfigPath(out, size, ADDONS_FILE);
}

static void save(App *app, uint64_t now)
{
    char path[APPROOT_PATH_MAX + 32], text[STREMIO_ADDONS_MAX * (STREMIO_URL_MAX + 2)];
    filePath(path, sizeof(path));
    if (g_urls.count == 0) {
        remove(path);
        return;
    }
    FILE *f = stremioAddonListWrite(&g_urls, text, sizeof(text)) ? fopen(path, "wb") : NULL;
    bool ok = f && fputs(text, f) >= 0;
    if (f)
        ok = fclose(f) == 0 && ok;
    if (!ok)
        appNotify(app, NOTICE_ERROR, T("It couldn't be written to the card."), now);
}

static void load(App *app)
{
    (void)app;
    useNetwork();
    if (g_urls.count == 0)
        return;
    snprintf(g_state, sizeof(g_state), "%s", T("Loading..."));
    stremioLoadStart(&g_urls);
}

void appStremioStart(App *app)
{
    char path[APPROOT_PATH_MAX + 32], text[STREMIO_ADDONS_MAX * (STREMIO_URL_MAX + 2) + 1] = "";
    filePath(path, sizeof(path));
    FILE *f = fopen(path, "rb");
    if (f) {
        size_t n = fread(text, 1, sizeof(text) - 1, f);
        text[n] = '\0';
        fclose(f);
    }
    stremioAddonListRead(&g_urls, text);
    load(app);
}

bool appStremioConfigured(void)
{
    return g_urls.count > 0;
}

void appStremioExit(App *app)
{
    stremioLoadExit();
    stremioStoreFree(&app->stremio);
    stremioStreamsFree(app->streams, app->stream_count);
    app->streams = NULL;
    app->stream_count = 0;
    g_urls.count = 0;
    useNetwork();
}

// --- The library ---

void appStremioInstall(App *app)
{
    if (!app->config.use_addons)
        return;
    Library built;
    libraryInit(&built, STREMIO_PREFIX);
    stremioFillLibrary(&built, &app->stremio, T("Reading the episodes..."), T("The episodes couldn't be read."));
    libraryBuild(&built);
    appLibraryInstallBuilt(app, &built);
}

// The series whose page (or details) is open, if its episodes weren't read: its id, else "".
static void shownStandIn(const App *app, char *id, size_t size)
{
    const LibView *v = &app->view;
    int item = -1;
    char kind[16];
    id[0] = '\0';
    if (v->show >= 0 && (size_t)v->show < app->library.show_count && app->library.shows[v->show].count > 0)
        item = app->library.shows[v->show].episodes[0];
    else if (v->details >= 0 && (size_t)v->details < app->library.count)
        item = v->details;
    if (item < 0 || !stremioParseLocation(app->library.items[item].path, kind, sizeof(kind), id, size) ||
        strcmp(kind, "show"))
        id[0] = '\0';
}

static void takeLoad(App *app, uint64_t now)
{
    bool ok;
    char note[STREMIO_NOTE_MAX];
    if (!stremioLoadTake(&app->stremio, &ok, note, sizeof(note)))
        return;
    int metas = 0;
    for (int i = 0; i < app->stremio.list_count; i++)
        metas += app->stremio.lists[i].count;
    snprintf(g_state, sizeof(g_state), T("Addons: %d, catalogs: %d"), app->stremio.addon_count,
             app->stremio.list_count);
    appLog("Addons: %d of %d answered, %d catalogs, %d titles%s%s", app->stremio.addon_count, g_urls.count,
           app->stremio.list_count, metas, note[0] ? "; " : "", note);
    g_asked_series[0] = '\0';
    if (!ok)
        appNotify(app, NOTICE_ERROR, note[0] ? note : T("No addon answered."), now);
    else if (note[0])
        appNotify(app, NOTICE_INFO, note, now);
    appStremioInstall(app);
}

static void takeEpisodes(App *app)
{
    char id[STREMIO_ID_MAX * 2];
    StremioVideo *videos;
    int count;
    if (!stremioTakeEpisodes(id, sizeof(id), &videos, &count))
        return;
    if (!videos)
        appLog("Addons: series %s's episodes couldn't be read", id);
    stremioStoreSetEpisodes(&app->stremio, id, videos, count);
    appStremioInstall(app);
}

static void takeSearch(App *app)
{
    if (!stremioTakeSearch(&app->stremio.found))
        return;
    appStremioInstall(app);
    if (app->view.tab == TAB_SEARCH && app->view.query[0])
        libviewSearch(&app->view, &app->library, app->view.query);  // The results, with what came.
}

void appStremioSearch(App *app, const char *query)
{
    if (app->config.use_addons && query[0] && app->stremio.addon_count > 0)
        stremioAskSearch(&app->stremio, query);
}

// --- Choosing a stream ---

static void closeStreams(App *app)
{
    app->streams_open = false;
    stremioStreamsFree(app->streams, app->stream_count);
    app->streams = NULL;
    app->stream_count = 0;
}

static void takeStreams(App *app, uint64_t now)
{
    char id[STREMIO_ID_MAX * 2];
    StremioStream *streams;
    int count;
    if (!stremioTakeStreams(id, sizeof(id), &streams, &count, app->streams_note, sizeof(app->streams_note)))
        return;
    if (!app->streams_open) {  // Closed meanwhile.
        stremioStreamsFree(streams, count);
        return;
    }
    stremioStreamsFree(app->streams, app->stream_count);
    app->streams = streams;
    app->stream_count = count;
    app->streams_loading = false;
    app->stream_cursor = app->stream_first = 0;
    appLog("Addons: %d streams for %s%s%s", count, id, app->streams_note[0] ? "; " : "", app->streams_note);
    (void)now;
}

bool appStremioMayOpen(App *app, const char *path, const char *name, uint64_t now)
{
    if (!strcmp(app->stremio_go, path)) {  // Its stream was just chosen.
        app->stremio_go[0] = '\0';
        return true;
    }
    char kind[16], id[STREMIO_ID_MAX];
    if (!stremioParseLocation(path, kind, sizeof(kind), id, sizeof(id)))
        return false;
    if (!strcmp(kind, "show")) {
        appNotify(app, NOTICE_INFO, T("The episodes are still being read."), now);
        return false;
    }
    closeStreams(app);
    app->streams_open = true;
    app->streams_loading = true;
    app->streams_note[0] = '\0';
    app->streams_from_start = app->open_from_start;
    snprintf(app->streams_path, sizeof(app->streams_path), "%s", path);
    snprintf(app->streams_name, sizeof(app->streams_name), "%s", name);
    stremioAskStreams(&app->stremio, !strcmp(kind, "series") ? "series" : "movie", id);
    return false;
}

static void chooseStream(App *app, int index, uint64_t now)
{
    if (index < 0 || index >= app->stream_count)
        return;
    char path[LIBRARY_PATH_MAX], name[APP_NAME_MAX];
    snprintf(path, sizeof(path), "%s", app->streams_path);
    snprintf(name, sizeof(name), "%s", app->streams_name);
    stremioChoose(path, app->streams[index].url, &app->streams[index].headers);
    appLog("Addons: %s plays a stream of %s", path, app->streams[index].addon);  // Not its address: it may hold a key.
    bool from_start = app->streams_from_start;
    closeStreams(app);
    snprintf(app->stremio_go, sizeof(app->stremio_go), "%s", path);
    app->open_from_start = from_start;
    if (!appStartVideo(app, path, name, now))
        app->open_from_start = false;
}

static void moveStreams(App *app, int delta)
{
    if (app->stream_count == 0)
        return;
    int c = app->stream_cursor + delta;
    c = c < 0 ? 0 : c >= app->stream_count ? app->stream_count - 1 : c;
    app->stream_cursor = c;
    if (c < app->stream_first)
        app->stream_first = c;
    if (c >= app->stream_first + UI_STREAM_ROWS)
        app->stream_first = c - UI_STREAM_ROWS + 1;
}

void appStremioStreamsInput(App *app, uint64_t pressed, TouchEvent touch, uint64_t now)
{
    if (pressed & BUTTONS_UP)
        moveStreams(app, -1);
    if (pressed & BUTTONS_DOWN)
        moveStreams(app, 1);
    if (pressed & BUTTONS_PAGE_UP)
        moveStreams(app, -UI_STREAM_ROWS);
    if (pressed & BUTTONS_PAGE_DOWN)
        moveStreams(app, UI_STREAM_ROWS);
    if (pressed & HidNpadButton_A) {
        chooseStream(app, app->stream_cursor, now);
        return;
    }
    if (pressed & HidNpadButton_B) {
        closeStreams(app);
        return;
    }
    if (touch.type == TOUCH_TAP) {
        LibHit hit = uiLibraryTap(touch.x, touch.y);
        if (hit.kind == LIBHIT_STREAM_ROW)
            chooseStream(app, hit.a, now);
        else if (hit.kind == LIBHIT_CLOSE)
            closeStreams(app);
    }
}

void appStremioStreamsDraw(const App *app)
{
    uiDrawStreams(app->streams_name, app->streams, app->stream_count, app->stream_cursor, app->stream_first,
                  app->streams_loading, app->streams_note);
}

void appStremioUpdate(App *app, uint64_t now)
{
    takeLoad(app, now);
    takeStreams(app, now);
    if (!app->config.use_addons)
        return;
    takeEpisodes(app);
    takeSearch(app);
    char id[STREMIO_ID_MAX];
    shownStandIn(app, id, sizeof(id));
    if (id[0] && strcmp(id, g_asked_series) && stremioStoreEpisodesState(&app->stremio, id) == STREMIO_EPISODES_UNREAD) {
        snprintf(g_asked_series, sizeof(g_asked_series), "%s", id);
        stremioAskEpisodes(&app->stremio, id);
    }
}

// --- Settings ---

static void addAddon(App *app, uint64_t now)
{
    if (g_urls.count >= STREMIO_ADDONS_MAX) {
        appNotify(app, NOTICE_ERROR, T("There are as many addons as KaiVideo keeps: remove one first."), now);
        return;
    }
    char text[STREMIO_URL_MAX];
    switch (systemKeyboardInput(T("The addon's address (its manifest.json)"), "", text, sizeof(text),
                                STREMIO_URL_MAX - 1)) {
    case SYSTEM_KEYBOARD_OK: break;
    case SYSTEM_KEYBOARD_UNAVAILABLE: appNotify(app, NOTICE_ERROR, T("The keyboard couldn't open."), now); return;
    default: return;
    }
    if (!stremioAddonListAdd(&g_urls, text)) {
        appNotify(app, NOTICE_ERROR, T("That isn't an addon's address, or it's added already."), now);
        return;
    }
    save(app, now);
    load(app);
    appNotify(app, NOTICE_DONE, T("Addon added: its catalogs load."), now);
}

static void removeAddon(App *app, int index, uint64_t now)
{
    if (g_armed != index || now - g_armed_at > REMOVE_ARMED_NS) {  // First A: asks for a second.
        g_armed = index;
        g_armed_at = now;
        appShowMessage(app, T("Press A again to remove this addon."), now);
        return;
    }
    g_armed = -1;
    stremioAddonListRemove(&g_urls, index);
    save(app, now);
    stremioLoadExit();
    stremioStoreFree(&app->stremio);
    if (g_urls.count == 0 && app->config.use_addons)
        appLibrarySetSource(app, false);  // Its view goes: the card's.
    else
        appStremioInstall(app);
    load(app);
    appNotify(app, NOTICE_DONE, T("The addon is removed."), now);
}

bool appStremioActivate(App *app, SettingRow row, uint64_t now)
{
    if (row == SETTING_ADDONS_ADD) {
        addAddon(app, now);
        return true;
    }
    if (row == SETTING_ADDONS_LOAD) {
        if (g_urls.count == 0)
            appShowMessage(app, T("Add an addon first."), now);
        else
            load(app);
        return true;
    }
    if (row >= SETTING_ADDON_FIRST && row < SETTING_ADDON_FIRST + STREMIO_ADDONS_MAX) {
        removeAddon(app, row - SETTING_ADDON_FIRST, now);
        return true;
    }
    return false;
}

void appStremioFeedSettings(const App *app)
{
    uint64_t now = armTicksToNs(armGetSystemTick());
    const char *names[STREMIO_ADDONS_MAX];
    for (int i = 0; i < g_urls.count; i++) {  // Its name once read, else its address.
        names[i] = g_urls.urls[i];
        for (int k = 0; k < app->stremio.addon_count; k++) {
            char url[STREMIO_URL_MAX];
            snprintf(url, sizeof(url), "%.*s/manifest.json", (int)sizeof(url) - 16, app->stremio.addons[k].base);
            if (!strcmp(url, g_urls.urls[i]))
                names[i] = app->stremio.addons[k].name;
        }
    }
    bool armed = g_armed >= 0 && now - g_armed_at <= REMOVE_ARMED_NS;
    uiSettingsSetAddons(names, g_urls.count, armed ? g_armed : -1,
                        g_urls.count == 0 ? T("None added") : stremioBusy(STREMIO_JOB_LOAD) ? T("Loading...") : g_state);
    settingsSetAddonCount(g_urls.count);
}
