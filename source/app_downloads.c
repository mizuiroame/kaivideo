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
#include "lang.h"
#include "ui_downloads.h"
#include "ui_library.h"
#include "util.h"

#define FILE_NAME_MAX 600
#define SAME_POSITION_NS 1000000000ll  // Closer than this, a resume point hasn't changed.
#define NAME_TRIES 9                   // "Title (2)" to "Title (9)" when two videos share a name.
#define ROW_TEXT_MAX 200
#define CONVERTED_AUTO_BITRATE 8000000ll  // A conversion's bitrate with the quality on Auto.

// --- The list and the card ---

static void updatePill(const App *app);
static void refreshAfterTold(void);

static void takeList(App *app)
{
    bool had = app->downloads.count > 0;
    dlListFree(&app->downloads);
    dlWorkerList(&app->downloads);
    app->dl_changes = dlWorkerChanges();
    updatePill(app);
    appDownloadsTabChanged(app);
    if (had != (app->downloads.count > 0))
        appLibraryRefreshHome(app);  // The Downloads tab comes, or goes.
}

void appDownloadsRefresh(App *app)
{
    takeList(app);
}

bool appDownloadsTabShown(const App *app)
{
    return app->downloads.count > 0 || jfSyncAnySignedIn();
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
    *fraction = dlProgress(d);
    switch (d->state) {
    case DL_QUEUED: return UI_DL_QUEUED;
    case DL_ACTIVE: return UI_DL_ACTIVE;
    case DL_PAUSED: return UI_DL_PAUSED;
    case DL_FAILED: return UI_DL_FAILED;
    case DL_DELETED: return UI_DL_NONE;
    default: return UI_DL_DONE;
    }
}

// The header's pill: how many are on their way and how far they've come
// together; what's paused or failed; nothing when all are on the card.
static void updatePill(const App *app)
{
    int coming = 0, paused = 0, failed = 0;
    double progress = 0;  // Each one coming counts the same: conversions have no size until done.
    for (size_t i = 0; i < app->downloads.count; i++) {
        const Download *d = &app->downloads.items[i];
        bool on_way = d->state == DL_QUEUED || d->state == DL_ACTIVE;
        coming += on_way;
        paused += d->state == DL_PAUSED;
        failed += d->state == DL_FAILED;
        if (on_way)
            progress += dlProgress(d);
    }
    char label[96] = "";
    double fraction = coming ? progress / coming : 0;
    if (coming)
        snprintf(label, sizeof(label), "%d \xC2\xB7 %d%%", coming, (int)(fraction * 100));
    else if (failed)
        snprintf(label, sizeof(label), T("%d failed"), failed);
    else if (paused)
        snprintf(label, sizeof(label), T("%d paused"), paused);
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
    if (!dlWorkerStart(list, sync, appLog))
        appLog("The downloads couldn't start.");
    takeList(app);
    app->dl_finished = dlWorkerFinished();
}

void appDownloadsExit(App *app)
{
    dlWorkerStop();
    dlListFree(&app->downloads);
}

void appDownloadsFolderFirstRun(char *out, size_t size)
{
    appConfigPath(out, size, DL_FOLDER);
}

