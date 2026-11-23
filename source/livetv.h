// Live TV on a Jellyfin server: its channels, and what's on each now and
// next, from the server's answers. Pure logic; jflive.c asks for them.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LIVE_ID_MAX 40
#define LIVE_NAME_MAX 96
#define LIVE_NUMBER_MAX 12
#define LIVE_SOURCE "live"  // A channel's location: "jellyfin:<channel id>/live".
#define LIVE_CHANNELS_MAX 2000  // A server's, and an IPTV list's too.
#define LIVE_PROGRAMS_PER_CHANNEL 16
#define LIVE_GRID_STEP_S 1800                     // The guide's columns: half hours.
#define LIVE_GRID_SPAN_S (4 * LIVE_GRID_STEP_S)   // What the guide shows at once: two hours.

typedef struct {
    char title[LIVE_NAME_MAX];  // "" for none.
    int64_t start, end;         // Seconds since 1970, UTC.
} LiveProgram;

typedef struct {
    char id[LIVE_ID_MAX];
    char name[LIVE_NAME_MAX];
    char number[LIVE_NUMBER_MAX];
    LiveProgram now, next;
    LiveProgram programs[LIVE_PROGRAMS_PER_CHANNEL];  // Those not over yet, by start time.
    int program_count;
} LiveChannel;

typedef struct {
    LiveChannel *channels;  // By number.
    int count;
} LiveGuide;

void liveGuideInit(LiveGuide *g);
void liveGuideFree(LiveGuide *g);
// The channels of a /LiveTv/Channels answer, replacing those there were
// (with no programs yet). How many, -1 if it can't be read (g unchanged).
int liveGuideSetChannels(LiveGuide *g, const char *json);
// What's on each channel at now_s, next, and the rest of its programs, from a
// /LiveTv/Programs answer.
void liveGuideSetPrograms(LiveGuide *g, const char *json, int64_t now_s);
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
// start, or 0 when it's the one on now. at if there's none that way.
int64_t liveStep(const LiveChannel *c, int64_t at, int64_t now_s, int dx);
// Where the guide's view starts: the local half hour of now_s, moved on by
// half hours to keep the cursor at off the last column.
int64_t liveGridStart(int64_t now_s, int64_t at, int64_t utc_offset_s);
// The part of p in the view from `from` (LIVE_GRID_SPAN_S long), spread over
// width pixels from x0. False if none of it is in view.
bool liveGridSpan(const LiveProgram *p, int64_t from, int x0, int width, int *x, int *w);

void liveLocation(char *out, size_t size, const char *channel_id);
bool liveIsLocation(const char *location);
