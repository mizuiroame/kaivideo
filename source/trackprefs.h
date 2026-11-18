// The sound track and subtitles chosen for each video, and for each series
// (the next episode opens with the same), kept in tracks.txt: one
// "key<TAB>sound<TAB>subtitles" line per video path or "series:<name>",
// the latest first. Tracks are kept by name ("Japanese", "English (SDH)",
// "Off"), since their order can change from one episode to the next. Pure
// logic over a file.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define TRACKPREFS_MAX 1000
#define TRACKPREFS_KEY_MAX 520
#define TRACKPREFS_NAME_MAX 64
#define TRACKPREFS_OFF "Off"  // Subtitles turned off.

typedef struct {
    char key[TRACKPREFS_KEY_MAX];
    char sound[TRACKPREFS_NAME_MAX];      // "" if never chosen.
    char subtitles[TRACKPREFS_NAME_MAX];  // A label, TRACKPREFS_OFF, or "" if never chosen.
} TrackPref;

typedef struct {
    TrackPref *items;  // The latest first.
    size_t count;
} TrackPrefs;

void trackPrefsInit(TrackPrefs *t);
void trackPrefsFree(TrackPrefs *t);
void trackPrefsLoad(TrackPrefs *t, const char *file);
bool trackPrefsSave(const TrackPrefs *t, const char *file);

// The choice for a video, else for its series (series may be NULL or ""):
// sound and subtitles ("" for what wasn't chosen). False if neither has one.
bool trackPrefsFind(const TrackPrefs *t, const char *path, const char *series, const char **sound,
                    const char **subtitles);
// Remembers a choice (sound or subtitles; NULL leaves the other as it was)
// for the video and its series.
void trackPrefsRemember(TrackPrefs *t, const char *path, const char *series, const char *sound,
                        const char *subtitles);
