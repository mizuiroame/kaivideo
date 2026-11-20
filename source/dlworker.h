// Fetches the downloads to the card one at a time, in a thread of its own,
// keeping their list on the card; and tells their servers what was watched
// of them without the network once they can be reached again.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "downloads.h"
#include "net.h"

// A conversion's part index (of count) came whole.
typedef void (*DlPartDone)(void *user, int index, int count);

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
    // A conversion the place makes of origin (at most max_bitrate bits a
    // second), sent in parts that join into one file: gives sink the parts
    // from first on, telling part_done after each whole one. NULL if it makes none.
    bool (*fetch_converted)(const char *origin, long long max_bitrate, int first, NetSink sink, DlPartDone part_done,
                            void *user, const volatile bool *cancel, char *error, size_t error_size);
    // True if the last fetch failed because the place refused it (the video
    // is gone, or it may not be downloaded), not because it couldn't be
    // reached: then trying again later won't help. May be NULL.
    bool (*refused)(void);
    // What to keep next to the video once it's whole (its subtitles; for a
    // conversion, which leaves them out, a text track inside it too). May be NULL.
    void (*extras)(const char *origin, const char *video_path, bool converted, const volatile bool *cancel);
} DlSource;

// Says what went wrong where nobody sees it (a rewrap that failed): the app's log.
typedef void (*DlLog)(const char *fmt, ...);

// Reads the lists kept in those files and starts the thread. False if it can't start.
bool dlWorkerStart(const char *list_file, const char *sync_file, DlLog log);
// Stops the thread; what was fetched stays, to go on next time.
void dlWorkerStop(void);

// A copy of the list (free it with dlListFree).
void dlWorkerList(DownloadList *out);
// Grows whenever the list changes: the screens showing it read it again.
unsigned dlWorkerChanges(void);
// Grows whenever a download is whole: the card's library looks again.
unsigned dlWorkerFinished(void);

// Asks for a video, after the others: its file, or (max_bitrate > 0) its
// server's conversion. Its place in the list (that of the same video for the
// same account if it's there), or -1 if it can't be added.
int dlWorkerAdd(const char *origin, const char *account, const VideoName *name, int64_t runtime_ns,
                const char *path, long long max_bitrate);
// Pauses a download (its fetching stops at once) or lets it go on.
void dlWorkerPause(const char *path, bool paused);
// A download that failed, or whose file was deleted, comes again.
void dlWorkerRetry(const char *path);
// Deletes a whole download's file from the card; it stays in the list as a record.
void dlWorkerDeleteFile(const char *path);
// Takes a download out of the list, leaving its file on the card (a video of
// the card's library like any other from then on).
void dlWorkerForget(const char *path);
// Takes every record of a deleted download out of the list. How many.
int dlWorkerForgetDeleted(void);
// Takes a download out of the list and deletes what it put on the card.
void dlWorkerRemove(const char *path);
// While a server's video streams, downloads wait (they'd take its network).
void dlWorkerHold(bool hold);
// While any video plays or opens, files on the card aren't rewrapped (one may be it).
void dlWorkerBusy(bool busy);

// What was watched of a download without its server, to tell it later
// (replacing what waited for that video).
void dlWorkerRecord(const char *account, const char *origin, int64_t position_ns, bool played, int64_t when);
// A server can be reached again: what waits for it is sent now, not at the next try.
void dlWorkerSyncSoon(void);
// The network is back (a server answered): downloads waiting for it go on now.
void dlWorkerNetworkBack(void);
// How many videos' news wait to be told.
int dlWorkerSyncWaiting(void);
// What waits to be told of origin for account, if anything.
bool dlWorkerSyncFind(const char *account, const char *origin, DlSyncEvent *out);
