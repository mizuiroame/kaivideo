// KaiVideo's side of downloads: L on a server's film or episode (or a series
// page's season) downloads it to the card, the Downloads panel (+ menu) shows
// how far each has come, a server's video with a whole download plays from
// the card, and what's watched of a download reaches its server later.
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include <switch.h>

#include "app.h"
#include "buttons.h"
#include "dlworker.h"
#include "kinetic.h"
#include "ui_downloads.h"
#include "ui_library.h"
#include "util.h"

#define FILE_NAME_MAX 600
#define GIB (1024.0 * 1024 * 1024)
#define MIB (1024.0 * 1024)
#define SAME_POSITION_NS 1000000000ll  // Closer than this, a resume point hasn't changed.
#define NAME_TRIES 9                   // "Title (2)" to "Title (9)" when two videos share a name.
#define ROW_TEXT_MAX 200

// --- The list and the card ---

static void updatePill(const App *app);

static void takeList(App *app)
{
    dlListFree(&app->downloads);
    dlWorkerList(&app->downloads);
    app->dl_changes = dlWorkerChanges();
    updatePill(app);
    int last = (int)app->downloads.count - 1;
    if (app->downloads_scroll.cursor > last)
        app->downloads_scroll.cursor = last < 0 ? 0 : last;
    if (app->downloads_confirm > last)
        app->downloads_confirm = -1;
}

// The mark a video's cards and rows show for its download (a server's video,
// or its download in the card's library).
static int markOf(const char *location, const void *user, double *fraction)
{
    const App *app = user;
    int i = dlListFindOrigin(&app->downloads, location, NULL);
    if (i < 0)
        i = dlListFindPath(&app->downloads, location);
    if (i < 0)
        return UI_DL_NONE;
    const Download *d = &app->downloads.items[i];
    *fraction = d->size > 0 ? (double)d->done / (double)d->size : 0;
    switch (d->state) {
    case DL_QUEUED: return UI_DL_QUEUED;
    case DL_ACTIVE: return UI_DL_ACTIVE;
    case DL_PAUSED: return UI_DL_PAUSED;
    case DL_FAILED: return UI_DL_FAILED;
    default: return UI_DL_DONE;
    }
}

// The header's pill: how many are on their way and how far they've come
// together; what's paused or failed; nothing when all are on the card.
static void updatePill(const App *app)
{
    int coming = 0, paused = 0, failed = 0;
    int64_t done = 0, size = 0;
    for (size_t i = 0; i < app->downloads.count; i++) {
        const Download *d = &app->downloads.items[i];
        coming += d->state == DL_QUEUED || d->state == DL_ACTIVE;
        paused += d->state == DL_PAUSED;
        failed += d->state == DL_FAILED;
        if ((d->state == DL_QUEUED || d->state == DL_ACTIVE) && d->size > 0) {
            done += d->done;
            size += d->size;
        }
    }
    char label[48] = "";
    double fraction = size > 0 ? (double)done / (double)size : 0;
    if (coming)
        snprintf(label, sizeof(label), "%d \xC2\xB7 %d%%", coming, (int)(fraction * 100));
    else if (failed)
        snprintf(label, sizeof(label), "%d failed", failed);
    else if (paused)
        snprintf(label, sizeof(label), "%d paused", paused);
    uiLibrarySetDownloadsPill(label, fraction);
}

void appDownloadsStart(App *app)
{
    uiLibrarySetDownloads(markOf, app);
    char list[FILE_NAME_MAX], sync[FILE_NAME_MAX];
    appConfigPath(list, sizeof(list), "downloads.txt");
    appConfigPath(sync, sizeof(sync), "offline_sync.txt");
    dlListInit(&app->downloads);
    app->downloads_confirm = -1;
    if (!dlWorkerStart(list, sync))
        appLog("The downloads couldn't start.");
    takeList(app);
    app->dl_finished = dlWorkerFinished();
}

void appDownloadsExit(App *app)
{
    dlWorkerStop();
    dlListFree(&app->downloads);
}

