// Videos downloaded to the card, to watch without the network, and what was
// watched of them meanwhile, waiting to be told to where they came from.
//
// A download names where it comes from (its origin: a server's location such
// as "jellyfin:<item>/<source>" now, a share's address later) and who asked
// for it (its account: the kind of source, the server, and the user, so a Plex
// Home profile or another user of the same server is told only of its own).
// The list is kept in downloads.txt, what waits to be told in offline_sync.txt.
// Pure logic over strings and files; dlworker.c does the fetching.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "library.h"
#include "naming.h"

#define DL_FOLDER "KaiVideo Downloads"  // In the library's folder, so the card's library shows them.
#define DL_PART_SUFFIX ".part"          // Added to a download's file until it's whole.
#define DL_LIST_HEADER "KaiVideo downloads 1"
#define DL_SYNC_HEADER "KaiVideo offline sync 1"
#define DL_ACCOUNT_MAX 384
#define DL_ERROR_MAX 96
#define DL_EXT_MAX 6  // An extension of up to five letters or digits.

// DL_DELETED: it was on the card and its file was deleted; it stays in the
// list as a record (to download again, or take out).
typedef enum { DL_QUEUED, DL_ACTIVE, DL_PAUSED, DL_DONE, DL_FAILED, DL_DELETED, DL_STATE_COUNT } DlState;

typedef struct {
    DlState state;
    char origin[LIBRARY_PATH_MAX];
    char account[DL_ACCOUNT_MAX];
    char path[LIBRARY_PATH_MAX];  // On the card once whole; meanwhile it's this + DL_PART_SUFFIX.
    VideoName name;               // As its server names it (the file's name may have lost accents).
    int64_t size;                 // Bytes; -1 until known.
    int64_t done;                 // Bytes on the card so far.
    int64_t runtime_ns;           // 0 if unknown.
    int64_t added;                // When it was asked for, in seconds since 1970.
    char error[DL_ERROR_MAX];     // Why it stopped, "" if it didn't.
    // Converted by its server (what the console can't play as it is), at most
    // this many bits a second; 0 for the original file. A conversion comes in
    // parts joined into one file (done counts only whole parts).
    long long max_bitrate;
    int parts_done, part_count;   // part_count 0 until known.
    bool finishing;               // A conversion's parts are being rewrapped into its file (not kept).
    float finish_fraction;        // How far that rewrap is, 0 to 1 (not kept).
    bool getting_subtitles;       // Its file is whole; its subtitle is being asked for (not kept).
    bool rewrap_tried;            // A whole one still in MPEG-TS was rewrapped, or tried (not kept).
} Download;

typedef struct {
    Download *items;  // In the order they were asked for.
    size_t count, capacity;
} DownloadList;

void dlListInit(DownloadList *list);
void dlListFree(DownloadList *list);
// Reads file into list (empty if there's none; lines that can't be read are
// skipped). Those that were being fetched wait again. False if it isn't such a file.
bool dlListLoad(DownloadList *list, const char *file);
bool dlListSave(const DownloadList *list, const char *file);

// Adds a download, waiting its turn. Its index: the one there already for the
// same origin and account; -1 if out of memory, if a field would break the
// file, or if another download has that path.
int dlListAdd(DownloadList *list, const char *origin, const char *account, const VideoName *name,
              int64_t runtime_ns, const char *path, int64_t added);
// The download of origin for account (NULL: anyone's), or -1.
int dlListFindOrigin(const DownloadList *list, const char *origin, const char *account);
// The download whose file is path, or -1.
int dlListFindPath(const DownloadList *list, const char *path);
void dlListRemove(DownloadList *list, int index);
// What to fetch next: the one being fetched, else the first waiting; -1 if none.
int dlListNext(const DownloadList *list);
// How far a download has come, 0 to 1 (by parts for a conversion, else by bytes).
double dlProgress(const Download *d);

// True for a path a download may be written to or deleted at: a video file on the card, with no
// "." or ".." folder in it. The list file on the card can't point the worker at other files.
bool dlPathIsSafe(const char *path);

// A name the card keeps and lists: plain ASCII (the Switch 2 leaves names with
// other bytes out of its folder lists), accents dropped from Latin letters,
// characters FAT refuses left out (": " becomes " - "), spaces folded, no
// dots or spaces at the ends. "" if nothing is left.
void dlSafeName(const char *text, char *out, size_t size);
// Where a video goes under root: "Films/Title (Year)/Title (Year).ext" or
// "Series/Show/Season 01/Show S01E02 Title.ext". fallback names it if its name
// leaves nothing; ext "" is mkv. False if it doesn't fit.
bool dlPathFor(const VideoName *name, const char *ext, const char *fallback, const char *root, char *out,
               size_t size);
// The extension of a file name, lowercase ("" if it has none, or an odd one).
void dlExtensionOf(const char *file_name, char *out, size_t size);

// "<kind>|<server>|<user>", with tabs and line breaks made spaces.
void dlAccountMake(const char *kind, const char *server, const char *user, char *out, size_t size);
// True if account is of that kind of source ("jellyfin", "plex"...).
bool dlAccountIsKind(const char *account, const char *kind);

// Gives the library's downloaded videos their server's names (before
// libraryBuild): the card's file names may have lost their accents.
void dlApplyNames(Library *lib, const DownloadList *list);

// --- What was watched without the network ---

typedef struct {
    char account[DL_ACCOUNT_MAX];
    char origin[LIBRARY_PATH_MAX];
    int64_t position_ns;  // Where it was left; 0 from the start.
    bool played;          // Watched to the end (or marked so).
    int64_t when;         // Seconds since 1970.
} DlSyncEvent;

typedef struct {
    DlSyncEvent *items;
    size_t count, capacity;
} DlSyncQueue;

void dlSyncInit(DlSyncQueue *q);
void dlSyncFree(DlSyncQueue *q);
// Reads file into q (empty if there's none). False if it isn't such a file.
bool dlSyncLoad(DlSyncQueue *q, const char *file);
bool dlSyncSave(const DlSyncQueue *q, const char *file);
// What's now known of origin for account, replacing what waited for it.
void dlSyncRecord(DlSyncQueue *q, const char *account, const char *origin, int64_t position_ns, bool played,
                  int64_t when);
int dlSyncFind(const DlSyncQueue *q, const char *account, const char *origin);
void dlSyncRemove(DlSyncQueue *q, int index);
// True if what was watched here at when should replace what the server says
// (which it last heard of at server_when; 0 if never).
bool dlSyncIsNewer(int64_t when, int64_t server_when);
