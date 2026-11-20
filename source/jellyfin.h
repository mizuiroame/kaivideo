// What KaiVideo knows of a Jellyfin server, without talking to it: the session
// kept on the card, addresses and request headers, the server's answers turned
// into a Library, which videos the console can play, and merging where they
// were left with this console's lists. Pure logic over strings and files;
// jfsync.c does the talking.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "library.h"
#include "net.h"
#include "resume.h"
#include "watched.h"

#define JF_PREFIX "jellyfin:"
#define JF_ROOT "jellyfin:"     // The root of a library that comes from the server.
#define PLEX_PREFIX "plex:"
#define JF_SERVER_MAX 256
#define JF_ID_MAX 40
#define JF_NAME_MAX 64
#define JF_TOKEN_MAX 128
#define JF_URL_MAX 768
#define JF_AUTH_MAX 512
#define JF_PAGE_ITEMS 200
#define JF_SERIES_SOURCE "series"  // A series' picture: "jellyfin:<series id>/series".
#define JF_MAX_BITRATE 25000000ll  // Picture and sound each read the file: twice this goes over Wi-Fi.

typedef struct {
    char server[JF_SERVER_MAX];  // "http://host:8096", no slash at the end.
    char user_id[JF_ID_MAX], user_name[JF_NAME_MAX], token[JF_TOKEN_MAX];
    char device_id[JF_ID_MAX];
} JfSession;

// Where the signed-in user left a video, parallel to the library's items.
typedef struct {
    int64_t position_ns;
    int64_t last_played;  // Seconds since 1970, 0 if never.
    bool played;
} JfUserData;

typedef enum { JF_PLAYABLE, JF_TOO_LARGE, JF_HDR, JF_TOO_HEAVY, JF_FORMAT } JfPlayability;

typedef struct {
    const char *codec, *range, *container;  // As the server names them; may be "".
    int width, bits;
    int64_t bitrate;
} JfVideoFacts;

bool jfIsLocation(const char *path);
// A Jellyfin or Plex location: a video from a server.
bool jfIsRemoteLocation(const char *path);
// The item and media source ids in a location. False if it isn't a good one.
bool jfLocationIds(const char *location, char *item, char *source);
void jfLocationMake(char *out, size_t size, const char *item, const char *source);

// "192.168.0.2" -> "http://192.168.0.2:8096". False if it can't be an address.
bool jfServerNormalize(const char *text, char *out, size_t size);

#define JF_VERDICT_MAX 200
#define JF_DISCOVERY_PORT 7359
#define JF_DISCOVERY_QUESTION "Who is JellyfinServer?"
// What a test of the connection found, from the answer to /System/Info/Public
// (body may be NULL): who answered, or which step failed and what to check.
// True if a Jellyfin server answered.
bool jfConnectionVerdict(NetProblem problem, long status, const char *body, const char *server, char *out, size_t size);
// True for an address on a home network (private ranges, names without a dot, .local).
bool jfAddressIsLocal(const char *server);
// A server's answer to JF_DISCOVERY_QUESTION, which came from sender_ip: its
// address there, with the scheme and port it gave, and its name.
bool jfDiscoveryParse(const char *json, const char *sender_ip, char *server, size_t server_size, char *name,
                      size_t name_size);
// The Authorization header; the token is left out when the session has none.
void jfAuthHeader(const JfSession *s, const char *version, char *out, size_t size);

bool jfSessionLoad(JfSession *s, const char *file);
bool jfSessionSave(const JfSession *s, const char *file);

JfPlayability jfPlayability(const JfVideoFacts *facts);
const char *jfPlayabilityMessage(JfPlayability p);
// Seconds since 1970 from "2024-03-09T21:15:00.123Z", 0 if it isn't a date.
int64_t jfParseDate(const char *text);

#define JF_PLAN_AUDIO 8
#define JF_PLAN_SUBS 16
#define JF_PLAN_PICTURES 8
#define JF_TRANSCODE_URL_MAX 1536
#define JF_TRACK_NAME_MAX 64
#define JF_LIVE_STREAM_MAX 192

typedef struct {
    int index;   // The server's number for the stream.
    char name[JF_TRACK_NAME_MAX];
    bool external;
} JfTrack;

// How the server says a video should be played.
typedef struct {
    bool direct;  // The original file; else the conversion at transcode_url.
    char transcode_url[JF_TRANSCODE_URL_MAX];
    char play_session[JF_ID_MAX + 24];
    char live_stream_id[JF_LIVE_STREAM_MAX];  // Live TV: the stream the server opened, closed after.
    JfTrack audio[JF_PLAN_AUDIO], subs[JF_PLAN_SUBS];  // Subtitles: text ones only.
    int audio_count, sub_count;
    int audio_current;  // An index in audio, -1 if none.
    int sub_default;    // An index in subs, -1 for none.
    // Picture subtitles (PGS, VobSub): the console can't draw them, the server
    // can, onto the video it converts.
    JfTrack pictures[JF_PLAN_PICTURES];
    int picture_count;
    int picture_burned; // The one the server draws, an index in pictures; -1 for none.
    char source_url[JF_TRANSCODE_URL_MAX];  // Live TV read straight from the channel's own HLS, or "".
} JfPlan;

