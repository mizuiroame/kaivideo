// Home's Showcase for a folder on the card whose videos have Kodi-style .nfo
// files and pictures beside them: a made-up library written to /tmp, scanned
// as the console scans it. And the Showcase's tiles wearing their downloads'
// marks, as the classic cards do.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "home.h"
#include "lang.h"
#include "library.h"
#include "libview.h"
#include "ui_downloads.h"
#include "ui_library.h"
#include "ui_style.h"
#include "showcase_scenes.h"

#define NFO_ROOT "/tmp/kaivideo_preview_nfo"
#define DRAWS 40

static void writeFile(const char *relative, const char *text)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", NFO_ROOT, relative);
    for (char *slash = strchr(path + 1, '/'); slash; slash = strchr(slash + 1, '/')) {
        *slash = '\0';
        mkdir(path, 0755);
        *slash = '/';
    }
    FILE *f = fopen(path, "w");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

typedef struct {
    const char *folder, *file, *title, *genres, *plot;
    int year, minutes;
} FakeFilm;

static const FakeFilm FILMS[] = {
    {"Lantern Bay (2021)", "Lantern.Bay.2021.1080p.mkv", "Lantern Bay", "Drama, Mystery",
     "When the harbor lights go dark one winter, a ferry pilot and her estranged brother follow a trail of "
     "unsent letters across three islands to learn who kept the lamps burning.", 2021, 118},
    {"Kite Hill (2003)", "Kite.Hill.2003.mkv", "Kite Hill", "Comedy, Family",
     "Two rival kite clubs share one windy hill and one very stubborn goat.", 2003, 94},
    {"Salt Meridian (2017)", "Salt.Meridian.2017.mkv", "Salt Meridian", "Science Fiction, Drama",
     "A survey crew mapping a dry sea finds a lighthouse that should not be there, still turning.", 2017, 131},
    {"The Copper Orchard (2012)", "The.Copper.Orchard.2012.mkv", "The Copper Orchard", "Drama, Western",
     "A widow keeps her orchard running through the driest summer the valley has known.", 2012, 107},
    {"Night Ferry (2015)", "Night.Ferry.2015.mkv", "Night Ferry", "Thriller, Mystery",
     "Seven passengers, one crossing, and a cabin nobody booked.", 2015, 99},
    {"Paper Boats (2019)", "Paper.Boats.2019.mkv", "Paper Boats", "Comedy, Drama",
     "A retired teacher races paper boats down the town's gutters with the kid next door.", 2019, 88},
};

// The made-up library: films in folders of their own (movie.nfo, poster, fanart) and two series
// (tvshow.nfo, poster and fanart in the series' folder, an .nfo for some episodes).
static void writeLibrary(void)
{
    char path[256], text[1024];
    for (size_t i = 0; i < sizeof(FILMS) / sizeof(FILMS[0]); i++) {
        const FakeFilm *f = &FILMS[i];
        snprintf(path, sizeof(path), "Films/%s/%s", f->folder, f->file);
        writeFile(path, "x");
        char genres[160] = "";
        const char *g = f->genres;
        while (*g) {  // One <genre> each.
            size_t len = strcspn(g, ",");
            size_t used = strlen(genres);
            snprintf(genres + used, sizeof(genres) - used, "<genre>%.*s</genre>", (int)len, g);
            g += len;
            while (*g == ',' || *g == ' ')
                g++;
        }
        snprintf(text, sizeof(text),
                 "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<movie>\n  <title>%s</title>\n  <year>%d</year>\n"
                 "  <runtime>%d</runtime>\n  <plot>%s</plot>\n  %s\n</movie>\n",
                 f->title, f->year, f->minutes, f->plot, genres);
        snprintf(path, sizeof(path), "Films/%s/movie.nfo", f->folder);
        writeFile(path, text);
        snprintf(path, sizeof(path), "Films/%s/poster.jpg", f->folder);
        writeFile(path, "x");
        snprintf(path, sizeof(path), "Films/%s/fanart.jpg", f->folder);
        writeFile(path, "x");
    }
    writeFile("TV/Prairie Road/tvshow.nfo",
              "<tvshow><title>Prairie Road</title><year>2009</year><genre>Western</genre><genre>Drama</genre>"
              "<plot>A small-town radio host keeps the lights on for a county that time forgot, one late-night "
              "call at a time.</plot></tvshow>");
    writeFile("TV/Prairie Road/poster.jpg", "x");
    writeFile("TV/Prairie Road/fanart.jpg", "x");
    static const char *const EPISODES[] = {"Dry Creek", "Harvest Fair", "Windmill Weather", "Copper Kettle"};
    for (int s = 1; s <= 2; s++)
        for (int e = 1; e <= 4; e++) {
            snprintf(path, sizeof(path), "TV/Prairie Road/Season %d/Prairie.Road.S%02dE%02d.mkv", s, s, e);
            writeFile(path, "x");
            snprintf(path, sizeof(path), "TV/Prairie Road/Season %d/Prairie.Road.S%02dE%02d.nfo", s, s, e);
            snprintf(text, sizeof(text),
                     "<episodedetails><title>%s</title><season>%d</season><episode>%d</episode>"
                     "<runtime>44</runtime><plot>The county gathers for the %s.</plot></episodedetails>",
                     EPISODES[e - 1], s, e, EPISODES[e - 1]);
            writeFile(path, text);
        }
    writeFile("TV/Moon Fox Station/tvshow.nfo",
              "<tvshow><title>Moon Fox Station</title><year>2022</year><genre>Science Fiction</genre>"
              "<genre>Comedy</genre><plot>The night shift on a lunar relay station, where the only visitor is a "
              "fox nobody can explain.</plot></tvshow>");
    writeFile("TV/Moon Fox Station/fanart.jpg", "x");
    for (int e = 1; e <= 6; e++) {
        snprintf(path, sizeof(path), "TV/Moon Fox Station/Moon.Fox.Station.S01E%02d.mkv", e);
        writeFile(path, "x");
    }
}

