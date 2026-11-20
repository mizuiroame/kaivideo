#include "dlworker.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#include "jfsync.h"

#define FILE_PATH_MAX 600
#define SAVE_EVERY_S 10          // The list is kept on the card this often while fetching.
#define PROGRESS_EVERY_BYTES (1024 * 1024)
#define TRIES 4                  // Without any progress, before it counts as failed.
#define RETRY_WAIT_S 2           // Doubles with each try.
#define IDLE_WAIT_S 30           // Nothing to do: it looks again this often anyway.
#define SYNC_EVERY_S 60          // What waits to be told is tried again this often.
#define ROOM_MARGIN (64ll * 1024 * 1024)  // Left free on the card.
#define GIB (1024.0 * 1024 * 1024)

extern const DlSource DL_SERVER_SOURCE;
static const DlSource *const SOURCES[] = {&DL_SERVER_SOURCE};

static struct {
    pthread_t thread;
    bool running;
    pthread_mutex_t lock;
    pthread_cond_t wake;
    char list_file[FILE_PATH_MAX], sync_file[FILE_PATH_MAX];
    // Under the lock:
    DownloadList list;
    DlSyncQueue sync;
    bool stop, hold, sync_soon;
    char current[LIBRARY_PATH_MAX];  // The path being fetched, "" if none.
    volatile bool abort;             // The fetch under way stops (paused, removed, held, stopping).
    volatile bool stopping;          // The thread is asked to stop: requests to the servers give up.
    unsigned changes, finished;
    time_t saved_at, synced_at;
} g = {.lock = PTHREAD_MUTEX_INITIALIZER, .wake = PTHREAD_COND_INITIALIZER};

// --- Under the lock ---

static void saveList(void)
{
    dlListSave(&g.list, g.list_file);
    g.saved_at = time(NULL);
}

static void changed(void)
{
    g.changes++;
    saveList();
}

static Download *findPath(const char *path)
{
    int i = dlListFindPath(&g.list, path);
    return i >= 0 ? &g.list.items[i] : NULL;
}

static void setFailed(const char *path, const char *error)
{
    Download *d = findPath(path);
    if (!d || d->state != DL_ACTIVE)  // Paused or held meanwhile: that stands.
        return;
    d->state = DL_FAILED;
    snprintf(d->error, sizeof(d->error), "%s", error);
    changed();
}

// --- Files on the card ---

static void partPath(const char *path, char *out, size_t size)
{
    snprintf(out, size, "%s" DL_PART_SUFFIX, path);
}

// Makes the folders above path.
static void makeFolders(const char *path)
{
    char dir[LIBRARY_PATH_MAX];
    snprintf(dir, sizeof(dir), "%s", path);
    for (char *p = strchr(dir, '/'); p; p = strchr(p + 1, '/')) {
        if (p > dir && p[-1] == ':')
            continue;  // "sdmc:/" is the card itself.
        *p = '\0';
        mkdir(dir, 0777);
        *p = '/';
    }
}

// The video's .srt next to it: its name without the extension, then ".srt".
static void removeWithExtras(const char *path)
{
    char part[FILE_PATH_MAX], srt[FILE_PATH_MAX];
    partPath(path, part, sizeof(part));
    remove(part);
    remove(path);
    snprintf(srt, sizeof(srt), "%s", path);
    char *dot = strrchr(srt, '.');
    if (dot && !strchr(dot, '/')) {
        snprintf(dot, sizeof(srt) - (size_t)(dot - srt), ".srt");
        remove(srt);
    }
}

// Bytes free on the card that holds path; -1 if it can't be told.
static long long roomOn(const char *path)
{
    char root[16];
    const char *colon = strchr(path, ':');
    size_t len = colon ? (size_t)(colon - path) + 2 : 0;
    if (!len || len >= sizeof(root))
        return -1;
    snprintf(root, sizeof(root), "%.*s", (int)len, path);
    struct statvfs st;
    if (statvfs(root, &st) != 0)
        return -1;
    return (long long)st.f_bavail * (long long)st.f_bsize;
}

// --- Fetching one ---

typedef struct {
    FILE *file;
    char path[LIBRARY_PATH_MAX];
    int64_t offset;           // Where the fetch under way began.
    bool restart;             // The place sent the file from its start, not from offset.
    int64_t done, reported;
    long long total;  // -1 until known.
    bool checked_room, no_room, write_failed;
    long long room_needed;
} Fetch;