// Where downloads go: a folder in the library's, so the card's library shows
// them. With no library folder yet, that folder is made the library's.
static bool downloadsRoot(App *app, char *out, size_t size)
{
    if (!app->library_root[0]) {
        snprintf(app->library_root, sizeof(app->library_root), "sdmc:/%s", DL_FOLDER);
        mkdir(app->library_root, 0777);
        appSaveSettings(app);
    }
    const char *name = utilFileName(app->library_root);
    if (!strcmp(name, DL_FOLDER)) {
        snprintf(out, size, "%s", app->library_root);
        return true;
    }
    return utilJoinPath(out, size, app->library_root, DL_FOLDER);
}

static bool streamingNow(const App *app)
{
    return (app->player || app->opening) && jfIsRemoteLocation(app->playing_path) && !app->open_copy[0];
}

void appDownloadsUpdate(App *app, uint64_t now)
{
    dlWorkerHold(streamingNow(app));
    if (dlWorkerChanges() != app->dl_changes)
        takeList(app);
    unsigned finished = dlWorkerFinished();
    if (finished == app->dl_finished)
        return;
    app->dl_finished = finished;
    appShowMessage(app, "A download is on the card.", now);
    if (!app->config.use_jellyfin && !app->player && !app->opening)
        appLibraryRescan(app, now);  // The card's library shows it.
}

const char *appDownloadedCopy(const App *app, const char *location)
{
    for (size_t i = 0; i < app->downloads.count; i++) {
        const Download *d = &app->downloads.items[i];
        if (d->state == DL_DONE && !strcmp(d->origin, location))
            return d->path;
    }
    return NULL;
}

// The same, only if its file is still on the card (it may have been deleted
// from a PC): else the video streams from its server.
static const char *copyOnCard(const App *app, const char *location)
{
    const char *path = appDownloadedCopy(app, location);
    struct stat st;
    return path && stat(path, &st) == 0 ? path : NULL;
}

const char *appPlayableCopy(const App *app, const char *location)
{
    return copyOnCard(app, location);
}

static const Download *downloadOf(const App *app, const char *location)
{
    int i = dlListFindOrigin(&app->downloads, location, NULL);
    return i >= 0 ? &app->downloads.items[i] : NULL;
}

static void sizeText(int64_t bytes, char *out, size_t size)
{
    if (bytes >= GIB)
        snprintf(out, size, "%.1f GB", bytes / GIB);
    else
        snprintf(out, size, "%.0f MB", bytes / MIB);
}

static int percentOf(const Download *d)
{
    return d->size > 0 ? (int)(d->done * 100 / d->size) : 0;
}

// --- Asking for downloads ---

const char *appDownloadLabel(const App *app, int item, char *out, size_t size)
{
    const LibraryItem *it = &app->library.items[item];
    if (!jfIsRemoteLocation(it->path) || liveIsLocation(it->path))
        return NULL;
    const Download *d = downloadOf(app, it->path);
    if (!d)
        return it->blocked ? NULL : "Download";
    switch (d->state) {
    case DL_QUEUED: return "Waiting to download";
    case DL_ACTIVE: snprintf(out, size, "Downloading %d%%", percentOf(d)); return out;
    case DL_PAUSED: snprintf(out, size, "Paused at %d%%", percentOf(d)); return out;
    case DL_FAILED: return "Download again";
    default: return "On the card";
    }
}

// The name its server gives a video (an episode's series from the library's).
static VideoName nameOf(const Library *lib, int item)
{
    const LibraryItem *it = &lib->items[item];
    VideoName name = it->name;
    if (it->show >= 0)
        snprintf(name.show, sizeof(name.show), "%s", lib->shows[it->show].name);
    return name;
}

