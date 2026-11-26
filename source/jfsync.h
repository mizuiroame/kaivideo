// Talking to the Jellyfin, Emby or Plex server, in a thread of its own so the screens stay
// live: signing in with Quick Connect, reading the library, and telling the
// server what's being watched.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dvr.h"
#include "jellyfin.h"
#include "livetv.h"
#include "net.h"

typedef enum {
    JF_SIGNED_OUT,
    JF_SIGN_IN_STARTING,
    JF_SIGN_IN_WAITING,  // The code is showing, for approval on another device.
    JF_SIGNED_IN,
} JfSignState;

typedef struct {
    JfSignState state;
    char server[JF_SERVER_MAX], user[JF_NAME_MAX];
    char code[16];
    char error[128];   // Why the last thing asked failed; "" if it didn't.
    bool linked;       // Plex: the code was approved, the server is being found.
    bool refreshing;
    int loaded, total; // Videos read so far while refreshing.
} JfStatus;

typedef enum { JF_REPORT_START, JF_REPORT_PROGRESS, JF_REPORT_STOP } JfReportKind;

// Starts the network and the thread (once), reading the session from its file.
// load_library: also reads the kept library and asks for a new one. False if
// the network can't be used.
// Where each kind of server's session and library are kept.
typedef struct {
    const char *jellyfin, *jellyfin_library, *emby, *emby_library, *plex, *plex_library;
} JfSyncFiles;

// kind: the server used (each kind keeps its own session and library).
bool jfSyncStart(const JfSyncFiles *files, const char *version, bool load_library, ServerKind kind);
// Changes which server Settings shows (signing in and out are for it). Every
// server signed in has its library read.
void jfSyncUseKind(ServerKind kind);
ServerKind jfSyncKind(void);
bool jfSyncSignedIn(ServerKind kind);
bool jfSyncAnySignedIn(void);
// The user signed in to a server ("" if none), from any thread.
void jfSyncUserName(ServerKind kind, char *out, size_t size);
// The uuid of the Plex Home profile in use ("" if none is known).
void jfSyncPlexProfile(char *out, size_t size);
// Sends what's waiting to be told and stops.
void jfSyncStop(void);
bool jfSyncRunning(void);

void jfSyncGetStatus(JfStatus *out);
// False if text can't be a server's address.
bool jfSyncSetServer(const char *text);
void jfSyncSignIn(void);
// With a user name and password. The password is only kept until it's sent.
void jfSyncSignInWithPassword(const char *user, const char *password);
void jfSyncCancelSignIn(void);
void jfSyncSignOut(void);
void jfSyncRefresh(void);

// True once for each library a server's read: c gets it (built; the caller
// frees it with jfCatalogFree). Each server's comes on its own.
bool jfSyncTakeCatalog(ServerKind kind, JfCatalog *c);

void jfSyncReport(JfReportKind kind, const char *location, int64_t position_ns, bool paused);
void jfSyncSetPlayed(const char *location, bool played);
// Live TV's channels, what's on now and next, and their programs from `from`
// to `to`, marked as the DVR's timers say, into out (replacing its channels),
// from any thread. How many channels, -1 if the server didn't answer.
int jfSyncFetchLive(LiveGuide *out, int64_t now_s, int64_t from, int64_t to);

// --- The Jellyfin server's DVR (jfdvr.c), from any thread; each call waits ---

typedef enum {
    JF_DVR_DONE,
    JF_DVR_NOT_ALLOWED,  // The user may not (recording needs Live TV management; deleting, the right to).
    JF_DVR_REFUSED,      // The server said no (no recording folder, a program gone...).
    JF_DVR_NO_ANSWER,
} JfDvrResult;

// Its timers (net: a client of the caller's, or NULL for one of its own). How many, -1 without an answer.
int jfSyncDvrTimers(NetClient *net, DvrTimer *out, int max);
// Its timers, series and recordings. False if any didn't come (out has what did).
bool jfSyncDvrFetch(DvrLists *out);
// Records a program (series: every episode of its series).
JfDvrResult jfSyncDvrRecord(const char *program_id, bool series);
JfDvrResult jfSyncDvrCancel(const char *timer_id);
JfDvrResult jfSyncDvrCancelSeries(const char *series_id);
JfDvrResult jfSyncDvrDelete(const char *recording_id);
// Live TV: the server can close the channel's stream it opened (JfPlan.live_stream_id).
void jfSyncCloseLiveStream(const char *location, const char *live_stream_id);
// The address and header for a location's trickplay: its sheet (a JPEG) of
// pictures width wide, or with width 0 the item's answer that says what there is.
bool jfSyncTrickplayRequest(const char *location, int width, int sheet, char *url, size_t url_size,
                            char *authorization, size_t auth_size);
