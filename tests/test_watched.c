// Watched films and episodes, and what Home suggests next from them.
#include <stdio.h>
#include <string.h>

#include "check.h"
#include "home.h"
#include "watched.h"

static void testWatchedList(void)
{
    static Watched w;
    watchedInit(&w);
    const char *file = TMP_ROOT "/watched.txt";
    remove(file);
    watchedLoad(&w, file);
    CHECK(w.count == 0 && !watchedHas(&w, "sdmc:/a.mkv"));
    watchedSet(&w, "sdmc:/a.mkv", true);
    watchedSet(&w, "sdmc:/b.mkv", true);
    watchedSet(&w, "sdmc:/a.mkv", true);  // Again: now the latest.
    CHECK(w.count == 2 && watchedOrder(&w, "sdmc:/a.mkv") > watchedOrder(&w, "sdmc:/b.mkv"));
    watchedSet(&w, "sdmc:/b.mkv", false);
    CHECK(w.count == 1 && !watchedHas(&w, "sdmc:/b.mkv"));
    watchedSet(&w, "sdmc:/bad\nname.mkv", true);  // Would break the file: not kept.
    CHECK(w.count == 1);
    watchedSet(&w, "sdmc:/Pelí cula.mkv", true);
    CHECK(watchedSave(&w, file));
    static Watched back;
    watchedInit(&back);
    watchedLoad(&back, file);
    CHECK(back.count == 2 && watchedOrder(&back, "sdmc:/Pelí cula.mkv") == 1 && watchedHas(&back, "sdmc:/a.mkv"));
    watchedFree(&back);
    watchedFree(&w);
}

static void testUpNext(void)
{
    static Library lib;
    libraryInit(&lib, "sdmc:/V");
    char path[128];
    for (int s = 1; s <= 2; s++)
        for (int e = 1; e <= 3; e++) {
            snprintf(path, sizeof(path), "sdmc:/V/Rook/Rook.S%02dE%02d.mkv", s, e);
            libraryAdd(&lib, path, 1, 1);
        }
    for (int e = 1; e <= 2; e++) {
        snprintf(path, sizeof(path), "sdmc:/V/Dustland/Dustland.S01E%02d.mkv", e);
        libraryAdd(&lib, path, 1, 1);
    }
    libraryAdd(&lib, "sdmc:/V/Paper Moon (1998)/moon.mkv", 1, 1);
    libraryBuild(&lib);
    int rook = strcmp(lib.shows[0].name, "Rook") == 0 ? 0 : 1;
    // The next episode, across seasons; none after the last, or for a film.
    int s1e3 = lib.shows[rook].episodes[2], s2e1 = lib.shows[rook].episodes[3];
    CHECK(libraryNextEpisode(&lib, s1e3) == s2e1);
    CHECK(libraryNextEpisode(&lib, lib.shows[rook].episodes[5]) == -1);
    CHECK(libraryNextEpisode(&lib, lib.films[0]) == -1);

    static Watched w;
    watchedInit(&w);
    static ResumeList resume;
    memset(&resume, 0, sizeof(resume));
    static Home home;
    // Rook S01E02 watched, then Dustland S01E01 later: Dustland's next first, then Rook's.
    watchedSet(&w, lib.items[lib.shows[rook].episodes[1]].path, true);
    watchedSet(&w, lib.items[lib.shows[1 - rook].episodes[0]].path, true);
    homeCompute(&home, &lib, &resume, &w);
    HomeRow *row = &home.rows[0];
    CHECK(row->kind == HOME_CONTINUE && row->count == 2 && row->up_next[0] && row->up_next[1]);
    CHECK(row->items[0].index == lib.shows[1 - rook].episodes[1] && row->items[1].index == s1e3);

    // An episode watched further on is skipped; one being watched replaces the suggestion.
    watchedSet(&w, lib.items[s1e3].path, true);
    watchedSet(&w, lib.items[lib.shows[rook].episodes[1]].path, true);  // S01E02 again, the latest.
    homeCompute(&home, &lib, &resume, &w);
    CHECK(row->items[0].index == s2e1);  // S01E03 is watched: S02E01.
    const int64_t S = 1000000000;
    resumeRemember(&resume, lib.items[lib.shows[rook].episodes[4]].path, 60 * S, 600 * S);
    homeCompute(&home, &lib, &resume, &w);
    CHECK(row->count == 2 && !row->up_next[0] && row->items[0].index == lib.shows[rook].episodes[4]);
    CHECK(row->up_next[1] && lib.items[row->items[1].index].show == 1 - rook);  // Rook isn't suggested twice.

    // The last episode watched: nothing next for that series.
    watchedSet(&w, lib.items[lib.shows[1 - rook].episodes[1]].path, true);
    memset(&resume, 0, sizeof(resume));
    watchedSet(&w, lib.items[lib.shows[rook].episodes[5]].path, true);
    homeCompute(&home, &lib, &resume, &w);
    CHECK(home.rows[0].kind != HOME_CONTINUE);
    watchedFree(&w);
    libraryFree(&lib);
}

void testWatched(void)
{
    testWatchedList();
    testUpNext();
}
