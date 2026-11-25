// Kodi-style .nfo files: the parser (and broken or hostile input), a card's and a network folder's scan
// with .nfo files and pictures beside the videos, and the cache keeping what they said.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "check.h"
#include "home.h"
#include "library.h"
#include "nfo.h"

#define NFO_ROOT TMP_ROOT "/nfolib"
#define FUZZ_ROUNDS 20000

static const char MOVIE[] =
    "\xEF\xBB\xBF<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\" ?>\n"
    "<!-- made by a tool -->\n"
    "<movie>\n"
    "  <title>Harbor &amp; Lights</title>\n"
    "  <originaltitle>Not this one</originaltitle>\n"
    "  <ratings><rating name=\"x\"><value>7.5</value></rating></ratings>\n"
    "  <year>2019</year>\n"
    "  <outline>Short one.</outline>\n"
    "  <plot>A keeper of a lighthouse\n   finds a  letter.\tThen &#x2014; nothing.</plot>\n"
    "  <runtime>112</runtime>\n"
    "  <genre>Drama</genre><genre>Mystery / Drama</genre><genre>Science Fiction, Thriller</genre>\n"
    "  <thumb aspect=\"poster\">http://example.invalid/p.jpg</thumb>\n"
    "  <fanart><thumb>http://example.invalid/f.jpg</thumb></fanart>\n"
    "  <title>Second title, ignored</title>\n"
    "</movie>\n"
    "http://www.example.invalid/title/tt0000000/\n";

static void testParse(void)
{
    NfoInfo n;
    CHECK(nfoParse(MOVIE, sizeof(MOVIE) - 1, &n) && n.kind == NFO_MOVIE);
    CHECK(!strcmp(n.title, "Harbor & Lights") && n.year == 2019 && n.runtime_min == 112);
    CHECK(!strcmp(n.plot, "A keeper of a lighthouse finds a letter. Then \xE2\x80\x94 nothing."));
    CHECK(!strcmp(n.genres, "Drama, Mystery, Science Fiction"));  // Each once, at most three.

    static const char SHOW[] = "<tvshow><title><![CDATA[Prairie <Road>]]></title><premiered>2009-09-14</premiered>"
                               "<outline>Only an outline.</outline><genre>Western</genre></tvshow>";
    CHECK(nfoParse(SHOW, strlen(SHOW), &n) && n.kind == NFO_TVSHOW && !strcmp(n.title, "Prairie <Road>") &&
          n.year == 2009 && !strcmp(n.plot, "Only an outline.") && !strcmp(n.genres, "Western"));

    static const char EPISODE[] = "<EpisodeDetails><Title>Dry Creek</Title><Season>1</Season><Episode>6</Episode>"
                                  "<aired>2009-10-19</aired><plot/></EpisodeDetails>"
                                  "<episodedetails><title>The second of a double</title></episodedetails>";
    CHECK(nfoParse(EPISODE, strlen(EPISODE), &n) && n.kind == NFO_EPISODE && !strcmp(n.title, "Dry Creek") &&
          n.season == 1 && n.episode == 6 && n.year == 2009 && n.plot[0] == '\0');

    // What isn't one: a bare address, another root, nothing at all.
    static const char URL[] = "https://www.example.invalid/title/tt0000001/";
    CHECK(!nfoParse(URL, strlen(URL), &n) && n.kind == NFO_NONE && !n.title[0]);
    CHECK(!nfoParse("<musicvideo><title>x</title></musicvideo>", 41, &n) && !n.title[0]);
    CHECK(!nfoParse("", 0, &n) && !nfoParse(NULL, 5, &n));

    // Broken: cut in a field, numbers that aren't, Latin-1 bytes, an unclosed CDATA, stray closes.
    static const char CUT[] = "<movie><title>Cut \xE9t\xE9 x</title><year>abc</year><runtime>-5</runtime>"
                              "</nothing></deeper><plot>Never ends";
    CHECK(nfoParse(CUT, strlen(CUT), &n) && !strcmp(n.title, "Cut ?t? x") && n.year == 0 && n.runtime_min == 0);
    static const char CDATA[] = "<movie><plot><![CDATA[open";
    CHECK(nfoParse(CDATA, strlen(CDATA), &n) && !strcmp(n.plot, "open"));
    // A plot far longer than kept: cut whole characters, still one valid line.
    size_t big_size = 3 * NFO_PLOT_MAX + 64;
    char *big = malloc(big_size);
    if (big) {
        int at = snprintf(big, big_size, "<movie><plot>");
        while ((size_t)at + 16 < big_size)
            at += snprintf(big + at, big_size - (size_t)at, "\xC3\xA9 ");
        CHECK(nfoParse(big, (size_t)at, &n) && strlen(n.plot) < NFO_PLOT_MAX && strlen(n.plot) > NFO_PLOT_MAX - 8);
        CHECK(((unsigned char)n.plot[strlen(n.plot) - 1] & 0xC0) != 0xC0);  // No half character at the end.
        free(big);
    }
}