// Adds a video to the downloads. False, with what to say, if it can't be.
static bool enqueue(App *app, int item, char *why, size_t why_size)
{
    const LibraryItem *it = &app->library.items[item];
    char account[DL_ACCOUNT_MAX], root[LIBRARY_PATH_MAX], path[LIBRARY_PATH_MAX], fallback[NAMING_MAX];
    if (it->blocked) {
        snprintf(why, why_size, "Only videos the console plays as they are can be downloaded for now.");
        return false;
    }
    if (!jfSyncAccountOf(it->path, account, sizeof(account))) {
        snprintf(why, why_size, "Sign in to its server to download it.");
        return false;
    }
    if (!downloadsRoot(app, root, sizeof(root))) {
        snprintf(why, why_size, "The library's folder name is too long for downloads.");
        return false;
    }
    dlSafeName(it->path, fallback, sizeof(fallback));  // For names with nothing a card can keep.
    VideoName name = nameOf(&app->library, item), file_name = name;
    for (int n = 1; n <= NAME_TRIES; n++) {
        if (n > 1)
            snprintf(file_name.title, sizeof(file_name.title), "%.*s (%d)", NAMING_MAX - 8, name.title, n);
        // The file holds what the server has; FFmpeg reads any container whatever its name says.
        if (!dlPathFor(&file_name, "mkv", fallback, root, path, sizeof(path)))
            break;
        if (dlListFindPath(&app->downloads, path) >= 0)
            continue;  // Another video's name.
        if (dlWorkerAdd(it->path, account, &name, it->runtime_ns, path) >= 0) {
            takeList(app);
            return true;
        }
    }
    snprintf(why, why_size, "It couldn't be added to the downloads.");
    return false;
}

void appDownloadItem(App *app, int item, uint64_t now)
{
    const LibraryItem *it = &app->library.items[item];
    if (!jfIsRemoteLocation(it->path) || liveIsLocation(it->path))
        return;
    const Download *d = downloadOf(app, it->path);
    char why[128];
    if (!d) {
        if (enqueue(app, item, why, sizeof(why)))
            appShowMessage(app, "Added to Downloads (+ menu).", now);
        else
            appShowMessage(app, why, now);
        return;
    }
    switch (d->state) {
    case DL_QUEUED:
    case DL_ACTIVE: dlWorkerPause(d->path, true); appShowMessage(app, "Download paused.", now); break;
    case DL_PAUSED: dlWorkerPause(d->path, false); appShowMessage(app, "The download goes on.", now); break;
    case DL_FAILED: dlWorkerRetry(d->path); appShowMessage(app, "Downloading it again.", now); break;
    default: appShowMessage(app, "It's on the card: it plays from there. Downloads (+ menu) deletes it.", now); break;
    }
    takeList(app);
}

// The season's episodes: their items in the library. Returns how many.
static int seasonItems(const Library *lib, int show, int season, int *out, int max)
{
    int first, count, n = 0;
    libviewSeasonRange(lib, show, season, &first, &count);
    for (int i = 0; i < count && n < max; i++)
        out[n++] = lib->shows[show].episodes[first + i];
    return n;
}

#define SEASON_MAX 512

const char *appSeasonDownloadLabel(const App *app, int show, int season, bool *done, char *out, size_t size)
{
    int items[SEASON_MAX];
    int n = seasonItems(&app->library, show, season, items, SEASON_MAX);
    *done = false;
    if (n == 0 || !jfIsRemoteLocation(app->library.items[items[0]].path))
        return NULL;
    int whole = 0, asked = 0;
    for (int i = 0; i < n; i++) {
        const Download *d = downloadOf(app, app->library.items[items[i]].path);
        asked += d != NULL;
        whole += d && d->state == DL_DONE;
    }
    *done = whole == n;
    if (*done)
        snprintf(out, size, "Season on the card");
    else if (asked == n)  // All asked for: how many have come.
        snprintf(out, size, "Season: %d of %d on the card", whole, n);
    else if (asked)  // Some: L asks for the others.
        snprintf(out, size, "Download season");
    else
        snprintf(out, size, "Download season");
    return out;
}

void appDownloadSeason(App *app, int show, int season, uint64_t now)
{
    int items[SEASON_MAX];
    int n = seasonItems(&app->library, show, season, items, SEASON_MAX), added = 0, skipped = 0;
    char why[128] = "";
    for (int i = 0; i < n; i++) {
        if (downloadOf(app, app->library.items[items[i]].path))
            continue;
        if (enqueue(app, items[i], why, sizeof(why)))
            added++;
        else
            skipped++;
    }
    char message[200];
    if (added && skipped)
        snprintf(message, sizeof(message), "%d episodes added to Downloads; %d can't be: %s", added, skipped, why);
    else if (added)
        snprintf(message, sizeof(message), "%d episode%s added to Downloads (+ menu).", added, added == 1 ? "" : "s");
    else if (skipped)
        snprintf(message, sizeof(message), "%s", why);
    else
        snprintf(message, sizeof(message), "This season is in Downloads already (+ menu).");
    appShowMessage(app, message, now);
}