// The body of a PlaybackInfo request. audio_index: the server's number, -1 for
// its choice. subtitle_index: a picture subtitle for it to draw on the video,
// -1 for none (else it would draw one marked default, and convert for it).
void jfPlaybackBody(char *out, size_t size, const char *user_id, long long max_bitrate, int audio_index,
                    int subtitle_index);
// Reads the answer. False if it says nothing playable.
bool jfPlanParse(const char *json, JfPlan *out);
// A picture subtitle's name in the subtitles list: "English - PGSSUB (burned in)".
void jfPictureLabel(const JfTrack *t, char *out, size_t size);
// What playing tells the server (started, how far, stopped).
typedef struct {
    const char *item, *source;  // The location's ids (source is LIVE_SOURCE's "live" for a channel).
    int64_t position_ns;
    bool paused, converted;
    const char *play_session;
    const char *live_stream;    // The channel's stream the server opened, or "".
} JfReport;

// The body of a /Sessions/Playing report. A channel has no media source of
// its own: it's left out, and the stream the server opened for it goes in.
void jfReportBody(char *out, size_t size, const JfReport *r);

// Live TV: the body asking about a channel without the server opening its stream.
void jfLiveInfoBody(char *out, size_t size, const char *user_id);
// From that answer, the channel's own HLS source (an http or https .m3u8 that
// never ends), for the console to read without the server: direct, with
// source_url set. False if it has none such.
bool jfLivePlanParse(const char *json, JfPlan *out);

// Holds a library being read with its user data, which grows with it.
// Parts of a video the server marked (with a plugin such as Intro Skipper).
#define JF_SEGMENTS_MAX 8
#define JF_SEGMENT_TAIL_NS 3000000000ll  // A part this close to its end isn't offered to skip.
typedef enum { JF_SEGMENT_INTRO, JF_SEGMENT_RECAP, JF_SEGMENT_PREVIEW, JF_SEGMENT_OUTRO } JfSegmentKind;
typedef struct {
    JfSegmentKind kind;
    int64_t start_ns, end_ns;
} JfSegment;

// The skippable parts of a /MediaSegments answer, by start. Returns how many (at most max).
int jfSegmentsParse(const char *json, JfSegment *out, int max);
// The part to offer to skip at pos_ns, -1 if none.
int jfSegmentAt(const JfSegment *s, int count, int64_t pos_ns);
// "Skip intro", "Skip credits"...
const char *jfSegmentLabel(JfSegmentKind kind);

#define JF_FAVORITE_SERIES_MAX 512

typedef struct {
    Library library;
    JfUserData *users;  // One per library item.
    size_t capacity;
    char view_ids[LIBRARY_COLLECTIONS][JF_ID_MAX];  // The server's id of each of library.collections.
    int adding_collection;  // The collection pages being added go in; -1 for none.
    char (*favorite_series)[JF_ID_MAX];  // Series the user marked: their episodes count as favorites.
    int favorite_series_count;
} JfCatalog;

void jfCatalogInit(JfCatalog *c);
void jfCatalogFree(JfCatalog *c);
// The user's libraries from /UserViews that hold videos, as collections.
// Returns how many, -1 if it can't be read.
int jfCatalogAddViews(JfCatalog *c, const char *json);
// The series the user marked as favorites (an /Items answer). Returns how many, -1 if unreadable.
int jfCatalogSetFavoriteSeries(JfCatalog *c, const char *json);
// Adds the films and episodes of one /Items answer. Returns how many it held
// (those that can't be used included), -1 if it can't be read. total, if not
// NULL, gets TotalRecordCount.
int jfCatalogAddPage(JfCatalog *c, const char *json, int *total);

// Adds one video with what the server says of it and where the user left it.
bool jfCatalogAddVideo(JfCatalog *c, const char *location, const VideoName *name, int64_t size, int64_t added,
                       const LibraryRemote *remote, const JfUserData *user);

// Keeps a catalog between runs, and reads it back (built). False if there's none.
bool jfCatalogSave(const JfCatalog *c, const char *file);
bool jfCatalogLoad(JfCatalog *c, const char *file);

// Makes resume and watched say what the server says about its videos; what
// they hold about files on the card stays.
void jfMergeUserData(const JfCatalog *c, ResumeList *resume, Watched *watched);
