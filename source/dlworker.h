// Fetches the downloads to the card one at a time, in a thread of its own,
// keeping their list on the card; and tells their servers what was watched
// of them without the network once they can be reached again.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "downloads.h"
#include "net.h"

// A kind of place videos come from: the media servers now (dlserver.c),
// network shares later. Called from the download thread only.
typedef struct {
    bool (*handles)(const char *origin);
    // Gives sink origin's bytes from offset on, until its end, *cancel, or the
    // sink says stop. total gets the whole file's size (before the first bytes
    // come, as an answer's headers say it); it stays -1 if the
    // place doesn't say, and then the bytes given start at 0 whatever offset was.
    bool (*fetch)(const char *origin, int64_t offset, NetSink sink, void *user, long long *total,
                  const volatile bool *cancel, char *error, size_t error_size);
    // What to keep next to the video once it's whole (its subtitles). May be NULL.
    void (*extras)(const char *origin, const char *video_path, const volatile bool *cancel);
} DlSource;

// Reads the lists kept in those files and starts the thread. False if it can't start.
bool dlWorkerStart(const char *list_file, const char *sync_file);
// Stops the thread; what was fetched stays, to go on next time.
void dlWorkerStop(void);

// A copy of the list (free it with dlListFree).
void dlWorkerList(DownloadList *out);
// Grows whenever the list changes: the screens showing it read it again.
unsigned dlWorkerChanges(void);
// Grows whenever a download is whole: the card's library looks again.
unsigned dlWorkerFinished(void);

// Asks for a video, after the others. Its place in the list (that of the same
// video for the same account if it's there), or -1 if it can't be added.
int dlWorkerAdd(const char *origin, const char *account, const VideoName *name, int64_t runtime_ns,
                const char *path);
// Pauses a download (its fetching stops at once) or lets it go on.
void dlWorkerPause(const char *path, bool paused);
// A download that failed tries again.
void dlWorkerRetry(const char *path);
// Takes a download out of the list and deletes what it put on the card.
void dlWorkerRemove(const char *path);
// While a server's video streams, downloads wait (they'd take its network).
void dlWorkerHold(bool hold);

// What was watched of a download without its server, to tell it later
// (replacing what waited for that video).
void dlWorkerRecord(const char *account, const char *origin, int64_t position_ns, bool played, int64_t when);
// A server can be reached again: what waits for it is sent now, not at the next try.
void dlWorkerSyncSoon(void);
// How many videos' news wait to be told.
int dlWorkerSyncWaiting(void);
// What waits to be told of origin for account, if anything.
bool dlWorkerSyncFind(const char *account, const char *origin, DlSyncEvent *out);