static void reportProgress(Fetch *f, bool force)
{
    if (!force && f->done - f->reported < PROGRESS_EVERY_BYTES)
        return;
    f->reported = f->done;
    pthread_mutex_lock(&g.lock);
    Download *d = findPath(f->path);
    if (d) {
        d->done = f->done;
        if (f->total >= 0)
            d->size = f->total;
        g.changes++;
        if (time(NULL) - g.saved_at >= SAVE_EVERY_S)
            saveList();
    }
    pthread_mutex_unlock(&g.lock);
}

static bool takeBytes(const void *data, size_t size, void *user)
{
    Fetch *f = user;
    if (g.abort)
        return false;
    if (f->offset > 0 && f->total < 0 && f->done == f->offset) {  // No Content-Range: not the part asked for.
        f->restart = true;
        return false;
    }
    if (!f->checked_room && f->total >= 0) {  // Once the size is known: does it fit?
        f->checked_room = true;
        long long room = roomOn(f->path);
        long long needed = f->total - f->done + ROOM_MARGIN;
        if (room >= 0 && room < needed) {
            f->no_room = true;
            f->room_needed = needed - room;
            return false;
        }
    }
    if (fwrite(data, 1, size, f->file) != size) {
        f->write_failed = true;
        return false;
    }
    f->done += (int64_t)size;
    reportProgress(f, false);
    return true;
}

// Opens the part fetched so far, cut back to what the list says is there.
static FILE *openPart(const char *path, int64_t *done)
{
    char part[FILE_PATH_MAX];
    partPath(path, part, sizeof(part));
    makeFolders(path);
    FILE *f = *done > 0 ? fopen(part, "r+b") : NULL;
    if (f && fseeko(f, 0, SEEK_END) == 0) {
        off_t have = ftello(f);
        if (have < *done)
            *done = have;
        if (ftruncate(fileno(f), (off_t)*done) != 0 || fseeko(f, (off_t)*done, SEEK_SET) != 0) {
            fclose(f);
            f = NULL;
        }
    } else if (f) {
        fclose(f);
        f = NULL;
    }
    if (!f) {
        *done = 0;
        f = fopen(part, "wb");
    }
    return f;
}

static const DlSource *sourceFor(const char *origin)
{
    for (size_t i = 0; i < sizeof(SOURCES) / sizeof(SOURCES[0]); i++)
        if (SOURCES[i]->handles(origin))
            return SOURCES[i];
    return NULL;
}

static void waitSeconds(int seconds)
{
    for (int i = 0; i < seconds * 10 && !g.abort; i++)
        nanosleep(&(struct timespec){0, 100000000L}, NULL);
}

// Puts the whole file on the card. False, with why in error, if it can't
// (or it was stopped: then error is "").
static bool fetchAll(const DlSource *src, const Download *d, Fetch *f, char *error, size_t error_size)
{
    error[0] = '\0';
    if (d->size > 0 && f->done >= d->size)
        return true;  // It came whole before it was stopped last time.
    int tries = 0;
    while (!g.abort) {
        int64_t offset = f->done;
        f->total = -1;
        f->offset = offset;
        f->restart = false;
        error[0] = '\0';
        bool ok = src->fetch(d->origin, offset, takeBytes, f, &f->total, &g.abort, error, error_size);
        fflush(f->file);
        if (f->no_room) {
            snprintf(error, error_size, "The card needs %.1f GB more free for it.", f->room_needed / GIB);
            return false;
        }
        if (f->write_failed) {
            snprintf(error, error_size, "It couldn't be written to the card.");
            return false;
        }
        if (ok && !f->restart && f->total >= 0 && f->done >= f->total)
            return true;
        if (ok && !f->restart && f->total < 0 && f->done > 0)
            return true;  // The place doesn't say how long: its end is the file's.
        if (g.abort)
            break;
        if (f->restart) {  // It sent the file from the start: again, whole, from 0.
            f->done = f->reported = 0;
            if (ftruncate(fileno(f->file), 0) != 0 || fseeko(f->file, 0, SEEK_SET) != 0) {
                snprintf(error, error_size, "It couldn't be written to the card.");
                return false;
            }
            continue;
        }
        tries = f->done > offset ? 0 : tries + 1;  // Progress gives it its tries back.
        if (tries >= TRIES) {
            if (!error[0])
                snprintf(error, error_size, "The server stopped sending it.");
            return false;
        }
        waitSeconds(RETRY_WAIT_S << tries);
    }
    error[0] = '\0';  // Stopped (paused, held, closing): not a failure.
    return false;
}

