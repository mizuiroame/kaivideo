// The library: scanning a folder tree, grouping series, the cache, search.
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <utime.h>

#include "check.h"
#include "library.h"

#define LIB_ROOT TMP_ROOT "/lib"

static void touchFile(const char *relative, long when)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", LIB_ROOT, relative);
    // Make the folders on the way.
    for (char *slash = strchr(path + strlen(TMP_ROOT) + 1, '/'); slash; slash = strchr(slash + 1, '/')) {
        *slash = '\0';
        mkdir(path, 0755);
        *slash = '/';
    }
    FILE *f = fopen(path, "w");
    if (f) {
        fputs("x", f);
        fclose(f);
    }
    struct utimbuf times = {when, when};
    utime(path, &times);
}

static const LibraryShow *findShow(const Library *lib, const char *name)
{
    for (size_t i = 0; i < lib->show_count; i++)
        if (strcmp(lib->shows[i].name, name) == 0)
            return &lib->shows[i];
    return NULL;
}

void testLibrary(void)
{
    mkdir(TMP_ROOT, 0755);
    touchFile("Movies/Quiet Harbor (2017)/harbor.mp4", 1000);
    touchFile("Movies/Paper.Moon.1998.1080p.BluRay.mkv", 3000);
    touchFile("Movies/notes.txt", 9000);  // Not a video.
    touchFile("TV/Rook/Season 2/Rook.S02E01.1080p.mkv", 2000);
    touchFile("TV/Rook/Rook.S01E02.1080p.mkv", 2000);
    touchFile("TV/Rook/Rook.S01E01.1080p.mkv", 2500);
    touchFile("Downloads/rook.s01e03.720p.webm", 1500);
    touchFile(".hidden/Secret.S01E01.mkv", 4000);

    static Library lib;
    libraryInit(&lib, LIB_ROOT);
    CHECK(libraryScan(&lib, NULL));
    CHECK(lib.count == 6);
    // Films by title; one series from three folders, its episodes in order.
    CHECK(lib.film_count == 2 && strcmp(lib.items[lib.films[0]].name.title, "Paper Moon") == 0 &&
          strcmp(lib.items[lib.films[1]].name.title, "Quiet Harbor") == 0);
    CHECK(lib.show_count == 1);
    const LibraryShow *rook = findShow(&lib, "Rook");
    CHECK(rook && rook->count == 4 && rook->seasons == 2 && rook->added == 2500);
    if (rook && rook->count == 4) {
        CHECK(lib.items[rook->episodes[0]].name.episode == 1 && lib.items[rook->episodes[2]].name.episode == 3 &&
              lib.items[rook->episodes[3]].name.season == 2);
        CHECK(lib.items[rook->episodes[0]].show == 0);
    }
    // The newest first: Paper Moon (3000), Rook (2500), Quiet Harbor (1000).
    CHECK(lib.recent_count == 3 && !lib.recent[0].is_show && lib.recent[1].is_show && !lib.recent[2].is_show);

    // Kept and read back.
    const char *cache = TMP_ROOT "/library.txt";
    CHECK(librarySave(&lib, cache));
    static Library back;
    CHECK(libraryLoad(&back, cache));
    CHECK(strcmp(back.root, LIB_ROOT) == 0 && back.count == 6 && back.film_count == 2 && back.show_count == 1);
    CHECK(!libraryLoad(&back, TMP_ROOT "/none.txt") && back.count == 0);

    char harbor[512];
    snprintf(harbor, sizeof(harbor), "%s/Movies/Quiet Harbor (2017)/harbor.mp4", LIB_ROOT);
    CHECK(libraryFind(&lib, harbor) >= 0 && libraryFind(&lib, "sdmc:/nothing.mkv") == -1);

    // Search: the series, then films; by key, so accents and case don't matter.
    LibraryRef found[8];
    CHECK(librarySearch(&lib, "ROOK", found, 8) == 1 && found[0].is_show);
    CHECK(librarySearch(&lib, "páper", found, 8) == 1 && !found[0].is_show);
    CHECK(librarySearch(&lib, "  ", found, 8) == 0);
    CHECK(librarySearch(&lib, "zzz", found, 8) == 0);

    // A missing folder is an empty library.
    static Library gone;
    libraryInit(&gone, TMP_ROOT "/missing");
    CHECK(!libraryScan(&gone, NULL) && gone.count == 0 && gone.show_count == 0);
    libraryFree(&gone);
    libraryFree(&back);
    libraryFree(&lib);
}
