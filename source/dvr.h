// Recording Live TV with the server's DVR (Jellyfin): what it will record
// (timers), the series it records, and what it recorded, from its answers;
// what can be done with a program of the guide; and the marks the guide shows.
// Pure logic; jfdvr.c asks the server, app_dvr.c shows it.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "livetv.h"

#define DVR_ID_MAX 40
#define DVR_TIMERS_MAX 300
#define DVR_SERIES_MAX 100
#define DVR_RECORDINGS_MAX 300

typedef enum {
    DVR_SCHEDULED,  // Waiting for its program.
    DVR_RECORDING,  // Recording now.
    DVR_CONFLICT,   // The server has no tuner free for it.
    DVR_FAILED,
} DvrTimerState;

typedef struct {
    char id[DVR_ID_MAX], program_id[DVR_ID_MAX], series_id[DVR_ID_MAX], channel_id[DVR_ID_MAX];
    char name[LIVE_NAME_MAX], channel[LIVE_NAME_MAX];
    int64_t start, end;  // Seconds since 1970, UTC.
    DvrTimerState state;
} DvrTimer;

typedef struct {
    char id[DVR_ID_MAX];
    char name[LIVE_NAME_MAX], channel[LIVE_NAME_MAX];
    bool any_channel, new_only;
} DvrSeries;

typedef struct {
    char id[DVR_ID_MAX];
    char name[LIVE_NAME_MAX], episode[LIVE_NAME_MAX], channel[LIVE_NAME_MAX];
    int64_t start, end;  // When it was on; 0 if the server didn't say.
    bool recording;      // Still being recorded.
    bool can_delete;     // The user may delete it.
} DvrRecording;

// Everything the DVR has, as last read.
typedef struct {
    DvrTimer timers[DVR_TIMERS_MAX];  // By start.
    int timer_count;
    DvrSeries series[DVR_SERIES_MAX];  // By name.
    int series_count;
    DvrRecording recordings[DVR_RECORDINGS_MAX];  // The latest first.
    int recording_count;
} DvrLists;

// /LiveTv/Timers: the timers not over or cancelled. How many, -1 if it can't be read.
int dvrParseTimers(const char *json, DvrTimer *out, int max);
// /LiveTv/SeriesTimers. How many, -1 if it can't be read.
int dvrParseSeries(const char *json, DvrSeries *out, int max);
// /LiveTv/Recordings. How many, -1 if it can't be read.
int dvrParseRecordings(const char *json, DvrRecording *out, int max);

// The timer recording program_id, or NULL.
const DvrTimer *dvrTimerFor(const DvrLists *d, const char *program_id);
// The guide's programs marked as the timers say (LIVE_TIMER, LIVE_SERIES_TIMER),
// replacing the marks they had; LIVE_IS_SERIES stays.
void dvrMarkGuide(LiveGuide *g, const DvrTimer *timers, int count);

// What can be done with a program of the guide.
typedef enum {
    DVR_DO_RECORD,         // Record this program.
    DVR_DO_RECORD_SERIES,  // Record every episode of its series.
    DVR_DO_CANCEL,         // Don't record it.
    DVR_DO_CANCEL_SERIES,  // Stop recording its series.
    DVR_DO_COUNT,
} DvrAction;

// The choices for program p at now_s (none for a program over, or without the
// server's id), into out; how many.
int dvrChoices(const LiveProgram *p, int64_t now_s, DvrAction out[DVR_DO_COUNT]);
// Its label ("Record this program").
const char *dvrActionLabel(DvrAction a);

// The request to record, made from the server's defaults for the program
// (/LiveTv/Timers/Defaults): its answer as it is, if it's the program's. The
// same body schedules the program or its series.
bool dvrTimerBody(const char *defaults_json, const char *program_id, char *out, size_t size);

// A recording's location, to play it as the server's other videos.
void dvrRecordingLocation(const DvrRecording *r, char *out, size_t size);
// Its length in whole minutes (0 if unknown).
int dvrMinutes(int64_t start, int64_t end);