// Fetches d (a copy; the list may change meanwhile).
static void runDownload(const Download *d)
{
    char error[DL_ERROR_MAX];
    const DlSource *src = sourceFor(d->origin);
    if (!src) {
        pthread_mutex_lock(&g.lock);
        setFailed(d->path, "KaiVideo can't download from there.");
        pthread_mutex_unlock(&g.lock);
        return;
    }
    Fetch f = {.done = d->done, .total = -1};
    snprintf(f.path, sizeof(f.path), "%s", d->path);
    f.file = openPart(d->path, &f.done);
    f.reported = f.done;
    bool whole = f.file && fetchAll(src, d, &f, error, sizeof(error));
    if (!f.file)
        snprintf(error, sizeof(error), "It couldn't be written to the card.");
    if (f.file)
        fclose(f.file);
    if (whole) {
        char part[FILE_PATH_MAX];
        partPath(d->path, part, sizeof(part));
        remove(d->path);
        if (rename(part, d->path) != 0) {
            whole = false;
            snprintf(error, sizeof(error), "It couldn't be written to the card.");
        } else if (src->extras) {
            src->extras(d->origin, d->path, &g.abort);
        }
    }
    reportProgress(&f, true);
    pthread_mutex_lock(&g.lock);
    Download *now = findPath(d->path);
    if (!now) {
        removeWithExtras(d->path);  // Taken out of the list meanwhile.
    } else if (whole) {
        now->state = DL_DONE;
        now->error[0] = '\0';
        if (now->size < 0)
            now->size = f.done;
        g.finished++;
        changed();
    } else if (error[0]) {
        setFailed(d->path, error);
    } else if (now->state == DL_ACTIVE) {  // Held, or stopping: it goes on later.
        now->state = DL_QUEUED;
        changed();
    } else {
        changed();
    }
    g.current[0] = '\0';  // Under the same lock: a removal from now on deletes the files itself.
    pthread_mutex_unlock(&g.lock);
}

// --- Telling the servers ---

// Sends what waits, one at a time without the lock. Under the lock on entry and exit.
static void sendWaiting(void)
{
    g.sync_soon = false;
    g.synced_at = time(NULL);
    for (size_t i = 0; i < g.sync.count && !g.stop;) {
        DlSyncEvent e = g.sync.items[i];
        pthread_mutex_unlock(&g.lock);
        DlSendResult r = jfSyncSendOffline(e.origin, e.account, e.position_ns, e.played, e.when, &g.stopping);
        pthread_mutex_lock(&g.lock);
        int at = dlSyncFind(&g.sync, e.account, e.origin);
        bool same = at >= 0 && g.sync.items[at].when == e.when;  // Not replaced meanwhile.
        if (r != DL_SEND_LATER && same) {
            dlSyncRemove(&g.sync, at);
            dlSyncSave(&g.sync, g.sync_file);
            continue;
        }
        i++;
    }
}

static bool syncDue(void)
{
    return g.sync.count > 0 && jfSyncRunning() && (g.sync_soon || time(NULL) - g.synced_at >= SYNC_EVERY_S);
}

static void *workerThread(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&g.lock);
    while (!g.stop) {
        if (syncDue()) {
            sendWaiting();
            continue;
        }
        int next = g.hold ? -1 : dlListNext(&g.list);
        if (next < 0) {
            struct timespec until;
            clock_gettime(CLOCK_REALTIME, &until);
            until.tv_sec += g.sync.count ? SYNC_EVERY_S : IDLE_WAIT_S;
            pthread_cond_timedwait(&g.wake, &g.lock, &until);
            continue;
        }
        Download d = g.list.items[next];
        g.list.items[next].state = DL_ACTIVE;
        g.list.items[next].error[0] = '\0';
        snprintf(g.current, sizeof(g.current), "%s", d.path);
        g.abort = false;
        changed();
        pthread_mutex_unlock(&g.lock);
        runDownload(&d);
        pthread_mutex_lock(&g.lock);
    }
    pthread_mutex_unlock(&g.lock);
    return NULL;
}

// --- From other threads ---

bool dlWorkerStart(const char *list_file, const char *sync_file)
{
    if (g.running)
        return true;
    snprintf(g.list_file, sizeof(g.list_file), "%s", list_file);
    snprintf(g.sync_file, sizeof(g.sync_file), "%s", sync_file);
    dlListInit(&g.list);
    dlSyncInit(&g.sync);
    dlListLoad(&g.list, g.list_file);
    dlSyncLoad(&g.sync, g.sync_file);
    g.stop = false;
    g.stopping = false;
    g.sync_soon = true;
    if (pthread_create(&g.thread, NULL, workerThread, NULL) != 0) {
        dlListFree(&g.list);
        dlSyncFree(&g.sync);
        return false;
    }
    g.running = true;
    return true;
}