static int rowOf(const Home *home, HomeRowKind kind, const char *genre)
{
    for (int r = 0; r < home->count; r++)
        if (home->rows[r].kind == kind && (!genre || !strcmp(home->rows[r].genre, genre)))
            return r;
    return 0;
}

static int refCol(const Library *lib, const HomeRow *row, const char *name)
{
    for (int c = 0; c < row->count; c++) {
        LibraryRef r = row->items[c];
        const char *n = r.is_show ? lib->shows[r.index].name : lib->items[r.index].name.title;
        if (!strcmp(n, name))
            return c;
    }
    return 0;
}

void nfoShowcaseScenes(const ShowcaseCtx *c)
{
    system("rm -rf " NFO_ROOT);
    writeLibrary();
    static Library lib;
    static Home home;
    static LibView view;
    libraryInit(&lib, NFO_ROOT);
    if (!libraryScan(&lib, NULL) || lib.count != 6 + 8 + 6 || lib.show_count != 2) {
        showcaseProblem(c, "showcase_sd_nfo", "the made-up library wasn't scanned as written");
        libraryFree(&lib);
        return;
    }
    libviewInit(&view);
    libviewSetHomeShowcase(&view, true);
    uiLibrarySetSource(T("SD card"), COLOR_ACCENT);
    homeComputeShowcase(&home, &lib, c->resume, c->watched);
    if (rowOf(&home, HOME_GENRE, "Drama") == 0)
        showcaseProblem(c, "showcase_sd_nfo", "no Drama row from the .nfo files' genres");

    homeSetTiles(&home, HOME_TILES_MIXED);
    int films = rowOf(&home, HOME_FILMS, NULL);
    showcaseMoveTo(&view, &home, films, refCol(&lib, &home.rows[films], "Lantern Bay"));
    showcaseDraw(c, "showcase_sd_nfo", &view, &lib, &home, DRAWS);
    showcaseCheckTaps(c, "showcase_sd_nfo", &view, &home);

    int series = rowOf(&home, HOME_SERIES, NULL);
    showcaseMoveTo(&view, &home, series, refCol(&lib, &home.rows[series], "Prairie Road"));
    showcaseDraw(c, "showcase_sd_nfo_series", &view, &lib, &home, DRAWS);
    showcaseCheckTaps(c, "showcase_sd_nfo_series", &view, &home);

    homeSetTiles(&home, HOME_TILES_LANDSCAPE);
    libviewClamp(&view, &lib, &home);
    int drama = rowOf(&home, HOME_GENRE, "Drama");
    showcaseMoveTo(&view, &home, drama, 1);
    showcaseDraw(c, "showcase_sd_nfo_genre", &view, &lib, &home, DRAWS);
    showcaseCheckTaps(c, "showcase_sd_nfo_genre", &view, &home);
    libraryFree(&lib);
}

// A server's videos on their way to the card, or on it.
static int fakeDownload(const char *path, const void *user, double *fraction)
{
    (void)user;
    *fraction = 0.42;
    if (strstr(path, "S01E01") || strstr(path, "Lanterns") || strstr(path, "Harbor"))
        return UI_DL_DONE;
    if (strstr(path, "S01E02") || strstr(path, "Ashfall"))
        return UI_DL_ACTIVE;
    if (strstr(path, "S01E03"))
        return UI_DL_PAUSED;
    return UI_DL_NONE;
}

void downloadShowcaseScenes(const ShowcaseCtx *c, const Library *server)
{
    static Home home;
    static LibView view;
    libviewInit(&view);
    libviewSetTabs(&view, server, true, false, false);
    libviewSetHomeShowcase(&view, true);
    uiLibrarySetSource("Jellyfin", uiServerColor(SERVER_JELLYFIN));
    uiLibrarySetDownloads(fakeDownload, NULL);
    homeComputeShowcase(&home, server, c->resume, c->watched);
    homeSetTiles(&home, HOME_TILES_POSTERS);
    showcaseMoveTo(&view, &home, rowOf(&home, HOME_FILMS, NULL), 0);
    showcaseDraw(c, "showcase_downloads_posters", &view, server, &home, DRAWS);
    showcaseCheckTaps(c, "showcase_downloads_posters", &view, &home);
    homeSetTiles(&home, HOME_TILES_LANDSCAPE);
    libviewClamp(&view, server, &home);
    showcaseMoveTo(&view, &home, rowOf(&home, HOME_FILMS, NULL), 0);
    showcaseDraw(c, "showcase_downloads_landscape", &view, server, &home, DRAWS);
    showcaseCheckTaps(c, "showcase_downloads_landscape", &view, &home);
    uiLibrarySetDownloads(NULL, NULL);
    uiLibrarySetSource(T("SD card"), COLOR_ACCENT);
}