// --- What was watched of a download ---

static void saveResume(const App *app)
{
    char path[FILE_NAME_MAX];
    appConfigPath(path, sizeof(path), "resume.txt");
    resumeSave(&app->resume, path);
}

// Makes key's resume point and watched mark say this, if they don't already.
// True if something changed.
static bool applyState(App *app, const char *key, int64_t position_ns, bool played, int64_t runtime_ns)
{
    bool changed = false;
    int64_t at = 0;
    bool has = resumeFind(&app->resume, key, &at, NULL);
    int64_t diff = at - position_ns;
    bool wanted = position_ns >= RESUME_MIN_NS && !played;
    if (wanted != has || (wanted && (diff > SAME_POSITION_NS || diff < -SAME_POSITION_NS))) {
        resumeRemember(&app->resume, key, wanted ? position_ns : 0, runtime_ns);
        changed = true;
    }
    if (watchedHas(&app->watched, key) != played) {
        watchedSet(&app->watched, key, played);
        changed = true;
    }
    return changed;
}

// The download of what's playing: the server's video read from the card, or
// a file of the card's library that's a download. -1 if neither.
static int playingDownload(const App *app)
{
    const char *path = app->open_copy[0] ? app->open_copy : app->playing_path;
    int i = dlListFindPath(&app->downloads, path);
    return i >= 0 && app->downloads.items[i].state == DL_DONE ? i : -1;
}

void appDownloadsPlayed(App *app, int64_t position_ns, bool finished)
{
    int i = playingDownload(app);
    if (i < 0)
        return;
    const Download *d = &app->downloads.items[i];
    (void)position_ns;
    (void)finished;
    // What's played was kept under playing_path (stopVideo): the download's other place gets the same.
    const char *other = app->open_copy[0] ? d->path : d->origin;
    int64_t at = 0;
    if (!resumeFind(&app->resume, app->playing_path, &at, NULL))
        at = 0;
    bool played = watchedHas(&app->watched, app->playing_path);
    applyState(app, other, at, played && at == 0, d->runtime_ns);
    if (played && at > 0 && !watchedHas(&app->watched, other))
        watchedSet(&app->watched, other, true);  // Watched before, and being watched again.
    appSaveWatched(app);
    dlWorkerRecord(d->account, d->origin, at, played, (int64_t)time(NULL));
}

void appDownloadsMarked(App *app, const char *path, bool watched)
{
    for (size_t i = 0; i < app->downloads.count; i++) {
        const Download *d = &app->downloads.items[i];
        bool local = !strcmp(d->path, path);
        if (d->state != DL_DONE || (!local && strcmp(d->origin, path) != 0))
            continue;
        watchedSet(&app->watched, local ? d->origin : d->path, watched);
        dlWorkerRecord(d->account, d->origin, 0, watched, (int64_t)time(NULL));
        return;
    }
}

void appDownloadsCatalogCame(App *app, ServerKind kind)
{
    dlWorkerSyncSoon();  // Its server answers: what waits for it can go.
    if (!app->server_has[kind])
        return;
    const JfCatalog *c = &app->server_catalogs[kind];
    bool changed = false;
    for (size_t i = 0; i < app->downloads.count; i++) {
        const Download *d = &app->downloads.items[i];
        char account[DL_ACCOUNT_MAX];
        if (d->state != DL_DONE || !jfIsRemoteLocation(d->origin) || serverKindOf(d->origin) != kind ||
            !jfSyncAccountOf(d->origin, account, sizeof(account)) || strcmp(account, d->account) != 0)
            continue;
        DlSyncEvent e;
        if (dlWorkerSyncFind(d->account, d->origin, &e)) {  // Watched here since: that stays until told.
            changed |= applyState(app, d->origin, e.position_ns, e.played, d->runtime_ns);
            changed |= applyState(app, d->path, e.position_ns, e.played, d->runtime_ns);
            continue;
        }
        int at = libraryFind(&c->library, d->origin);
        if (at >= 0)
            changed |= applyState(app, d->path, c->users[at].position_ns, c->users[at].played, d->runtime_ns);
    }
    if (changed) {
        saveResume(app);
        appSaveWatched(app);
        appLibraryRefreshHome(app);
    }
}