// Where downloads go: a folder in the library's, so the card's library shows
// them. With no library folder yet, one in KaiVideo's own folder, made the library's.
static bool downloadsRoot(App *app, char *out, size_t size)
{
    if (!app->library_root[0]) {
        appDownloadsFolderFirstRun(app->library_root, sizeof(app->library_root));
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

#define WAIT_NOTE_NS (6ull * 1000000000ull)

// A video from the server began while downloads were on their way: the
// player's bar says they wait for it (once a video).
static void sayDownloadsWait(App *app, uint64_t now)
{
    static bool said;
    if (!app->player || !streamingNow(app)) {
        said = false;
        return;
    }
    if (said)
        return;
    said = true;
    bool coming = false, this_one = false;
    for (size_t i = 0; i < app->downloads.count; i++) {
        const Download *d = &app->downloads.items[i];
        if (d->state != DL_QUEUED && d->state != DL_ACTIVE)
            continue;
        coming = true;
        this_one = this_one || !strcmp(d->origin, app->playing_path);
    }
    if (!coming)
        return;
    const char *note = this_one ? T("Its download waits while it plays from the server, and the others too")
                                : T("Downloads wait while this plays from the server");
    size_t n = strlen(app->bar_note);  // After what's said already ("Resumed at...").
    snprintf(app->bar_note + n, sizeof(app->bar_note) - n, T("%s%s; they go on after"), n ? "  \xC2\xB7  " : "", note);
    app->bar_note_until = now + WAIT_NOTE_NS;
    app->overlay_until = app->overlay_until > now + WAIT_NOTE_NS ? app->overlay_until : now + WAIT_NOTE_NS;
}

void appDownloadsUpdate(App *app, uint64_t now)
{
    dlWorkerHold(streamingNow(app));
    sayDownloadsWait(app, now);
    refreshAfterTold();
    dlWorkerBusy(app->player || app->opening);
    if (dlWorkerChanges() != app->dl_changes)
        takeList(app);
    unsigned finished = dlWorkerFinished();
    if (finished == app->dl_finished)
        return;
    app->dl_finished = finished;
    appNotify(app, NOTICE_DONE, T("A download is on the card."), now);
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

static int percentOf(const Download *d)
{
    return (int)(dlProgress(d) * 100);
}

// The bitrate a video the console can't play is converted at: the server
// quality chosen, 8 Mb/s on Auto (a film of two hours is then about 7 GB).
static long long convertedBitrate(const App *app)
{
    return app->config.jf_quality == 0 ? CONVERTED_AUTO_BITRATE : settingQualityBitrate(&app->config);
}

// --- Asking for downloads ---

const char *appDownloadLabel(const App *app, int item, char *out, size_t size)
{
    const LibraryItem *it = &app->library.items[item];
    if (!jfIsRemoteLocation(it->path) || liveIsLocation(it->path))
        return NULL;
    const Download *d = downloadOf(app, it->path);
    if (!d || d->state == DL_DELETED)
        return it->blocked ? T("Download converted") : T("Download");
    switch (d->state) {
    case DL_QUEUED: return T("Waiting to download");
    case DL_ACTIVE:
        snprintf(out, size, "%s %d%%", d->max_bitrate ? T("Converting") : T("Downloading"), percentOf(d));
        return out;
    case DL_PAUSED: snprintf(out, size, T("Paused at %d%%"), percentOf(d)); return out;
    case DL_FAILED: return T("Download again");
    default: return T("On the card");
    }
}

const char *appDetailsNote(const App *app, int item, char *out, size_t size)
{
    const LibraryItem *it = &app->library.items[item];
    const char *plays = jfPlayabilityMessage((JfPlayability)it->blocked);
    bool streams = jfIsRemoteLocation(it->path) && !appPlayableCopy(app, it->path);
    bool coming = false, this_one = false;
    for (size_t i = 0; streams && i < app->downloads.count; i++) {
        const Download *d = &app->downloads.items[i];
        if (d->state != DL_QUEUED && d->state != DL_ACTIVE)
            continue;
        coming = true;
        this_one = this_one || !strcmp(d->origin, it->path);
    }
    if (!coming)
        return plays;
    snprintf(out, size, "%s%s%s",
             this_one ? T("Still downloading: playing it streams it from the server, and its download and the "
                          "others pause until you stop.")
                      : T("Playing it streams it from the server: the downloads pause until you stop."),
             plays[0] ? " " : "", plays);
    return out;
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
    if (!jfSyncAccountOf(it->path, account, sizeof(account))) {
        snprintf(why, why_size, "%s", T("Sign in to its server to download it."));
        return false;
    }
    if (!downloadsRoot(app, root, sizeof(root))) {
        snprintf(why, why_size, "%s", T("The library's folder name is too long for downloads."));
        return false;
    }
    dlSafeName(it->path, fallback, sizeof(fallback));  // For names with nothing a card can keep.
    VideoName name = nameOf(&app->library, item), file_name = name;
    // What the console can't play comes converted by the server: its parts are rewrapped into an MKV.
    long long bitrate = it->blocked ? convertedBitrate(app) : 0;
    for (int n = 1; n <= NAME_TRIES; n++) {
        if (n > 1)
            snprintf(file_name.title, sizeof(file_name.title), "%.*s (%d)", NAMING_MAX - 8, name.title, n);
        // The file holds what the server has; FFmpeg reads any container whatever its name says.
        if (!dlPathFor(&file_name, "mkv", fallback, root, path, sizeof(path)))
            break;
        if (dlListFindPath(&app->downloads, path) >= 0)
            continue;  // Another video's name.
        if (dlWorkerAdd(it->path, account, &name, it->runtime_ns, path, bitrate) >= 0) {
            takeList(app);
            return true;
        }
    }
    snprintf(why, why_size, "%s", T("It couldn't be added to the downloads."));
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
        if (!enqueue(app, item, why, sizeof(why)))
            appNotify(app, NOTICE_ERROR, why, now);
        else if (it->blocked) {
            snprintf(why, sizeof(why), T("Added to Downloads: the server converts it first, at %.0f Mb/s."),
                     convertedBitrate(app) / 1e6);
            appShowMessage(app, why, now);
        } else {
            appNotify(app, NOTICE_DONE, T("Added to Downloads (+ menu)."), now);
        }
        return;
    }
    switch (d->state) {
    case DL_QUEUED:
    case DL_ACTIVE: dlWorkerPause(d->path, true); appShowMessage(app, T("Download paused."), now); break;
    case DL_PAUSED: dlWorkerPause(d->path, false); appShowMessage(app, T("The download goes on."), now); break;
    case DL_FAILED:
    case DL_DELETED: dlWorkerRetry(d->path); appShowMessage(app, T("Downloading it again."), now); break;
    default:
        appShowMessage(app, T("It's on the card: it plays from there. Downloads (+ menu) deletes it."), now);
        break;
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
        if (d && d->state == DL_DELETED)
            d = NULL;  // Deleted from the card: as if never asked for.
        asked += d != NULL;
        whole += d && d->state == DL_DONE;
    }
    *done = whole == n;
    if (*done)
        snprintf(out, size, "%s", T("Season on the card"));
    else if (asked == n)  // All asked for: how many have come.
        snprintf(out, size, T("Season: %d of %d on the card"), whole, n);
    else if (asked)  // Some: L asks for the others.
        snprintf(out, size, "%s", T("Download season"));
    else
        snprintf(out, size, "%s", T("Download season"));
    return out;
}

void appDownloadSeason(App *app, int show, int season, uint64_t now)
{
    int items[SEASON_MAX];
    int n = seasonItems(&app->library, show, season, items, SEASON_MAX), added = 0, skipped = 0;
    char why[128] = "";
    for (int i = 0; i < n; i++) {
        const Download *d = downloadOf(app, app->library.items[items[i]].path);
        if (d && d->state == DL_DELETED) {
            dlWorkerRetry(d->path);
            added++;
            continue;
        }
        if (d)
            continue;
        if (enqueue(app, items[i], why, sizeof(why)))
            added++;
        else
            skipped++;
    }
    char message[200];
    if (added && skipped)
        snprintf(message, sizeof(message), T("%d episodes added to Downloads; %d can't be: %s"), added, skipped, why);
    else if (added)
        snprintf(message, sizeof(message),
                 added == 1 ? T("%d episode added to Downloads (+ menu).")
                            : T("%d episodes added to Downloads (+ menu)."),
                 added);
    else if (skipped)
        snprintf(message, sizeof(message), "%s", why);
    else
        snprintf(message, sizeof(message), "%s", T("This season is in Downloads already (+ menu)."));
    appNotify(app, NOTICE_DONE, message, now);
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

void appDownloadsOverlayOffline(App *app)
{
    bool changed = false;
    for (size_t i = 0; i < app->downloads.count; i++) {
        const Download *d = &app->downloads.items[i];
        DlSyncEvent e;
        if (d->state != DL_DONE || !dlWorkerSyncFind(d->account, d->origin, &e))
            continue;
        changed |= applyState(app, d->origin, e.position_ns, e.played, d->runtime_ns);
        changed |= applyState(app, d->path, e.position_ns, e.played, d->runtime_ns);
    }
    if (changed) {
        saveResume(app);
        appSaveWatched(app);
    }
}

// 3. Once what waited has been told, the servers' libraries are read again: the
// copy kept was read before, and would show the old place until the next refresh.
static void refreshAfterTold(void)
{
    static int last = -1;
    int waiting = dlWorkerSyncWaiting();
    if (last >= 0 && waiting < last && jfSyncAnySignedIn())
        jfSyncRefresh();
    last = waiting;
}

void appDownloadsCatalogCame(App *app, ServerKind kind)
{
    dlWorkerSyncSoon();  // Its server answers: what waits for it can go,
    dlWorkerNetworkBack();  // and downloads waiting for the network go on.
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