// Random bytes, and the movie mangled: never reads out of bounds (the sanitizers check), and whatever
// comes out is on one line.
static void testFuzz(void)
{
    uint32_t seed = 12345;
    char buffer[512];
    NfoInfo n;
    const char *pieces[] = {"<movie>", "</movie>", "<title>", "</title>", "<plot>", "</plot>", "<![CDATA[",
                            "]]>", "&amp;", "&#x41;", "&#99999999;", "&", "<", ">", "/>", "<genre>", "\xC3",
                            "\xE2\x80", "\t", "\n", "<!--", "-->", "<?", "?>", "<year>2001", "</genre>", "\"", "'"};
    int pieces_count = (int)(sizeof(pieces) / sizeof(pieces[0]));
    for (int round = 0; round < FUZZ_ROUNDS; round++) {
        size_t len = 0;
        if (round % 2) {
            size_t want = (seed = seed * 1103515245 + 12345) % sizeof(buffer);
            for (; len < want; len++)
                buffer[len] = (char)((seed = seed * 1103515245 + 12345) >> 16);
        } else {
            while (len + 16 < sizeof(buffer) && (seed = seed * 1103515245 + 12345) % 64) {
                const char *p = pieces[(seed >> 8) % (unsigned)pieces_count];
                size_t pl = strlen(p);
                memcpy(buffer + len, p, pl);
                len += pl;
            }
        }
        char *exact = malloc(len ? len : 1);  // Exactly len bytes: reading past them shows.
        if (!exact)
            continue;
        memcpy(exact, buffer, len);
        nfoParse(exact, len, &n);
        CHECK(!strpbrk(n.title, "\t\r\n") && !strpbrk(n.plot, "\t\r\n") && !strpbrk(n.genres, "\t\r\n"));
        free(exact);
    }
    for (size_t cut = 0; cut < sizeof(MOVIE) - 1; cut++) {  // The movie cut at every byte.
        char *exact = malloc(cut ? cut : 1);
        if (!exact)
            continue;
        memcpy(exact, MOVIE, cut);
        nfoParse(exact, cut, &n);
        free(exact);
    }
}

// --- Scanning ---