void appDownloadsNameLibrary(const App *app, Library *lib)
{
    dlApplyNames(lib, &app->downloads);
    libraryBuild(lib);
}

// --- The Downloads panel ---

void appDownloadsOpen(App *app)
{
    takeList(app);
    app->downloads_open = true;
    app->downloads_confirm = -1;
    app->downloads_scroll = (Scroll){0, 0, 0};
}

#define ROWS_SHOWN_MAX 256  // The panel lists this many at most.

// How many rows the panel lists.
static int rowsShown(const App *app)
{
    int count = (int)app->downloads.count;
    return count < ROWS_SHOWN_MAX ? count : ROWS_SHOWN_MAX;
}

static void keepInView(Scroll *s, int count)
{
    if (s->cursor >= count)
        s->cursor = count > 0 ? count - 1 : 0;
    if (s->cursor < s->first)
        s->first = s->cursor;
    if (s->cursor >= s->first + UI_DOWNLOAD_ROWS)
        s->first = s->cursor - UI_DOWNLOAD_ROWS + 1;
    int last_first = count > UI_DOWNLOAD_ROWS ? count - UI_DOWNLOAD_ROWS : 0;
    if (s->first > last_first)
        s->first = last_first;  // No empty rows at the bottom while earlier ones hide.
    s->offset = 0;
}

bool appDownloadsScrollPx(App *app, int px)
{
    return scrollPx(&app->downloads_scroll, px, UI_DOWNLOAD_ROW_H, UI_DOWNLOAD_ROWS, 1, rowsShown(app));
}

// A: a whole one plays; one going on pauses; a paused one goes on; a failed one tries again.
static void activate(App *app, int row, uint64_t now)
{
    if (row < 0 || (size_t)row >= app->downloads.count)
        return;
    Download d = app->downloads.items[row];
    switch (d.state) {
    case DL_DONE: {
        char name[APP_NAME_MAX];
        if (d.name.is_episode)
            snprintf(name, sizeof(name), "%s \xC2\xB7 S%02dE%02d", d.name.show, d.name.season, d.name.episode);
        else
            snprintf(name, sizeof(name), "%s", d.name.title);
        app->downloads_open = false;
        appStartVideo(app, d.path, name, now);
        return;
    }
    case DL_QUEUED:
    case DL_ACTIVE: dlWorkerPause(d.path, true); break;
    case DL_PAUSED: dlWorkerPause(d.path, false); break;
    default: dlWorkerRetry(d.path); break;
    }
    takeList(app);
}

// X once asks, X again on the same one deletes it (from the card too).
static void deleteRow(App *app, int row, uint64_t now)
{
    if (row < 0 || (size_t)row >= app->downloads.count)
        return;
    if (app->downloads_confirm != row) {
        app->downloads_confirm = row;
        appShowMessage(app, "Press X again to delete it from the card.", now);
        return;
    }
    bool whole = app->downloads.items[row].state == DL_DONE;
    dlWorkerRemove(app->downloads.items[row].path);
    app->downloads_confirm = -1;
    takeList(app);
    keepInView(&app->downloads_scroll, rowsShown(app));
    appShowMessage(app, "Deleted.", now);
    if (whole && !app->config.use_jellyfin)
        appLibraryRescan(app, now);  // The card's library lets it go.
}