// The parts of a location the server marked to skip (intro, credits...), from
// any thread. How many, 0 if none or the server didn't answer.
int jfSyncFetchSegments(const char *location, JfSegment *out, int max);
// Marks a film (its location) or a series ("jellyfin:<series id>/series") as a favorite, or not.
void jfSyncSetFavorite(const char *location, bool favorite);
// The server can stop converting the video that was playing.
void jfSyncEndConversion(const char *location);

// Live TV: the channel's own HLS source, asked for without the server opening
// the channel (out is direct, with source_url). False if it has none such.
// The request stops when *cancel (may be NULL) becomes true.
bool jfSyncLiveSource(const char *location, JfPlan *out, const volatile bool *cancel);
// Asks the server how to play a location, from any thread. audio_index: the
// server's number for the sound track, -1 for its choice; subtitle_index: a
// picture subtitle for it to draw on the video, -1 for none. Stops when
// *cancel (may be NULL) becomes true.
bool jfSyncResolve(const char *location, long long max_bitrate, int audio_index, int subtitle_index, JfPlan *out,
                   char *error, size_t error_size, const volatile bool *cancel);
// A conversion of one sound track of a location alone, as HLS (not for Live
// TV): its media list's address and header, and its own play session.
bool jfSyncAudioConversion(const char *location, int audio_index, char *url, size_t url_size, char *authorization,
                           size_t auth_size, char *play_session, size_t session_size);
// Ends such a conversion on the server.
void jfSyncEndConversionOf(const char *location, const char *play_session);
// The address and header for a conversion the server of location gave (url as in the plan).
void jfSyncConvertedRequest(const char *location, const char *url, char *full, size_t full_size, char *authorization,
                            size_t auth_size);
// How long a subtitle may take: the server may have to read it out of the
// whole file first, which for a 21 GB film took it 3.5 minutes.
#define JF_SUBTITLE_PLAY_WAIT_S 120L       // While the video plays.
#define JF_SUBTITLE_DOWNLOAD_WAIT_S 900L   // For a download (stopped by cancel when KaiVideo ends).
// A text subtitle track of a location, as SubRip (free with netResponseFree),
// waiting at most wait_s for it (a Plex server: its own wait).
bool jfSyncFetchSubtitle(const char *location, int stream_index, long wait_s, const volatile bool *cancel,
                         NetResponse *out);
// What the reports about the video playing say about how it plays, and the
// channel's stream the server opened (NULL or "" for none).
void jfSyncSetPlayback(const char *play_session, bool converted, const char *live_stream);

// --- Downloads (jfoffline.c) ---

// Who a server's location is watched as now: "<kind>|<server>|<user>"
// (downloads.h's account). False, with "", if its server isn't signed in.
bool jfSyncAccountOf(const char *location, char *out, size_t size);

typedef enum {
    DL_SEND_DONE,          // The server took it.
    DL_SEND_SERVER_NEWER,  // The server heard of the video later (watched elsewhere): what it says stays.
    DL_SEND_DROPPED,       // The server doesn't have the video any more, or refused: nothing to try again.
    DL_SEND_LATER,         // No answer, or its user isn't signed in now: try again later.
} DlSendResult;

// Tells the server where a video downloaded as account was left without the
// network (when: seconds since 1970), unless it heard of it later. Waits for
// the answer; the requests stop when *cancel (may be NULL) becomes true.
DlSendResult jfSyncSendOffline(const char *location, const char *account, int64_t position_ns, bool played,
                               int64_t when, const volatile bool *cancel);

// The address and header for reading a location's file; for its picture.
bool jfSyncStreamRequest(const char *location, char *url, size_t url_size, char *authorization, size_t auth_size);
bool jfSyncImageRequest(const char *location, const char *kind, int max_width, char *url, size_t url_size,
                        char *authorization, size_t auth_size);