static void writeFile(const char *relative, const char *text)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", NFO_ROOT, relative);
    for (char *slash = strchr(path + strlen(TMP_ROOT) + 1, '/'); slash; slash = strchr(slash + 1, '/')) {
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

static int find(const Library *lib, const char *relative)
{
    char path[1024];
    size_t len = strlen(lib->root);
    snprintf(path, sizeof(path), "%s%s%s", lib->root, len && lib->root[len - 1] == '/' ? "" : "/", relative);
    return libraryFind(lib, path);
}

static bool endsWith(const char *text, const char *ending)
{
    size_t n = text ? strlen(text) : 0, e = strlen(ending);
    return n >= e && !strcmp(text + n - e, ending);
}

static const LibraryShow *showNamed(const Library *lib, const char *name)
{
    for (size_t i = 0; i < lib->show_count; i++)
        if (!strcmp(lib->shows[i].name, name))
            return &lib->shows[i];
    return NULL;
}

// What the scan of the made-up library found, read the same from the scan and from the cache.
static void checkScanned(const Library *lib)
{
    int harbor = find(lib, "Films/Harbor Lights (2019)/Harbor.Lights.2019.1080p.mkv");
    CHECK(harbor >= 0);
    if (harbor >= 0) {
        const LibraryItem *it = &lib->items[harbor];
        CHECK(!strcmp(it->name.title, "Harbor & Lights") && it->name.year == 2019 && !it->name.is_episode);
        CHECK(it->overview && !strncmp(it->overview, "A keeper", 8) && it->runtime_ns == 112 * 60000000000ll);
        CHECK(it->genres && !strcmp(it->genres, "Drama, Mystery, Science Fiction"));
        CHECK(endsWith(it->art, "Harbor Lights (2019)/poster.jpg"));      // movie.nfo's folder: its poster,
        CHECK(endsWith(it->backdrop, "Harbor Lights (2019)/fanart.jpg")); // and its fanart.
    }
    // In a folder of several films: each by its own name.
    int kite = find(lib, "Films/Kite Hill.mp4"), paper = find(lib, "Films/Paper Boats.mp4");
    CHECK(kite >= 0 && paper >= 0);
    if (kite >= 0 && paper >= 0) {
        const LibraryItem *k = &lib->items[kite], *p = &lib->items[paper];
        CHECK(!strcmp(k->name.title, "Kite Hill") && k->name.year == 2003 && k->genres && !strcmp(k->genres, "Comedy"));
        CHECK(endsWith(k->art, "Films/Kite Hill-poster.png") && endsWith(k->backdrop, "Films/Kite Hill-fanart.jpg"));
        CHECK(!p->overview && !p->genres && !p->art && !p->backdrop);  // Nothing of its own: not the folder's.
    }
    // A series: tvshow.nfo and its fanart for every episode; an episode's own .nfo and still.
    const LibraryShow *show = showNamed(lib, "Prairie Road");
    CHECK(show && show->count == 3 && show->seasons == 2);
    CHECK(show && homeShowGenres(lib, (int)(show - lib->shows)) &&
          !strcmp(homeShowGenres(lib, (int)(show - lib->shows)), "Western, Drama"));
    CHECK(show && homeShowOverview(lib, (int)(show - lib->shows)) &&
          !strcmp(homeShowOverview(lib, (int)(show - lib->shows)), "A county radio host."));
    int given = 0;
    for (int i = 0; show && i < show->count; i++) {
        const LibraryItem *it = &lib->items[show->episodes[i]];
        given += it->series_overview != NULL;
        CHECK(endsWith(it->backdrop, "TV/Prairie Road/fanart.jpg") && it->name.year == 2009);
        CHECK(endsWith(it->series_art, "TV/Prairie Road/poster.jpg"));
    }
    CHECK(given == 1);  // Kept by one episode only.
    int dry = find(lib, "TV/Prairie Road/Season 1/Prairie.Road.S01E02.mkv");
    CHECK(dry >= 0);
    if (dry >= 0) {
        const LibraryItem *it = &lib->items[dry];
        CHECK(!strcmp(it->name.title, "Dry Creek") && it->overview && !strcmp(it->overview, "The well runs dry."));
        CHECK(it->name.season == 1 && it->name.episode == 2 && it->runtime_ns == 44 * 60000000000ll);
        CHECK(endsWith(it->art, "Season 1/Prairie.Road.S01E02-thumb.jpg"));
    }
    // A broken .nfo, and one that's only an address: the file's name stands.
    int odd = find(lib, "Odd/Broken One (2001).mkv"), url = find(lib, "Odd/Just A Link (2002).mkv");
    CHECK(odd >= 0 && !strcmp(lib->items[odd].name.title, "Half Written") && lib->items[odd].name.year == 2001);
    CHECK(url >= 0 && !strcmp(lib->items[url].name.title, "Just A Link") && !lib->items[url].overview);
}

static void makeCardLibrary(void)
{
    writeFile("Films/Harbor Lights (2019)/Harbor.Lights.2019.1080p.mkv", "x");
    writeFile("Films/Harbor Lights (2019)/movie.nfo", MOVIE);
    writeFile("Films/Harbor Lights (2019)/poster.jpg", "x");
    writeFile("Films/Harbor Lights (2019)/fanart.jpg", "x");
    writeFile("Films/Kite Hill.mp4", "x");
    writeFile("Films/Kite Hill.nfo", "<movie><title>Kite Hill</title><year>2003</year><genre>Comedy</genre></movie>");
    writeFile("Films/Kite Hill-poster.png", "x");
    writeFile("Films/Kite Hill-fanart.jpg", "x");
    writeFile("Films/Paper Boats.mp4", "x");
    writeFile("Films/fanart.jpg", "x");  // A folder of several films: no one's.
    writeFile("Films/movie.nfo", "<movie><title>Wrong</title></movie>");
    writeFile("TV/Prairie Road/tvshow.nfo",
              "<tvshow><title>Prairie Road</title><year>2009</year><plot>A county radio host.</plot>"
              "<genre>Western</genre><genre>Drama</genre></tvshow>");
    writeFile("TV/Prairie Road/poster.jpg", "x");
    writeFile("TV/Prairie Road/fanart.jpg", "x");
    writeFile("TV/Prairie Road/Season 1/Prairie.Road.S01E01.mkv", "x");
    writeFile("TV/Prairie Road/Season 1/Prairie.Road.S01E02.mkv", "x");
    writeFile("TV/Prairie Road/Season 1/Prairie.Road.S01E02.nfo",
              "<episodedetails><title>Dry Creek</title><season>1</season><episode>2</episode>"
              "<runtime>44</runtime><plot>The well runs dry.</plot></episodedetails>");
    writeFile("TV/Prairie Road/Season 1/Prairie.Road.S01E02-thumb.jpg", "x");
    writeFile("TV/Prairie Road/Season 2/Prairie.Road.S02E01.mkv", "x");
    writeFile("Odd/Broken One (2001).mkv", "x");
    writeFile("Odd/Broken One (2001).nfo", "<movie><title>Half Written</title><plot>cut off <<<&&&");
    writeFile("Odd/Just A Link (2002).mkv", "x");
    writeFile("Odd/Just A Link (2002).nfo", "https://www.example.invalid/title/tt0000002/");
}

// The same tree, as a network folder's server lists it, and its files as read.
static bool fakeList(const char *path, DavEntry **out, size_t *count, char *why, size_t why_size)
{
    char local[512];
    snprintf(local, sizeof(local), "%s%s", NFO_ROOT, path + 4);  // "dav:/X" -> NFO_ROOT "/X".
    *count = 0;
    *out = calloc(64, sizeof(DavEntry));
    FILE *p = NULL;
    char command[600];
    snprintf(command, sizeof(command), "ls -1Ap '%s'", local);
    if (!*out || !(p = popen(command, "r"))) {
        free(*out);
        *out = NULL;
        snprintf(why, why_size, "down");
        return false;
    }
    char name[256];
    while (*count < 64 && fgets(name, sizeof(name), p)) {
        name[strcspn(name, "\n")] = '\0';
        size_t n = strlen(name);
        DavEntry *e = &(*out)[*count];
        e->is_dir = n && name[n - 1] == '/';
        if (e->is_dir)
            name[n - 1] = '\0';
        snprintf(e->name, sizeof(e->name), "%s", name);
        e->size = 1;
        (*count)++;
    }
    pclose(p);
    return true;
}

static int g_reads;

static char *fakeRead(const char *location, size_t max_bytes, size_t *size)
{
    char local[512];
    g_reads++;
    if (strncmp(location, "dav:/", 5))
        return NULL;
    snprintf(local, sizeof(local), "%s/%s", NFO_ROOT, location + 5);
    char *data = NULL;
    FILE *f = fopen(local, "rb");
    if (f) {
        data = malloc(max_bytes + 1);
        size_t n = data ? fread(data, 1, max_bytes, f) : 0;
        if (data)
            data[n] = '\0';
        *size = n;
        fclose(f);
    }
    return data;
}

static void testScan(void)
{
    makeCardLibrary();
    static Library card, back, net;
    libraryInit(&card, NFO_ROOT);
    CHECK(libraryScan(&card, NULL) && card.count == 8);
    checkScanned(&card);

    // Home's Showcase: genre rows for the card, as for a server.
    static Home home;
    homeComputeShowcase(&home, &card, NULL, NULL);
    bool drama = false;
    for (int r = 0; r < home.count; r++)
        drama = drama || (home.rows[r].kind == HOME_GENRE && !strcmp(home.rows[r].genre, "Drama") &&
                          home.rows[r].count == 2);  // Harbor Lights and Prairie Road.
    CHECK(drama);

    // Kept in the cache and read back the same, without reading any .nfo again.
    const char *cache = TMP_ROOT "/nfolib.txt";
    CHECK(librarySave(&card, cache));
    CHECK(libraryLoad(&back, cache) && back.count == card.count && back.show_count == card.show_count);
    checkScanned(&back);
    // A cache line broken in every way is skipped, the rest still read.
    FILE *f = fopen(cache, "a");
    if (f) {
        fputs("nfo\tsdmc:/nowhere.mkv\t0\t0\t0\t0\t0\tX\t\t\t\t\n", f);
        fputs("nfo\t" NFO_ROOT "/Films/Paper Boats.mp4\tzz\n", f);
        fputs("backdrop\t" NFO_ROOT "/Films/Paper Boats.mp4\t/elsewhere/fanart.jpg\n", f);
        fputs("nfo\t" NFO_ROOT "/Films/Paper Boats.mp4\t-1\t0\t0\t0\t90\t\t\tWar\t\t\textra\tfields\n", f);
        fclose(f);
    }
    CHECK(libraryLoad(&back, cache) && back.count == card.count);
    int paper = find(&back, "Films/Paper Boats.mp4");
    CHECK(paper >= 0 && !back.items[paper].backdrop && back.items[paper].genres &&
          !strcmp(back.items[paper].genres, "War") && back.items[paper].runtime_ns == 90 * 60000000000ll &&
          !strcmp(back.items[paper].name.title, "Paper Boats"));

    // The network folder: the same, its files read through the server.
    libraryInit(&net, "dav:/");
    g_reads = 0;
    CHECK(libraryScanNetwork(&net, fakeList, fakeRead, NULL) && net.count == 8);
    checkScanned(&net);
    CHECK(g_reads == 6);  // movie.nfo, Kite Hill.nfo, tvshow.nfo, one episode's, two odd ones.
    // Without a way to read files: the names and pictures only.
    libraryFree(&net);
    libraryInit(&net, "dav:/");
    CHECK(libraryScanNetwork(&net, fakeList, NULL, NULL) && net.count == 8);
    int harbor = find(&net, "Films/Harbor Lights (2019)/Harbor.Lights.2019.1080p.mkv");
    CHECK(harbor >= 0 && !net.items[harbor].overview && endsWith(net.items[harbor].backdrop, "fanart.jpg"));
    libraryFree(&net);
    libraryFree(&back);
    libraryFree(&card);
}

void testNfo(void)
{
    testParse();
    testFuzz();
    testScan();
}
