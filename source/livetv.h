// Live TV on a Jellyfin server: its channels, and what's on each now and
// next, from the server's answers. Pure logic; jfrequest.c asks for them.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LIVE_ID_MAX 40
#define LIVE_PROGRAM_ID_MAX 36  // A program's id on the server (32 hex digits).
#define LIVE_NAME_MAX 96
#define LIVE_NUMBER_MAX 12
#define LIVE_SOURCE "live"  // A channel's location: "jellyfin:<channel id>/live".
#define LIVE_CHANNELS_MAX 2000  // A server's, and an IPTV list's too.
#define LIVE_PROGRAMS_PER_CHANNEL 160  // In the guide's hours: six of them in 5-minute programs, and more.
#define LIVE_PROGRAMS_MAX 3000  // Read at once for the guide's hours, all channels together.
#define LIVE_GRID_STEP_S 1800                     // The guide's columns: half hours.
#define LIVE_GRID_SPAN_S (4 * LIVE_GRID_STEP_S)   // What the guide shows at once: two hours.
#define LIVE_GUIDE_DAY_S (24 * 3600)              // How far ahead the guide goes.
#define LIVE_WINDOW_S (6 * 3600)                  // The hours of programs read at once,
#define LIVE_WINDOW_BEFORE_S (2 * 3600)           // starting this much before the view.

// A program's marks: recorded by the server's DVR, part of a series.
enum { LIVE_TIMER = 1, LIVE_SERIES_TIMER = 2, LIVE_IS_SERIES = 4 };

typedef struct {
    char title[LIVE_NAME_MAX];  // "" for none.
    int64_t start, end;         // Seconds since 1970, UTC.
    char id[LIVE_PROGRAM_ID_MAX];  // The server's id for it (to record it), "" for none.
    unsigned char marks;        // LIVE_TIMER and the others.
} LiveProgram;

typedef struct {
    char id[LIVE_ID_MAX];
    char name[LIVE_NAME_MAX];
    char number[LIVE_NUMBER_MAX];
    LiveProgram now, next;
    LiveProgram *programs;  // Those in the guide's hours, by start time: in its guide's pool.
    int program_count;
    const char *logo;  // An IPTV channel's logo's address (in the app's list), or NULL.
} LiveChannel;

typedef struct {
    LiveChannel *channels;  // By number.
    int count, room;        // Channels, and room for them.
    LiveProgram *pool;      // The channels' programs, one channel's after another's.
    int pool_count, pool_room;
    int64_t from, to;       // The hours the programs were read for; 0, 0 for all there are.
} LiveGuide;

void liveGuideInit(LiveGuide *g);
void liveGuideFree(LiveGuide *g);
// The channels of a /LiveTv/Channels answer, replacing those there were
// (with no programs yet). How many, -1 if it can't be read (g unchanged).
int liveGuideSetChannels(LiveGuide *g, const char *json);
// What's on each channel at now_s, next, and its programs in the hours from
// `from` to `to` (0, 0: all of them), from a /LiveTv/Programs answer.
void liveGuideSetPrograms(LiveGuide *g, const char *json, int64_t now_s, int64_t from, int64_t to);
// Only what's on each channel at now_s, from an answer about then (the guide's
// hours being later).
void liveGuideSetNow(LiveGuide *g, const char *json, int64_t now_s);
// Room for channels and programs in an empty guide, to add them one by one.
bool liveGuideReserve(LiveGuide *g, int channels, int programs);
// Adds a channel (its id, name, number, logo, now and next) with count
// programs, copied. False if there's no room.
bool liveGuideAdd(LiveGuide *g, const LiveChannel *c, const LiveProgram *programs, int count);
// The hours to read programs for, so the view from view_from (LIVE_GRID_SPAN_S
// long) is in them: false if the guide's hours already hold it.
bool liveWindowFor(const LiveGuide *g, int64_t now_s, int64_t view_from, int64_t *from, int64_t *to);
// How far into the program now_s is (0-1), -1 if there's none.
double liveProgress(const LiveProgram *p, int64_t now_s);
// "10:30 - 11:00" (or "10:30 AM - 11:00 AM"), in the time utc_offset_s from UTC.
void liveTimeRange(const LiveProgram *p, int64_t utc_offset_s, bool clock_24h, char *out, size_t size);
// "10:30" (or "10:30 AM").
void liveClock(int64_t utc_s, int64_t utc_offset_s, bool clock_24h, char *out, size_t size);

// The guide's cursor is a time on the channel under it: 0 (or a time gone by)
// means now. The program on at t, or else the first after it: an index in
// programs, -1 if there's none.
int liveProgramAt(const LiveChannel *c, int64_t t);
// The cursor after moving dx (-1 or 1) programs from at on c: that program's
// start, or 0 when it's the one on now. Past the programs known, half an hour
// that way; never before now or a day after it.
int64_t liveStep(const LiveChannel *c, int64_t at, int64_t now_s, int dx);
// The cursor moved by delta_s (a finger along the guide), kept within the day.
int64_t liveShift(int64_t at, int64_t now_s, int64_t delta_s);
// Where the guide's view starts: the local half hour of now_s, moved on by
// half hours to keep the cursor at off the last column.
int64_t liveGridStart(int64_t now_s, int64_t at, int64_t utc_offset_s);
// The part of p in the view from `from` (LIVE_GRID_SPAN_S long), spread over
// width pixels from x0. False if none of it is in view.
bool liveGridSpan(const LiveProgram *p, int64_t from, int x0, int width, int *x, int *w);

void liveLocation(char *out, size_t size, const char *channel_id);
bool liveIsLocation(const char *location);