void dlWorkerStop(void)
{
    if (!g.running)
        return;
    pthread_mutex_lock(&g.lock);
    g.stop = true;
    g.stopping = true;
    g.abort = true;
    pthread_cond_broadcast(&g.wake);
    pthread_mutex_unlock(&g.lock);
    pthread_join(g.thread, NULL);
    saveList();
    dlSyncSave(&g.sync, g.sync_file);
    dlListFree(&g.list);
    dlSyncFree(&g.sync);
    g.running = false;
}

void dlWorkerList(DownloadList *out)
{
    dlListInit(out);
    pthread_mutex_lock(&g.lock);
    if (g.list.count && (out->items = malloc(g.list.count * sizeof(Download))) != NULL) {
        memcpy(out->items, g.list.items, g.list.count * sizeof(Download));
        out->count = out->capacity = g.list.count;
    }
    pthread_mutex_unlock(&g.lock);
}

unsigned dlWorkerChanges(void)
{
    pthread_mutex_lock(&g.lock);
    unsigned n = g.changes;
    pthread_mutex_unlock(&g.lock);
    return n;
}

unsigned dlWorkerFinished(void)
{
    pthread_mutex_lock(&g.lock);
    unsigned n = g.finished;
    pthread_mutex_unlock(&g.lock);
    return n;
}

int dlWorkerAdd(const char *origin, const char *account, const VideoName *name, int64_t runtime_ns,
                const char *path)
{
    if (!g.running)
        return -1;
    pthread_mutex_lock(&g.lock);
    int at = dlListAdd(&g.list, origin, account, name, runtime_ns, path, (int64_t)time(NULL));
    if (at >= 0) {
        changed();
        pthread_cond_broadcast(&g.wake);
    }
    pthread_mutex_unlock(&g.lock);
    return at;
}

// Stops the fetch under way if it's path's. Under the lock.
static void abortIfCurrent(const char *path)
{
    if (!strcmp(g.current, path))
        g.abort = true;
}

void dlWorkerPause(const char *path, bool paused)
{
    pthread_mutex_lock(&g.lock);
    Download *d = findPath(path);
    if (d && paused && (d->state == DL_QUEUED || d->state == DL_ACTIVE)) {
        d->state = DL_PAUSED;
        abortIfCurrent(path);
        changed();
    } else if (d && !paused && d->state == DL_PAUSED) {
        d->state = DL_QUEUED;
        changed();
        pthread_cond_broadcast(&g.wake);
    }
    pthread_mutex_unlock(&g.lock);
}

void dlWorkerRetry(const char *path)
{
    pthread_mutex_lock(&g.lock);
    Download *d = findPath(path);
    if (d && d->state == DL_FAILED) {
        d->state = DL_QUEUED;
        d->error[0] = '\0';
        changed();
        pthread_cond_broadcast(&g.wake);
    }
    pthread_mutex_unlock(&g.lock);
}

void dlWorkerRemove(const char *path)
{
    pthread_mutex_lock(&g.lock);
    int i = dlListFindPath(&g.list, path);
    if (i >= 0) {
        bool fetching = !strcmp(g.current, path);
        dlListRemove(&g.list, i);
        if (fetching)
            g.abort = true;  // The thread deletes its file once it lets go of it.
        else
            removeWithExtras(path);
        changed();
    }
    pthread_mutex_unlock(&g.lock);
}

void dlWorkerHold(bool hold)
{
    pthread_mutex_lock(&g.lock);
    if (g.hold != hold) {
        g.hold = hold;
        if (hold && g.current[0])
            g.abort = true;
        pthread_cond_broadcast(&g.wake);
    }
    pthread_mutex_unlock(&g.lock);
}

void dlWorkerRecord(const char *account, const char *origin, int64_t position_ns, bool played, int64_t when)
{
    pthread_mutex_lock(&g.lock);
    if (g.running) {
        dlSyncRecord(&g.sync, account, origin, position_ns, played, when);
        dlSyncSave(&g.sync, g.sync_file);
        g.sync_soon = true;
        pthread_cond_broadcast(&g.wake);
    }
    pthread_mutex_unlock(&g.lock);
}

void dlWorkerSyncSoon(void)
{
    pthread_mutex_lock(&g.lock);
    g.sync_soon = true;
    pthread_cond_broadcast(&g.wake);
    pthread_mutex_unlock(&g.lock);
}

int dlWorkerSyncWaiting(void)
{
    pthread_mutex_lock(&g.lock);
    int n = (int)g.sync.count;
    pthread_mutex_unlock(&g.lock);
    return n;
}

bool dlWorkerSyncFind(const char *account, const char *origin, DlSyncEvent *out)
{
    pthread_mutex_lock(&g.lock);
    int at = dlSyncFind(&g.sync, account, origin);
    if (at >= 0)
        *out = g.sync.items[at];
    pthread_mutex_unlock(&g.lock);
    return at >= 0;
}
