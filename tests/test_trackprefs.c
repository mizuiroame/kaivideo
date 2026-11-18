// The sound track and subtitles kept for videos and series.
#include <stdio.h>
#include <string.h>

#include "check.h"
#include "trackprefs.h"

void testTrackPrefs(void)
{
    static TrackPrefs t;
    trackPrefsInit(&t);
    const char *sound, *subs;
    CHECK(!trackPrefsFind(&t, "sdmc:/V/Rook/e1.mkv", "Rook", &sound, &subs));

    // Chosen in one episode: that episode, and the series' next ones.
    trackPrefsRemember(&t, "sdmc:/V/Rook/e1.mkv", "Rook", "Japanese", NULL);
    trackPrefsRemember(&t, "sdmc:/V/Rook/e1.mkv", "Rook", NULL, "English (SDH)");
    CHECK(trackPrefsFind(&t, "sdmc:/V/Rook/e1.mkv", "Rook", &sound, &subs) && strcmp(sound, "Japanese") == 0 &&
          strcmp(subs, "English (SDH)") == 0);
    CHECK(trackPrefsFind(&t, "sdmc:/V/Rook/e2.mkv", "Rook", &sound, &subs) && strcmp(sound, "Japanese") == 0);

    // A later choice in another episode changes the series', not e1's own.
    trackPrefsRemember(&t, "sdmc:/V/Rook/e2.mkv", "Rook", NULL, TRACKPREFS_OFF);
    CHECK(trackPrefsFind(&t, "sdmc:/V/Rook/e3.mkv", "Rook", &sound, &subs) && strcmp(subs, TRACKPREFS_OFF) == 0);
    CHECK(trackPrefsFind(&t, "sdmc:/V/Rook/e1.mkv", "Rook", &sound, &subs) && strcmp(subs, "English (SDH)") == 0);

    // A film: only its own.
    trackPrefsRemember(&t, "sdmc:/V/moon.mkv", NULL, "Español", NULL);
    CHECK(trackPrefsFind(&t, "sdmc:/V/moon.mkv", NULL, &sound, &subs) && strcmp(sound, "Español") == 0 &&
          subs[0] == '\0');
    CHECK(!trackPrefsFind(&t, "sdmc:/V/other.mkv", "", &sound, &subs));

    // Kept in the file, in the same order; broken lines skipped.
    const char *file = TMP_ROOT "/tracks.txt";
    CHECK(trackPrefsSave(&t, file));
    FILE *f = fopen(file, "a");
    if (f) {
        fputs("no tabs here\n", f);
        fclose(f);
    }
    static TrackPrefs back;
    trackPrefsInit(&back);
    trackPrefsLoad(&back, file);
    CHECK(back.count == t.count && strcmp(back.items[0].key, t.items[0].key) == 0);
    CHECK(trackPrefsFind(&back, "sdmc:/V/Rook/e1.mkv", "Rook", &sound, &subs) && strcmp(sound, "Japanese") == 0 &&
          strcmp(subs, "English (SDH)") == 0);
    trackPrefsFree(&back);
    trackPrefsFree(&t);
}
