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
#define LIVE_CHANNELS_MAX 500

typedef struct {
    char title[LIVE_NAME_MAX];  // "" for none.
    int64_t start, end;         // Seconds since 1970, UTC.
} LiveProgram;

typedef struct {
    char id[LIVE_ID_MAX];
    char name[LIVE_NAME_MAX];
    char number[LIVE_NUMBER_MAX];
    LiveProgram now, next;
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
// What's on each channel at now_s, and next, from a /LiveTv/Programs answer.
void liveGuideSetPrograms(LiveGuide *g, const char *json, int64_t now_s);
// How far into the program now_s is (0-1), -1 if there's none.
double liveProgress(const LiveProgram *p, int64_t now_s);
// "10:30 - 11:00" (or "10:30 AM - 11:00 AM"), in the time utc_offset_s from UTC.
void liveTimeRange(const LiveProgram *p, int64_t utc_offset_s, bool clock_24h, char *out, size_t size);

void liveLocation(char *out, size_t size, const char *channel_id);
bool liveIsLocation(const char *location);
