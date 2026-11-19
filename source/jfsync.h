// Talking to the Jellyfin server, in a thread of its own so the screens stay
// live: signing in with Quick Connect, reading the library, and telling the
// server what's being watched.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
    bool refreshing;
    int loaded, total; // Videos read so far while refreshing.
} JfStatus;

typedef enum { JF_REPORT_START, JF_REPORT_PROGRESS, JF_REPORT_STOP } JfReportKind;

// Starts the network and the thread (once), reading the session from its file.
// load_library: also reads the kept library and asks for a new one. False if
// the network can't be used.
bool jfSyncStart(const char *session_file, const char *catalog_file, const char *version, bool load_library);
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

// True once for each library read: c gets it (the caller frees it with jfCatalogFree).
bool jfSyncTakeCatalog(JfCatalog *c);

void jfSyncReport(JfReportKind kind, const char *location, int64_t position_ns, bool paused);
void jfSyncSetPlayed(const char *location, bool played);
// Live TV's channels and what's on now and next, into out (replacing its
// channels), from any thread. How many channels, -1 if the server didn't answer.
int jfSyncFetchLive(LiveGuide *out, int64_t now_s);
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

// Asks the server how to play a location, from any thread. audio_index: the
// server's number for the sound track, -1 for its choice.
// Live TV: the channel's own HLS source, asked for without the server opening
// the channel (out is direct, with source_url). False if it has none such.
bool jfSyncLiveSource(const char *location, JfPlan *out);
bool jfSyncResolve(const char *location, long long max_bitrate, int audio_index, JfPlan *out, char *error,
                   size_t error_size);
// The address and header for a conversion the server gave (url as in the plan).
void jfSyncConvertedRequest(const char *url, char *full, size_t full_size, char *authorization, size_t auth_size);
// A text subtitle track of a location, as SubRip (free with netResponseFree).
bool jfSyncFetchSubtitle(const char *location, int stream_index, const volatile bool *cancel, NetResponse *out);
// What the reports about the video playing say about how it plays.
void jfSyncSetPlayback(const char *play_session, bool converted);

// The address and header for reading a location's file; for its picture.
bool jfSyncStreamRequest(const char *location, char *url, size_t url_size, char *authorization, size_t auth_size);
bool jfSyncImageRequest(const char *location, const char *kind, int max_width, char *url, size_t url_size,
                        char *authorization, size_t auth_size);
