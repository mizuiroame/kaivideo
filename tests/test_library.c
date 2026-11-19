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

// A server's libraries ("collections") and favorites: what each tab and the
// Favorites row show.
static void testCollections(void)
{
    static Library lib;
    libraryInit(&lib, "jellyfin:");
    int films = libraryAddCollection(&lib, "Movies", COLLECTION_FILMS);
    int shows = libraryAddCollection(&lib, "Shows", COLLECTION_SERIES);
    int anime = libraryAddCollection(&lib, "Anime", COLLECTION_MIXED);
    CHECK(films == 0 && shows == 1 && anime == 2 && lib.collection_count == 3);
    VideoName lantern = {.title = "Paper Lanterns", .year = 1998}, kite = {.title = "Kite Hill"};
    VideoName ep1 = {.is_episode = true, .show = "Rook", .season = 1, .episode = 1};
    VideoName ep2 = {.is_episode = true, .show = "Rook", .season = 1, .episode = 2};
    VideoName moon = {.is_episode = true, .show = "Moon Fox", .season = 1, .episode = 1};
    VideoName clip = {.title = "Moon Fox: The Film"};
    CHECK(libraryAddRemote(&lib, "jellyfin:a/a", &lantern, 1, 1, &(LibraryRemote){.in_collection = films + 1, .favorite = true}));
    CHECK(libraryAddRemote(&lib, "jellyfin:b/b", &kite, 1, 2, &(LibraryRemote){.in_collection = films + 1}));
    CHECK(libraryAddRemote(&lib, "jellyfin:c/c", &ep1, 1, 3, &(LibraryRemote){.in_collection = shows + 1}));
    CHECK(libraryAddRemote(&lib, "jellyfin:d/d", &ep2, 1, 4, &(LibraryRemote){.in_collection = shows + 1, .favorite = true}));
    CHECK(libraryAddRemote(&lib, "jellyfin:e/e", &moon, 1, 5, &(LibraryRemote){.in_collection = anime + 1}));
    CHECK(libraryAddRemote(&lib, "jellyfin:f/f", &clip, 1, 6, &(LibraryRemote){.in_collection = anime + 1}));
    CHECK(libraryAddRemote(&lib, "jellyfin:g/g", &kite, 1, 7, &(LibraryRemote){0}));  // In none.
    libraryBuild(&lib);
    CHECK(lib.items[6].collection == -1 && lib.items[0].collection == films);

    LibraryRef refs[16];
    size_t n = libraryCollectionRefs(&lib, films, refs, 16);
    CHECK(n == 2 && !refs[0].is_show && lib.items[refs[0].index].name.title[0] == 'K');  // By title.
    n = libraryCollectionRefs(&lib, shows, refs, 16);
    CHECK(n == 1 && refs[0].is_show && !strcmp(lib.shows[refs[0].index].name, "Rook"));
    n = libraryCollectionRefs(&lib, anime, refs, 16);
    CHECK(n == 2 && refs[0].is_show && !refs[1].is_show);  // A mixed one: its series, then its films.
    CHECK(libraryCollectionRefs(&lib, 7, refs, 16) == 0);
    CHECK(libraryCollectionRefs(&lib, films, refs, 1) == 1);  // Never more than asked.

    // A series is a favorite when the server says so of it (its episodes carry it).
    n = libraryFavorites(&lib, refs, 16);
    CHECK(n == 2 && refs[0].is_show && !refs[1].is_show && refs[1].index == 0);
    int moon_show = lib.items[4].show;
    librarySetFavorite(&lib, (LibraryRef){true, moon_show}, true);
    CHECK(lib.shows[moon_show].favorite && lib.items[4].favorite);
    librarySetFavorite(&lib, (LibraryRef){false, 0}, false);
    n = libraryFavorites(&lib, refs, 16);
    CHECK(n == 2 && refs[0].is_show && refs[1].is_show);
    CHECK(libraryAddCollection(&lib, "", COLLECTION_FILMS) == -1);
    libraryFree(&lib);
}

void testLibrary(void)
{
    testCollections();
    mkdir(TMP_ROOT, 0755);
    touchFile("Movies/Quiet Harbor (2017)/harbor.mp4", 1000);
    touchFile("Movies/Paper.Lanterns.1998.1080p.BluRay.mkv", 3000);
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
    CHECK(lib.film_count == 2 && strcmp(lib.items[lib.films[0]].name.title, "Paper Lanterns") == 0 &&
          strcmp(lib.items[lib.films[1]].name.title, "Quiet Harbor") == 0);
    CHECK(lib.show_count == 1);
    const LibraryShow *rook = findShow(&lib, "Rook");
    CHECK(rook && rook->count == 4 && rook->seasons == 2 && rook->added == 2500);
    if (rook && rook->count == 4) {
        CHECK(lib.items[rook->episodes[0]].name.episode == 1 && lib.items[rook->episodes[2]].name.episode == 3 &&
              lib.items[rook->episodes[3]].name.season == 2);
        CHECK(lib.items[rook->episodes[0]].show == 0);
    }
    // The newest first: Paper Lanterns (3000), Rook (2500), Quiet Harbor (1000).
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