void appDownloadsInput(App *app, uint64_t pressed, TouchEvent touch, uint64_t now)
{
    Scroll *s = &app->downloads_scroll;
    int count = rowsShown(app);
    if (pressed & (BUTTONS_UP | BUTTONS_DOWN)) {
        s->cursor += (pressed & BUTTONS_UP) ? -1 : 1;
        s->cursor = s->cursor < 0 ? 0 : s->cursor;
        keepInView(s, count);
        app->downloads_confirm = -1;
    }
    if (pressed & HidNpadButton_A)
        activate(app, s->cursor, now);
    if (pressed & HidNpadButton_X)
        deleteRow(app, s->cursor, now);
    if (pressed & (HidNpadButton_B | HidNpadButton_Plus)) {
        app->downloads_open = false;
        return;
    }
    if (touch.type == TOUCH_DRAG) {
        appLibraryDrag(app, touch, now);
        return;
    }
    if (touch.type != TOUCH_TAP)
        return;
    LibHit hit = uiLibraryTap(touch.x, touch.y);
    if (hit.kind == LIBHIT_DOWNLOAD_ROW) {
        s->cursor = hit.a;
        app->downloads_confirm = -1;
        activate(app, hit.a, now);
    } else if (hit.kind == LIBHIT_DOWNLOAD_DELETE) {
        s->cursor = hit.a;
        deleteRow(app, hit.a, now);
    } else if (hit.kind == LIBHIT_CLOSE) {
        app->downloads_open = false;
    }
}

typedef struct {
    char title[ROW_TEXT_MAX], detail[ROW_TEXT_MAX], state[ROW_TEXT_MAX];
} RowText;

static void describe(const Download *d, RowText *t, UiDownloadRow *row)
{
    if (d->name.is_episode) {
        snprintf(t->title, sizeof(t->title), "%s", d->name.title[0] ? d->name.title : d->name.show);
        snprintf(t->detail, sizeof(t->detail), "%s \xC2\xB7 S%02dE%02d", d->name.show, d->name.season, d->name.episode);
    } else {
        snprintf(t->title, sizeof(t->title), d->name.year ? "%s (%d)" : "%s", d->name.title, d->name.year);
        snprintf(t->detail, sizeof(t->detail), "Film");
    }
    char done[32], size[32];
    sizeText(d->done, done, sizeof(done));
    sizeText(d->size, size, sizeof(size));
    switch (d->state) {
    case DL_QUEUED: snprintf(t->state, sizeof(t->state), "Waiting"); break;
    case DL_ACTIVE:
        if (d->size > 0)
            snprintf(t->state, sizeof(t->state), "%s of %s", done, size);
        else
            snprintf(t->state, sizeof(t->state), "Starting...");
        break;
    case DL_PAUSED: snprintf(t->state, sizeof(t->state), "Paused \xC2\xB7 %d%%", percentOf(d)); break;
    case DL_FAILED: snprintf(t->state, sizeof(t->state), "%s", d->error[0] ? d->error : "It stopped."); break;
    default: snprintf(t->state, sizeof(t->state), "On the card \xC2\xB7 %s", size); break;
    }
    *row = (UiDownloadRow){t->title, t->detail, t->state, d->size > 0 && d->done > 0 ? (double)d->done / d->size : -1,
                           d->state == DL_DONE, d->state == DL_FAILED};
}

void appDownloadsDraw(const App *app)
{
    if (!app->downloads_open)
        return;
    int count = (int)app->downloads.count;
    static RowText texts[ROWS_SHOWN_MAX];
    static UiDownloadRow rows[ROWS_SHOWN_MAX];
    int shown = rowsShown(app);
    int waiting = 0, whole = 0;
    for (int i = 0; i < count; i++) {
        waiting += app->downloads.items[i].state == DL_QUEUED || app->downloads.items[i].state == DL_ACTIVE;
        whole += app->downloads.items[i].state == DL_DONE;
    }
    for (int i = 0; i < shown; i++)
        describe(&app->downloads.items[i], &texts[i], &rows[i]);
    char summary[200];
    int untold = dlWorkerSyncWaiting();
    int n = snprintf(summary, sizeof(summary), "%d on the card, %d to come", whole, waiting);
    if (untold > 0 && n > 0 && (size_t)n < sizeof(summary))
        snprintf(summary + n, sizeof(summary) - (size_t)n, " \xC2\xB7 %d watched offline, told to the server when it's back",
                 untold);
    uiDrawDownloads(rows, shown, &app->downloads_scroll, app->downloads_confirm, summary,
                    "Nothing yet: open a film or episode from a server and press L to download it.");
}
