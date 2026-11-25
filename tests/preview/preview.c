// Renders the real UI code into PPM images on the host, with a fake player
// and a made-up picture, so the design can be checked without a console.
// Usage: preview OUTDIR
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "browser.h"
#include "font.h"
#include "gfx.h"
#include "home.h"
#include "lang.h"
#include "library.h"
#include "libview.h"
#include "menu.h"
#include "playback.h"
#include "player.h"
#include "resume.h"
#include "ui.h"
#include "ui_downloads.h"
#include "ui_library.h"
#include "settings.h"
#include "theme.h"
#include "thumbs.h"
#include "ui_settings.h"
#include "ui_iptvhome.h"
#include "stremio.h"
#include "ui_streams.h"
#include "xtreamvod.h"
#include "ui_live.h"
#include "ui_notice.h"
#include "ui_stats.h"
#include "ui_style.h"
#include "sample_paths.h"

// --- A fake player: what the bar and the lists ask of it ---

struct Player {
    bool paused;
    int audio, subtitle;
    int audio_count, subtitle_count;
    int speed, volume, audio_delay, subtitle_delay;
    int chapter_count;
    bool live;  // Live TV: no length.
};

static const char *const AUDIO[] = {"English - DD5.1", "Español (Latinoamérica) - DD5.1", "Commentary - AAC2.0"};
static const char *const SUBS[] = {"English", "English (SDH)", "Español", "Français", "Deutsch", "Italiano",
                                   "Português (Brasil)", "日本語", "Polski", "Svenska"};
static const char *const CHAPTERS[] = {"Cold Open", "The Harbor", "Night Shift", "A Long Way Down", "Reckoning",
                                       "The Letter", "Low Tide", "Morning", "End Credits"};

bool playerPaused(const Player *p) { return p->paused; }
int64_t playerPosition(const Player *p) { (void)p; return 1325ll * 1000000000; }
int64_t playerDuration(const Player *p) { return p->live ? 0 : 2710ll * 1000000000; }
const char *playerVideoInfo(const Player *p) { (void)p; return "H.264 1920x1080, 24 fps"; }
const char *playerAudioInfo(const Player *p) { (void)p; return "ac3, 48 kHz, 6 channels"; }
int playerAudioTrackCount(const Player *p) { return p->audio_count; }
const char *playerAudioTrackName(const Player *p, int track) { (void)p; return AUDIO[track]; }
int playerAudioTrack(const Player *p) { return p->audio; }
int playerSubtitleCount(const Player *p) { return p->subtitle_count; }
const char *playerSubtitleLabel(const Player *p, int track) { (void)p; return SUBS[track]; }
int playerSubtitle(const Player *p) { return p->subtitle; }
int playerSpeed(const Player *p) { return p->speed ? p->speed : PLAYBACK_PERMILLE; }
int playerVolume(const Player *p) { return p->volume ? p->volume : PLAYBACK_VOLUME_NORMAL; }
PlaybackAspect playerAspect(const Player *p) { (void)p; return PLAYBACK_ASPECT_ORIGINAL; }
PlaybackLoop playerLoop(const Player *p) { (void)p; return (PlaybackLoop){83ll * 1000000000, -1}; }
int playerAudioDelay(const Player *p) { return p->audio_delay; }
int playerSubtitleDelay(const Player *p) { return p->subtitle_delay; }
int playerChapterCount(const Player *p) { return p->chapter_count; }
const char *playerChapterName(const Player *p, int chapter) { (void)p; return CHAPTERS[chapter]; }
int64_t playerChapterStart(const Player *p, int chapter) { (void)p; return chapter * 300ll * 1000000000; }
int playerChapter(const Player *p) { return p->chapter_count ? 4 : -1; }

// --- Output ---

static bool writePpm(const char *path, const uint32_t *pixels)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;
    fprintf(f, "P6\n%d %d\n255\n", GFX_FB_WIDTH, GFX_FB_HEIGHT);
    for (int i = 0; i < GFX_FB_WIDTH * GFX_FB_HEIGHT; i++) {
        uint32_t p = pixels[i];
        unsigned char rgb[3] = {p & 0xFF, (p >> 8) & 0xFF, (p >> 16) & 0xFF};
        fwrite(rgb, 1, 3, f);
    }
    return fclose(f) == 0;
}

static uint32_t g_pixels[GFX_FB_WIDTH * GFX_FB_HEIGHT];
static const char *g_dir;
static int g_problems;

static void begin(void)
{
    uiLibraryResetHits();
    gfxSetTarget(g_pixels, GFX_FB_WIDTH);
}

static void save(const char *name)
{
    gfxSetTarget(NULL, 0);
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.ppm", g_dir, name);
    if (!writePpm(path, g_pixels))
        fprintf(stderr, "cannot write %s\n", path);
}

// A stand-in for a film's picture: a dusk sky over dark hills.
static void drawPicture(void)
{
    gfxGradientV(0, 0, GFX_WIDTH, GFX_HEIGHT, GFX_RGB(0x2B, 0x3A, 0x6B), GFX_RGB(0xE0, 0x8A, 0x5A));
    gfxFillCircle(900, 430, 70, GFX_RGB(0xFF, 0xD9, 0x8C));
    for (int i = 0; i < 6; i++)
        gfxFillCircle(i * 260 - 40, 760, 220 - (i % 2) * 40, GFX_RGB(0x1B, 0x1F, 0x2E));
}

static Config g_config;  // The subtitles' options shown in their list.

static void playerScene(const char *name, Player *p, UiMenuKind kind, const Menu *m, const char *subtitle,
                        const char *note)
{
    begin();
    drawPicture();
    if (subtitle)
        uiDrawSubtitles(subtitle, true);
    if (p->paused && !m->open)
        uiDrawPausedSymbol();
    uiDrawPlayerStatus();
    uiDrawPlayerBar(p, "Rook - S01E03 - Reckoning.mkv", m->open ? NULL : note, -1);
    uiDrawTrackMenu(p, kind, m, &g_config);
    save(name);
}

// The Live TV tab, and a channel playing (LIVE in the bar).
static void makeFakeThumbs(void);

static void liveScene(Player *p)
{
    static const char *const NAMES[] = {"KaiVideo Test One", "KaiVideo Test Two", "Harbor News 24", "Kite Hill Sports"};
    static const char *const NOW[] = {"The Quiet Workshop", "Kite Hill Live", "Evening Report", "Regional Final"};
    static const char *const NEXT[] = {"Paper Boats", "Morning Lanterns", "Harbor Weather", "Post-Game"};
    static const char *const LATER[] = {"Lantern Makers", "Tide Tables", "Night Desk", "Highlights", "Quiet Hours"};
    static LiveChannel channels[4];
    const int64_t now = 1795082400 + 600;
    for (int i = 0; i < 4; i++) {
        LiveChannel *c = &channels[i];
        snprintf(c->id, sizeof(c->id), "c%d", i);
        snprintf(c->name, sizeof(c->name), "%s", NAMES[i]);
        snprintf(c->number, sizeof(c->number), "%d", i + 1);
        c->now = (LiveProgram){.start = now - 600 - i * 300, .end = now + 1200 + i * 300};
        snprintf(c->now.title, sizeof(c->now.title), "%s", NOW[i]);
        c->next = (LiveProgram){.start = c->now.end, .end = c->now.end + 1800 + i * 900};
        snprintf(c->next.title, sizeof(c->next.title), "%s", NEXT[i]);
        c->programs[0] = c->now;
        c->programs[1] = c->next;
        c->program_count = 2;
        for (int64_t start = c->next.end; start < now + 3 * 3600; c->program_count++) {
            LiveProgram *p = &c->programs[c->program_count];
            *p = (LiveProgram){.start = start, .end = start + 1800 + (c->program_count % 3) * 900};
            snprintf(p->title, sizeof(p->title), "%s", LATER[(i + c->program_count) % 5]);
            start = p->end;
        }
    }
    channels[3].now.title[0] = '\0';
    memmove(&channels[3].programs[0], &channels[3].programs[1], (size_t)(channels[3].program_count - 1) * sizeof(LiveProgram));
    channels[3].program_count--;  // No guide for now on the last channel, only later.
    LiveGuide guide = {channels, 4};
    static LibView view;
    static Library lib;   // The tab doesn't need videos.
    static Home home;
    static ResumeList resume;
    libraryInit(&lib, "jellyfin:");
    libviewInit(&view);
    libviewSetTabs(&view, &lib, true, true, false);
    view.tab = TAB_LIVE;
    libviewLiveMove(&view, 4, 1);
    uiLibrarySetSource("Jellyfin", uiServerColor(SERVER_JELLYFIN));
    begin();
    uiDrawLibrary(&view, &lib, &home, &resume, true, NULL);
    uiDrawLiveTab(&view, &guide, now, -6 * 3600, false, false, NULL, false, -1);
    uiDrawFooterHelp(T("[A] watch  [^v] channel  [<>] guide  [ZL][ZR] tabs  [-] settings  [+] menu"));
    save("live_tab");
    const int row2_y = 116 + 76 + 38;  // The middle of the second channel's row.
    if (uiLibraryTap(100, row2_y).kind != LIBHIT_LIVE_ROW) {
        printf("TOUCH: live_tab: a channel's name doesn't answer a tap\n");
        g_problems++;
    }
    LibHit program = uiLibraryTap(GFX_WIDTH - 60, row2_y);
    if (program.kind != LIBHIT_LIVE_PROGRAM || program.a != 1 || program.b < 1) {
        printf("TOUCH: live_tab: a later program doesn't answer a tap\n");
        g_problems++;
    }
    // The cursor on a later program, the view moved on to it.
    view.live_at = liveStep(&channels[1], liveStep(&channels[1], 0, now, 1), now, 1);
    view.live_at = liveStep(&channels[1], view.live_at, now, 1);
    begin();
    uiDrawLibrary(&view, &lib, &home, &resume, true, NULL);
    uiDrawLiveTab(&view, &guide, now, -6 * 3600, false, false, NULL, false, -1);
    uiDrawFooterHelp(T("[A] watch  [^v] channel  [<>] guide  [ZL][ZR] tabs  [-] settings  [+] menu"));
    save("live_guide_later");
    view.live_at = 0;
    // An IPTV list's group (made-up), and its groups over the guide.
    begin();
    uiDrawLibrary(&view, &lib, &home, &resume, true, NULL);
    uiDrawLiveTab(&view, &guide, now, -6 * 3600, false, false, "News", true, 0);
    uiDrawFooterHelp(T("[A] watch  [^v] channel  [<>] guide  [Y] groups  [R] favorite  [ZL][ZR] tabs"));
    save("live_iptv");
    if (uiLibraryTap(GFX_WIDTH - MARGIN - 20, 631).kind != LIBHIT_FAVORITE) {
        printf("TOUCH: live_iptv: the favorite button doesn't answer a tap\n");
        g_problems++;
    }
    if (uiLibraryTap(MARGIN + 80, 84 + 13).kind != LIBHIT_LIVE_GROUPS) {
        printf("TOUCH: live_iptv: the group pill doesn't answer a tap\n");
        g_problems++;
    }
    static IptvGroup groups[] = {{"News", "", 12}, {"Sports", "", 30}, {"Kids", "", 8}, {"Music", "", 15},
                                 {"Movies", "", 22}, {"Documentaries", "", 9}, {"Local", "", 4}};
    IptvList groups_list = {.channels = NULL, .count = 100, .groups = groups, .group_count = 7};
    begin();
    uiDrawLibrary(&view, &lib, &home, &resume, true, NULL);
    uiDrawLiveTab(&view, &guide, now, -6 * 3600, false, false, "News", true, 1);
    uiDrawLiveGroups(&groups_list, 2, 1);
    uiDrawFooterHelp(T("[A] choose  [B] back"));
    save("live_groups");
    if (uiLibraryTap(GFX_WIDTH / 2, 64 + 56 + 2 * 52 + 26).kind != LIBHIT_LIVE_GROUP_ROW) {
        printf("TOUCH: live_groups: a group doesn't answer a tap\n");
        g_problems++;
    }

    // The IPTV view's Home (made-up channels): Recently watched, Favorites, then a row a group.
    makeFakeThumbs();  // The logos (made again later, the same).
    static const char *const IPTV_NAMES[] = {"Canal Uno", "Noticias 24", "Deportes Max", "Kids Planet", "Musica Viva",
                                        "Cine Clasico", "Docu World", "Local Siete", "Noticias Norte", "Gol TV"};
    static IptvChannel iptv_channels[10];
    static IptvGroup home_groups[] = {{"News", "", 3}, {"Sports", "", 2}, {"Kids", "", 1}, {"Music", "", 1},
                                      {"Movies", "", 2}, {"Local", "", 1}};
    static const int GROUP_OF[] = {0, 0, 1, 2, 3, 4, 4, 5, 0, 1};
    for (int i = 0; i < 10; i++) {
        snprintf(iptv_channels[i].name, sizeof(iptv_channels[i].name), "%s", IPTV_NAMES[i]);
        if (i % 4 != 3)  // Some have no logo: their name on a tile.
            snprintf(iptv_channels[i].logo, sizeof(iptv_channels[i].logo), "https://logo.example/%d.png", i);
        iptv_channels[i].group = GROUP_OF[i];
    }
    IptvList home_list = {.channels = iptv_channels, .count = 10, .groups = home_groups, .group_count = 6};
    static IptvMarks marks;
    iptvMarksInit(&marks);
    iptvMarksWatched(&marks, "Gol TV");
    iptvMarksWatched(&marks, "Kids Planet");
    iptvMarksWatched(&marks, "Noticias 24");
    iptvMarksToggleFavorite(&marks, "Noticias 24");
    iptvMarksToggleFavorite(&marks, "Docu World");
    static IptvHome iptv_home;
    static IptvHomeView home_view;
    iptvHomeBuild(&iptv_home, &home_list, &marks);
    iptvHomeViewReset(&home_view);
    libviewInit(&view);
    libviewSetIptvTabs(&view, &lib);
    uiLibrarySetSource("IPTV", UI_IPTV_COLOR);
    begin();
    uiDrawLibrary(&view, &lib, &home, &resume, true, NULL);
    uiDrawIptvHome(&iptv_home, &home_view, &home_list, &marks, NULL, NULL, false);
    uiDrawFooterHelp(T("[A] watch  [R] favorite  [X] source  [ZL][ZR] tabs  [-] settings  [+] menu"));
    save("iptv_home");
    LibHit card = uiLibraryTap(MARGIN + 20, 76 + 34 + 40);
    if (card.kind != LIBHIT_HOME_CARD || card.a != 0 || card.b != 0) {
        printf("TOUCH: iptv_home: the first channel doesn't answer a tap\n");
        g_problems++;
    }
    card = uiLibraryTap(MARGIN + 222 + 20, 76 + 196 + 34 + 40);
    if (card.kind != LIBHIT_HOME_CARD || card.a != 1 || card.b != 1) {
        printf("TOUCH: iptv_home: a favorite doesn't answer a tap\n");
        g_problems++;
    }
    if (uiLibraryTap(GFX_WIDTH - MARGIN - 20, 76 + 11).kind != LIBHIT_FAVORITE) {
        printf("TOUCH: iptv_home: the favorite button doesn't answer a tap\n");
        g_problems++;
    }
    bool home_tabs = false;
    for (int x = 0; x < GFX_WIDTH && !home_tabs; x += 4)
        home_tabs = uiLibraryTap(x, 34).kind == LIBHIT_TAB && uiLibraryTap(x, 34).a == 1;
    if (!home_tabs) {
        printf("TOUCH: iptv_home: the Live TV tab doesn't answer a tap\n");
        g_problems++;
    }
    // Further down, on a group's row moved along.
    iptvHomeMove(&home_view, &iptv_home, 0, 3, UI_IPTVHOME_ROWS, UI_IPTVHOME_COLS);
    iptvHomeMove(&home_view, &iptv_home, 1, 0, UI_IPTVHOME_ROWS, UI_IPTVHOME_COLS);
    begin();
    uiDrawLibrary(&view, &lib, &home, &resume, true, NULL);
    uiDrawIptvHome(&iptv_home, &home_view, &home_list, &marks, NULL, NULL, false);
    uiDrawFooterHelp(T("[A] watch  [R] favorite  [X] source  [ZL][ZR] tabs  [-] settings  [+] menu"));
    save("iptv_home_down");
    static IptvHome empty_home;
    begin();
    uiDrawLibrary(&view, &lib, &home, &resume, true, NULL);
    uiDrawIptvHome(&empty_home, &home_view, &home_list, &marks, NULL, NULL, true);
    save("iptv_home_loading");
    iptvHomeFree(&iptv_home);
    // An Xtream account's films and series (made-up), in the IPTV view's tabs.
    static const char *const FILMS_JSON =
        "[{\"name\":\"Paper Boats (2019)\",\"stream_id\":1,\"stream_icon\":\"http://img.example/1.jpg\",\"category_id\":\"1\"},"
        "{\"name\":\"Night Desk (2021)\",\"stream_id\":2,\"stream_icon\":\"http://img.example/2.jpg\",\"category_id\":\"2\"},"
        "{\"name\":\"Harbor Lights\",\"stream_id\":3,\"category_id\":\"2\"},"
        "{\"name\":\"The Quiet Workshop (2017)\",\"stream_id\":4,\"stream_icon\":\"http://img.example/4.jpg\",\"category_id\":\"3\"},"
        "{\"name\":\"Morning Lanterns (2022)\",\"stream_id\":5,\"stream_icon\":\"http://img.example/5.jpg\",\"category_id\":\"1\"},"
        "{\"name\":\"Kite Hill (2015)\",\"stream_id\":6,\"stream_icon\":\"http://img.example/6.jpg\",\"category_id\":\"1\"}]";
    static const char *const FILM_CATEGORIES_JSON =
        "[{\"category_id\":\"1\",\"category_name\":\"Family\"},{\"category_id\":\"2\",\"category_name\":\"Drama\"},"
        "{\"category_id\":\"3\",\"category_name\":\"Documentaries\"}]";
    static const char *const SHOWS_JSON =
        "[{\"name\":\"Evening Report\",\"series_id\":7,\"cover\":\"http://img.example/7.jpg\",\"plot\":\"News.\"}]";
    static XtreamCatalog vod;
    xtreamParseVodCategories(FILM_CATEGORIES_JSON, &vod, false);
    xtreamParseFilms(FILMS_JSON, &vod);
    xtreamParseShows(SHOWS_JSON, &vod);
    static Library vod_lib;
    libraryInit(&vod_lib, "iptv:");
    xtreamFillLibrary(&vod_lib, &vod, T("Reading the episodes..."), T("The episodes couldn't be read."));
    libraryBuild(&vod_lib);
    libviewInit(&view);
    libviewSetIptvTabs(&view, &vod_lib);
    libviewShowTab(&view, TAB_FILMS);
    uiLibrarySetSource("IPTV", UI_IPTV_COLOR);
    uiLibrarySetGridGroup(GRID_FILMS, T("All films"));
    begin();
    uiDrawLibrary(&view, &vod_lib, &home, &resume, true, NULL);
    uiDrawFooterHelp(T("[A] open  [Y] categories  [ZL][ZR] tabs  [-] settings  [+] menu"));
    save("iptv_films");
    if (uiLibraryTap(200, 90).kind != LIBHIT_LIVE_GROUPS) {
        printf("TOUCH: iptv_films: the category pill doesn't answer a tap\n");
        g_problems++;
    }
    // One category chosen, and the list of them.
    static int family[8];
    int family_count = (int)libraryGroupRefs(&vod_lib, false, 0, family, 8);
    libviewSetGridOnly(&view, GRID_FILMS, family, family_count);
    uiLibrarySetGridGroup(GRID_FILMS, vod.film_groups[0].name);
    begin();
    uiDrawLibrary(&view, &vod_lib, &home, &resume, true, NULL);
    uiDrawFooterHelp(T("[A] open  [Y] categories  [ZL][ZR] tabs  [-] settings  [+] menu"));
    save("iptv_films_category");
    begin();
    uiDrawLibrary(&view, &vod_lib, &home, &resume, true, NULL);
    uiDrawGroupList(T("Categories"), T("All films"), vod.film_count, vod.film_groups, vod.film_group_count, 1, 1);
    uiDrawFooterHelp(T("[A] choose  [^v] category  [B] back"));
    save("iptv_categories");
    for (int r = 0; r <= vod.film_group_count; r++) {
        LibHit hit = uiLibraryTap(400, 120 + r * 52 + 26);
        if (hit.kind != LIBHIT_LIVE_GROUP_ROW || hit.a != r) {
            printf("TOUCH: iptv_categories: row %d doesn't answer a tap\n", r);
            g_problems++;
        }
    }
    libviewSetGridOnly(&view, GRID_FILMS, NULL, 0);
    uiLibrarySetGridGroup(GRID_FILMS, NULL);
    bool vod_tabs[TAB_COUNT] = {false};
    for (int x = 0; x < GFX_WIDTH; x += 4) {
        LibHit hit = uiLibraryTap(x, 34);
        if (hit.kind == LIBHIT_TAB && hit.a >= 0 && hit.a < view.tab_count)
            vod_tabs[view.tabs[hit.a].kind] = true;
    }
    if (!vod_tabs[TAB_FILMS] || !vod_tabs[TAB_SERIES] || !vod_tabs[TAB_SEARCH]) {
        printf("TOUCH: iptv_films: the Films, Series or Search tab doesn't answer a tap\n");
        g_problems++;
    }
    libviewShowTab(&view, TAB_SERIES);
    libviewOpenShow(&view, &vod_lib, 0, -1);
    begin();
    uiDrawLibrary(&view, &vod_lib, &home, &resume, true, NULL);
    uiDrawFooterHelp(T("[A] play  [X] watched  [<>] season  [B] back  [Y] search  [ZL][ZR] tabs  [+] menu"));
    save("iptv_show_reading");
    libraryFree(&vod_lib);
    xtreamCatalogFree(&vod);
    // Stremio addons (made-up ones): their catalogs as tabs, and the streams to choose from.
    static StremioStore addons;
    memset(&addons, 0, sizeof(addons));
    addons.list_count = 2;
    snprintf(addons.lists[0].title, sizeof(addons.lists[0].title), "Popular \xC2\xB7 %s", T("Films"));
    snprintf(addons.lists[0].type, sizeof(addons.lists[0].type), "movie");
    stremioParseMetas("{\"metas\":[{\"id\":\"kt1\",\"type\":\"movie\",\"name\":\"Paper Boats\",\"releaseInfo\":\"2019\","
                      "\"poster\":\"https://img.example/1.jpg\"},{\"id\":\"kt2\",\"type\":\"movie\",\"name\":\"Night Desk\","
                      "\"poster\":\"https://img.example/2.jpg\"},{\"id\":\"kt3\",\"type\":\"movie\",\"name\":\"Harbor Lights\"},"
                      "{\"id\":\"kt4\",\"type\":\"movie\",\"name\":\"Morning Lanterns\",\"poster\":\"https://img.example/4.jpg\"}]}",
                      &addons.lists[0].metas, &addons.lists[0].count);
    snprintf(addons.lists[1].title, sizeof(addons.lists[1].title), "Popular \xC2\xB7 %s", T("Series"));
    snprintf(addons.lists[1].type, sizeof(addons.lists[1].type), "series");
    stremioParseMetas("{\"metas\":[{\"id\":\"kts9\",\"type\":\"series\",\"name\":\"Kite Hill\","
                      "\"poster\":\"https://img.example/9.jpg\"}]}",
                      &addons.lists[1].metas, &addons.lists[1].count);
    static Library addon_lib;
    libraryInit(&addon_lib, STREMIO_PREFIX);
    stremioFillLibrary(&addon_lib, &addons, T("Reading the episodes..."), T("The episodes couldn't be read."));
    libraryBuild(&addon_lib);
    libviewInit(&view);
    libviewSetTabs(&view, &addon_lib, true, false, false);
    view.tab = TAB_COLLECTION;
    view.collection = 0;
    uiLibrarySetSource(T("Addons"), UI_ADDONS_COLOR);
    begin();
    uiDrawLibrary(&view, &addon_lib, &home, &resume, true, NULL);
    uiDrawFooterHelp(T("[A] open  [Y] search  [ZL][ZR] tabs  [-] settings  [+] menu"));
    save("addons_catalog");
    bool catalog_tabs = false;
    for (int x = 0; x < GFX_WIDTH && !catalog_tabs; x += 4) {
        LibHit hit = uiLibraryTap(x, 34);
        catalog_tabs = hit.kind == LIBHIT_TAB && hit.a >= 0 && hit.a < view.tab_count &&
                       view.tabs[hit.a].kind == TAB_COLLECTION && view.tabs[hit.a].collection == 1;
    }
    if (!catalog_tabs) {
        printf("TOUCH: addons_catalog: the series catalog's tab doesn't answer a tap\n");
        g_problems++;
    }
    static StremioStream streams[] = {
        {"https://cdn.example/a.mp4", "1080p", "Paper Boats 2019 1080p WEB 2.1 GB", "Kites"},
        {"https://cdn.example/b.mkv", "720p", "Paper Boats 2019 720p 1.2 GB", "Kites"},
        {"https://other.example/c.m3u8", NULL, "Adaptive (HLS)", "Harbor Streams"},
    };
    begin();
    uiDrawLibrary(&view, &addon_lib, &home, &resume, true, NULL);
    uiDrawStreams("Paper Boats", streams, 3, 1, 0, false, NULL);
    uiDrawFooterHelp(T("[A] play  [^v] stream  [B] back"));
    save("addons_streams");
    // The panel: 532 high, centered between the header and the help line; rows 64 from 64 below its top.
    int streams_top = (UI_TAB_BAR_H + UI_FOOTER_Y - 532) / 2;
    LibHit row = uiLibraryTap(GFX_WIDTH / 2, streams_top + 64 + 2 * 64 + 30);
    LibHit back = uiLibraryTap((GFX_WIDTH - 900) / 2 + 40, streams_top + 30);
    LibHit out = uiLibraryTap(GFX_WIDTH / 2, 690);
    if (row.kind != LIBHIT_STREAM_ROW || row.a != 2 || back.kind != LIBHIT_CLOSE || out.kind != LIBHIT_CLOSE) {
        printf("TOUCH: addons_streams: a stream, the back arrow or the outside doesn't answer a tap\n");
        g_problems++;
    }
    begin();
    uiDrawLibrary(&view, &addon_lib, &home, &resume, true, NULL);
    uiDrawStreams("Paper Boats", NULL, 0, 0, 0, true, NULL);
    save("addons_streams_asking");
    begin();
    uiDrawLibrary(&view, &addon_lib, &home, &resume, true, NULL);
    uiDrawStreams("Kite Hill \xC2\xB7 S01E02", NULL, 0, 0, 0, false, "Harbor Streams: it didn't answer");
    save("addons_streams_none");
    libraryFree(&addon_lib);
    stremioStoreFree(&addons);
    uiLibrarySetSource("", uiServerColor(SERVER_JELLYFIN));
    uiLibrarySetSource("", uiServerColor(SERVER_JELLYFIN));
    p->live = true;
    begin();
    drawPicture();
    uiDrawPlayerStatus();
    uiDrawPlayerBar(p, "KaiVideo Test One  \xC2\xB7  The Quiet Workshop", NULL, -1);
    save("player_live");
    p->live = false;
}

// "Skip intro" over a playing video, with the bar and without it.
static void skipScene(Player *p)
{
    begin();
    drawPicture();
    uiDrawPlayerStatus();
    uiDrawPlayerBar(p, "Rook - S01E03 - Reckoning.mkv", NULL, -1);
    uiDrawSkipButton("Skip intro", true);
    save("player_skip");
    if (uiLibraryTap(GFX_WIDTH - MARGIN - 30, GFX_HEIGHT - UI_PLAYER_BAR_H - 90).kind != LIBHIT_SKIP) {
        printf("TOUCH: player_skip: the skip button doesn't answer a tap\n");
        g_problems++;
    }
    begin();
    drawPicture();
    uiDrawSkipButton("Skip credits", false);
    save("player_skip_nobar");
}

// The statistics panel (↓) over a converted video.
// A line of subtitles in each font KaiVideo brings, accents and all.
static void fontsScene(void)
{
    for (int i = -1; i < fontBundledCount(); i++) {
        if (i < 0)
            fontSetSubtitleFile(NULL);
        else
            fontSetSubtitleBundled(i);
        begin();
        gfxClear(GFX_RGB(40, 46, 60));
        uiDrawSubtitles(i < 0 ? "KaiVideo: \xC2\xBFQu\xC3\xA9 tal? A\xC3\xB1o, ac\xC3\xA1, \xC2\xA1v\xC3\xA1monos!"
                              : "\xC2\xBFQu\xC3\xA9 tal? A\xC3\xB1o, ac\xC3\xA1, \xC2\xA1v\xC3\xA1monos!", false);
        char name[48];
        snprintf(name, sizeof(name), "font_%d", i + 1);
        save(name);
    }
    fontSetSubtitleFile(NULL);
}

static void statsScene(void)
{
    StatsInfo info = {
        .source = STATS_CONVERTED,
        .quality = "8 Mb/s",
        .video = "H.264 1920x1080, 24 fps",
        .sound = "aac, 48 kHz, 2 channels",
        .rates = {.valid = true, .fps = 23.98, .late_percent = 0.4, .mbps = 7.84},
        .ahead_s = 9.5,
        .ahead_mb = -1,
        .speed_permille = 1250,
    };
    char lines[STATS_LINES][STATS_LINE_MAX];
    begin();
    drawPicture();
    uiDrawStats(lines, statsFormat(&info, lines));
    save("player_stats");
}

// Every row drawn in an open list must answer a tap with its own index.
static void checkMenuTaps(const char *name, UiMenuKind kind, const Menu *m)
{
    for (int i = 0; i < menuVisible(m); i++) {
        int found = -1, row = -1;
        // The middle of row i: scan down the list's right half for it.
        for (int y = 0; y < GFX_HEIGHT && found < 0; y += 4) {
            if (uiTapTrackMenu(kind, m, kind == UI_MENU_SUBTITLES ? 1000 : 700, y, &row) == TAP_MENU_ROW &&
                row == m->first + i)
                found = y;
        }
        if (found < 0) {
            printf("TOUCH: %s: row %d doesn't answer a tap\n", name, m->first + i);
            g_problems++;
        }
    }
    int row;
    if (uiTapTrackMenu(kind, m, 100, 100, &row) != TAP_OUTSIDE) {
        printf("TOUCH: %s: a tap far away isn't outside the list\n", name);
        g_problems++;
    }
}

// --- Stand-ins for the console's thumbnails: a made-up frame per video ---

#define FAKE_THUMBS 8
static uint32_t g_fake[FAKE_THUMBS][THUMB_W * THUMB_H];

static void makeFakeThumbs(void)
{
    static const uint32_t SKY[FAKE_THUMBS][2] = {
        {0x2B3A6B, 0xE08A5A}, {0x0E2236, 0x4DA3FF}, {0x1F2E11, 0x9AC45A}, {0x33200E, 0xFFB86B},
        {0x2A102A, 0xC85AA0}, {0x101C2B, 0x7AB8D8}, {0x241710, 0xD89A6A}, {0x0F2A23, 0x5AD8B0},
    };
    for (int t = 0; t < FAKE_THUMBS; t++) {
        uint32_t a = SKY[t][0], b = SKY[t][1];
        for (int y = 0; y < THUMB_H; y++) {
            unsigned k = (unsigned)(y * 255 / THUMB_H);
            unsigned r = (((a >> 16) & 0xFF) * (255 - k) + ((b >> 16) & 0xFF) * k) / 255;
            unsigned g = (((a >> 8) & 0xFF) * (255 - k) + ((b >> 8) & 0xFF) * k) / 255;
            unsigned bl = ((a & 0xFF) * (255 - k) + (b & 0xFF) * k) / 255;
            for (int x = 0; x < THUMB_W; x++) {
                int dx = x - (60 + t * 37) % THUMB_W, dy = y - 70, hill = 150 + (int)(30 * ((x / 40 + t) % 3));
                bool sun = dx * dx + dy * dy < 900, ground = y > hill;
                g_fake[t][y * THUMB_W + x] = ground ? GFX_RGB(20, 22, 30) : sun ? GFX_RGB(255, 220, 150)
                                                                             : GFX_RGB(r, g, bl);
            }
        }
    }
}

const uint32_t *thumbGet(const char *path, int64_t size)
{
    (void)size;
    unsigned h = 0;
    for (const char *p = path; *p; p++)
        h = h * 31 + (unsigned char)*p;
    return h % 5 == 0 ? NULL : g_fake[h % FAKE_THUMBS];  // Some still being made.
}

const uint32_t *thumbGetLarge(const char *path, int64_t size)
{
    (void)path;
    (void)size;
    return NULL;  // The panel then shows the card's picture.
}

bool thumbIsNone(const char *path, int64_t size)
{
    (void)path;
    (void)size;
    return false;
}

// --- The library's screens ---

static Library g_lib;
static ResumeList g_resume;
static Watched g_watched;
static Home g_home;
static LibView g_view;

static void buildSampleLibrary(void)
{
    libraryInit(&g_lib, "sdmc:/Media");
    for (size_t i = 0; i < sizeof(SAMPLE_PATHS) / sizeof(SAMPLE_PATHS[0]); i++)
        libraryAdd(&g_lib, SAMPLE_PATHS[i], 900ll << 20, (int64_t)(i * 37 % 101));
    libraryBuild(&g_lib);
    const int64_t S = 1000000000;
    memset(&g_resume, 0, sizeof(g_resume));
    for (size_t i = 4; i < g_lib.count; i += 9)  // Not the watched episodes below.
        resumeRemember(&g_resume, g_lib.items[i].path, (int64_t)(200 + i * 13) * S, 2700 * S);
    // A series' first three episodes watched: its fourth is Up next on Home.
    watchedInit(&g_watched);
    for (size_t s = 0; s < g_lib.show_count; s++)
        if (strstr(g_lib.shows[s].name, "Prairie"))
            for (int e = 0; e < 3 && e < g_lib.shows[s].count; e++)
                watchedSet(&g_watched, g_lib.items[g_lib.shows[s].episodes[e]].path, true);
    watchedSet(&g_watched, g_lib.items[g_lib.films[1]].path, true);
    uiLibrarySetWatched(&g_watched);
    homeCompute(&g_home, &g_lib, &g_resume, &g_watched);
    libviewInit(&g_view);
}

#define HOME_HELP "[A] open  [Y] search  [ZL][ZR] tabs  [-] settings  [+] menu"

static void libraryHelpScene(const char *name, const char *help)
{
    begin();
    uiDrawLibrary(&g_view, &g_lib, &g_home, &g_resume, true, NULL);
    uiDrawFooterHelp(T(help));
    save(name);
}

// A notice fully in, as main.c draws it last: over everything.
static void drawNotice(NoticeKind kind, const char *message)
{
    Notices notices = {0};
    NoticeView views[NOTICE_STACK + 1];
    noticePush(&notices, kind, message, 1);
    noticeVisible(&notices, 1000000000ull, views);  // The first look starts it,
    int count = noticeVisible(&notices, 2000000000ull, views);  // and a second later it's in.
    uiDrawNotices(views, count);
}

static void libraryScene(const char *name, bool folder_set, const char *status, const char *message)
{
    begin();
    uiDrawLibrary(&g_view, &g_lib, &g_home, &g_resume, folder_set, status);
    uiDrawFooterHelp(T(HOME_HELP));
    if (message)
        drawNotice(NOTICE_ERROR, message);
    save(name);
    if (message && !uiNoticeAt(GFX_WIDTH / 2, 64 + 10 + 20)) {
        printf("TOUCH: %s: the notice doesn't answer a tap\n", name);
        g_problems++;
    }
}

// Every tab and the Folder button answer a tap on the tab bar.
static void checkTabTaps(const char *name)
{
    int found = 0;
    for (int x = 0; x < GFX_WIDTH; x += 4) {
        LibHit hit = uiLibraryTap(x, 34);
        if (hit.kind == LIBHIT_TAB)
            found |= 1 << hit.a;
        else if (hit.kind == LIBHIT_MENU)
            found |= 1 << g_view.tab_count;
    }
    if (found != (1 << (g_view.tab_count + 1)) - 1) {
        printf("TOUCH: %s: a tab or the Folder button doesn't answer a tap\n", name);
        g_problems++;
    }
}

// The + menu just drawn: down its middle, every item answers once and in order,
// the line after this screen's `screen` items is no item, and outside closes it.
static void checkPlusMenu(const char *name, int count, int screen)
{
    int next = 0, last = -1;
    bool ok = true;
    for (int y = 0; y < GFX_HEIGHT; y++) {
        LibHit hit = uiLibraryTap(GFX_WIDTH / 2, y);
        if (hit.kind != LIBHIT_MENU_ITEM || hit.a == last)
            continue;
        ok = ok && hit.a == next;
        last = hit.a;
        next++;
    }
    int divider = screen > 0 ? 12 : 0, h = 76 + count * 48 + divider + 20;
    int line_y = (GFX_HEIGHT - h) / 2 + 76 + screen * 48 + (divider - 6) / 2;
    if (screen > 0 && uiLibraryTap(GFX_WIDTH / 2, line_y).kind != LIBHIT_PANEL)
        ok = false;
    if (!ok || next != count || uiLibraryTap(20, 400).kind != LIBHIT_CLOSE) {
        printf("TOUCH: %s: an item, the line or the outside doesn't answer as drawn\n", name);
        g_problems++;
    }
}

// A Jellyfin server's libraries as tabs: the films in one, the series in another.
static void collectionScene(void)
{
    static Library lib;
    libraryInit(&lib, "jellyfin:");
    int films = libraryAddCollection(&lib, "Pel\xC3\xAD" "culas", COLLECTION_FILMS);
    int shows = libraryAddCollection(&lib, "Programas", COLLECTION_SERIES);
    for (size_t i = 0; i < g_lib.count; i++) {
        const LibraryItem *it = &g_lib.items[i];
        LibraryRemote remote = {.in_collection = (it->show >= 0 ? shows : films) + 1, .favorite = i % 5 == 0};
        libraryAddRemote(&lib, it->path, &it->name, it->size, it->added, &remote);
    }
    libraryBuild(&lib);
    static LibView view;
    static Home home;
    libviewInit(&view);
    homeCompute(&home, &lib, &g_resume, &g_watched);
    libviewSetTabs(&view, &lib, true, false, false);
    uiLibrarySetSource("Jellyfin", uiServerColor(SERVER_JELLYFIN));
    begin();
    uiDrawLibrary(&view, &lib, &home, &g_resume, true, NULL);
    uiDrawFooterHelp(T(HOME_HELP));
    save("server_home");
    libviewNextTab(&view, 1);
    begin();
    uiDrawLibrary(&view, &lib, &home, &g_resume, true, NULL);
    uiDrawFooterHelp(T(HOME_HELP));
    save("server_films");
    int found = 0;
    for (int x = 0; x < GFX_WIDTH; x += 4) {
        LibHit hit = uiLibraryTap(x, 34);
        if (hit.kind == LIBHIT_TAB)
            found |= 1 << hit.a;
    }
    if (found != (1 << view.tab_count) - 1) {
        printf("TOUCH: server_films: a library's tab doesn't answer a tap\n");
        g_problems++;
    }
    uiLibrarySetSource("", uiServerColor(SERVER_JELLYFIN));
    libraryFree(&lib);
}

// Downloads of the sample library: one of each kind, by episode.
static int fakeDownload(const char *path, const void *user, double *fraction)
{
    (void)user;
    *fraction = 0.42;
    if (strstr(path, "S02E01") || strstr(path, "Lanterns"))
        return UI_DL_DONE;
    if (strstr(path, "S02E02") || strstr(path, "Ashfall"))
        return UI_DL_ACTIVE;
    if (strstr(path, "S02E03"))
        return UI_DL_QUEUED;
    if (strstr(path, "S02E04"))
        return UI_DL_PAUSED;
    if (strstr(path, "S02E05"))
        return UI_DL_FAILED;
    return UI_DL_NONE;
}

// The Downloads panel: one of each state, more rows than fit, and its touch targets.
static void downloadsScene(void)
{
    static const UiDownloadRow ROWS[] = {
        {.title = "DOWNLOADING \xC2\xB7 4", .detail = "", .state = "", .progress = -1, .header = true},
        {"Dry Creek", "Prairie Road \xC2\xB7 S09E06", "1.1 GB of 2.3 GB", 0.48, false, false, false, false, NULL},
        {"Lamentia", "Rook \xC2\xB7 S01E03", "Waiting", -1, false, false, false, false, NULL},
        {"Pawns", "Rook \xC2\xB7 S01E02", "Paused \xC2\xB7 12%", 0.12, false, false, false, false, NULL},
        {"Quiet Harbor (2017)", "Film \xC2\xB7 converted, 8 Mb/s", "The card needs 2.1 GB more free for it.", 0.3, false,
         true, false, false, NULL},
        {.title = "ON THE CARD \xC2\xB7 2", .detail = "", .state = "", .progress = -1, .header = true},
        {"Paper Lanterns (1998)", "Film", "4.2 GB", -1, true, false, false, false, NULL},
        {"Ashfall (2010)", "Film \xC2\xB7 converted, 2 Mb/s", "1.3 GB", -1, true, false, false, false, NULL},
        {.title = "DELETED FROM THE CARD \xC2\xB7 1", .detail = "", .state = "", .progress = -1, .header = true,
         .action = "Clear"},
        {"Kite Hill (2004)", "Film", "Deleted from the card", -1, false, false, false, true, NULL},
    };
    int count = (int)(sizeof(ROWS) / sizeof(ROWS[0]));
    static LibView view;
    libviewInit(&view);
    libviewSetTabs(&view, &g_lib, false, false, true);
    libviewShowTab(&view, TAB_DOWNLOADS);
    for (int pass = 0; pass < 2; pass++) {
        Scroll s = pass ? (Scroll){.cursor = 9, .first = count - UI_DOWNLOAD_ROWS} : (Scroll){.cursor = 1};
        begin();
        uiDrawLibrary(&view, &g_lib, &g_home, &g_resume, true, NULL);
        uiDrawDownloadsTab(ROWS, count, &s, pass ? -1 : 4,
                           "4 downloading \xC2\xB7 2 on the card (5.5 GB) \xC2\xB7 1 deleted \xC2\xB7 2 watched offline, "
                           "told to the server when it's back",
                           "");
        uiDrawFooterHelp(pass ? T("[A] download again  [X] take off the list  [Y] clear deleted  [ZL][ZR] tabs  [+] menu")
                              : T("[A] pause / go on  [X] cancel  [ZL][ZR] tabs  [+] menu"));
        save(pass ? "downloads_end" : "downloads");
    }
    bool rows = false, deletes = false, heading = false, tab = false, clear = false;
    for (int y = 0; y < GFX_HEIGHT; y += 4)
        for (int x = 0; x < GFX_WIDTH; x += 8) {
            LibHit hit = uiLibraryTap(x, y);
            rows = rows || (hit.kind == LIBHIT_DOWNLOAD_ROW && hit.a == count - 1);
            deletes = deletes || (hit.kind == LIBHIT_DOWNLOAD_DELETE && hit.a == count - 1);
            heading = heading || ((hit.kind == LIBHIT_DOWNLOAD_ROW || hit.kind == LIBHIT_DOWNLOAD_DELETE) &&
                                  ROWS[hit.a].header);
            tab = tab || (hit.kind == LIBHIT_TAB && view.tabs[hit.a].kind == TAB_DOWNLOADS);
            clear = clear || hit.kind == LIBHIT_DOWNLOADS_CLEAR;
        }
    if (!rows || !deletes || heading || !tab || !clear) {
        printf("TOUCH: downloads: a row, its delete, Clear or its tab doesn't answer a tap, or a heading does\n");
        g_problems++;
    }
    if (uiLibraryTap(GFX_WIDTH / 2, 668).kind == LIBHIT_DOWNLOAD_ROW) {
        printf("TOUCH: downloads: a row below the list answers a tap\n");
        g_problems++;
    }
    begin();
    uiDrawLibrary(&view, &g_lib, &g_home, &g_resume, true, NULL);
    uiDrawDownloadsTab(ROWS, 0, &(Scroll){0}, -1, "0 downloading \xC2\xB7 0 on the card (0 MB) \xC2\xB7 0 deleted",
                       "Nothing yet: open a film or episode from a server and press L to download it.");
    uiDrawFooterHelp(T("[ZL][ZR] tabs  [-] settings  [+] menu"));
    save("downloads_empty");
}

static void libraryScenes(void)
{
    SysInfo sys = {.has_time = true, .hour = 21, .minute = 47, .has_battery = true, .battery_percent = 82};
    uiLibrarySetStatus(&sys, false);
    makeFakeThumbs();
    uiLibrarySetThumbnails(false);  // The color cards first.
    buildSampleLibrary();
    collectionScene();
    libraryScene("lib_home", true, NULL, NULL);
    checkTabTaps("lib_home");
    LibHit hit = uiLibraryTap(MARGIN + 20, 80 + 36 + 40);
    if (hit.kind != LIBHIT_HOME_CARD || hit.a != 0 || hit.b != 0) {
        printf("TOUCH: lib_home: the first card doesn't answer a tap\n");
        g_problems++;
    }
    libviewHomeMove(&g_view, &g_home, 2, 1);
    libraryScene("lib_home_moved", true, "Looking for videos... 312", NULL);

    g_view.tab = TAB_FILMS;
    libviewGridMove(&g_view, GRID_FILMS, (int)g_lib.film_count, 1, 0);
    libraryScene("lib_films", true, NULL, NULL);
    // Left by a finger between rows: cut under the tabs, and only what shows answers a tap.
    scrollPx(&g_view.grid[GRID_FILMS], 90, LIBVIEW_GRID_ROW_PX, LIBVIEW_GRID_ROWS, LIBVIEW_COLS,
                    (int)g_lib.film_count);
    libraryScene("lib_films_between", true, NULL, NULL);
    if (uiLibraryTap(GFX_WIDTH / 2, UI_TAB_BAR_H + 4).kind == LIBHIT_GRID_CARD) {
        printf("TOUCH: lib_films_between: a card answers a tap above its list\n");
        g_problems++;
    }
    libviewGridMove(&g_view, GRID_FILMS, (int)g_lib.film_count, 0, 0);  // A button: back on a row.
    g_view.tab = TAB_SERIES;
    libraryScene("lib_series", true, NULL, NULL);

    for (size_t s = 0; s < g_lib.show_count; s++)
        if (strstr(g_lib.shows[s].name, "Prairie"))
            libviewOpenShow(&g_view, &g_lib, (int)s, -1);
    libviewShowMove(&g_view, &g_lib, 1, 3);
    uiLibrarySetDownloads(fakeDownload, NULL);
    uiLibrarySetDownloadsPill("2 \xC2\xB7 42%", 0.42);
    begin();
    uiDrawLibrary(&g_view, &g_lib, &g_home, &g_resume, true, NULL);
    uiDrawShowFavorite(false);
    uiDrawShowDownload("Download season", false, true, false);
    uiDrawFooterHelp(T("[A] play  [X] watched  [R] favorite  [L] download  [<>] season  [B] back  [ZL][ZR] tabs"));
    save("lib_show_server");
    bool pill = false;
    for (int x = GFX_WIDTH / 2; x < GFX_WIDTH; x += 4)
        pill = pill || uiLibraryTap(x, 30).kind == LIBHIT_DOWNLOADS;
    if (!pill) {
        printf("TOUCH: lib_show_server: the downloads pill doesn't answer a tap\n");
        g_problems++;
    }
    if (uiLibraryTap(GFX_WIDTH - MARGIN - 20, UI_TAB_BAR_H + 38).kind != LIBHIT_FAVORITE) {
        printf("TOUCH: lib_show_server: the favorite button doesn't answer a tap\n");
        g_problems++;
    }
    bool season_download = false;
    for (int x = GFX_WIDTH / 3; x < GFX_WIDTH; x += 4)
        season_download = season_download || uiLibraryTap(x, UI_TAB_BAR_H + 38).kind == LIBHIT_SHOW_DOWNLOAD;
    if (!season_download) {
        printf("TOUCH: lib_show_server: the season's download button doesn't answer a tap\n");
        g_problems++;
    }
    g_view.show = -1;
    g_view.tab = TAB_FILMS;
    libraryScene("lib_films_downloads", true, NULL, NULL);
    for (size_t s = 0; s < g_lib.show_count; s++)
        if (strstr(g_lib.shows[s].name, "Prairie"))
            libviewOpenShow(&g_view, &g_lib, (int)s, -1);
    libviewShowMove(&g_view, &g_lib, 1, 3);
    uiLibrarySetDownloads(NULL, NULL);
    uiLibrarySetDownloadsPill("", 0);
    libraryScene("lib_show", true, NULL, NULL);
    if (uiLibraryTap(MARGIN + 10, 100).kind != LIBHIT_BACK) {
        printf("TOUCH: lib_show: Back doesn't answer a tap\n");
        g_problems++;
    }

    g_view.show = -1;
    g_view.tab = TAB_SEARCH;
    libraryScene("lib_search_empty", true, NULL, NULL);
    libviewSearch(&g_view, &g_lib, "lo");
    libviewResultsMove(&g_view, 1);
    libraryScene("lib_search", true, NULL, NULL);
    libviewSearch(&g_view, &g_lib, "zzzz");
    libraryScene("lib_search_none", true, NULL, NULL);

    g_view.tab = TAB_HOME;
    g_view.show = -1;
    uiLibrarySetThumbnails(true);
    libraryScene("thumbs_home", true, NULL, NULL);
    g_view.tab = TAB_FILMS;
    libraryScene("thumbs_films", true, NULL, NULL);
    g_view.tab = TAB_SERIES;
    for (size_t s = 0; s < g_lib.show_count; s++)
        if (strstr(g_lib.shows[s].name, "Prairie"))
            libviewOpenShow(&g_view, &g_lib, (int)s, -1);
    libviewShowMove(&g_view, &g_lib, 0, 3);  // The cursor on the next to watch.
    libraryHelpScene("thumbs_show", "[A] play  [X] watched  [<>] season  [B] back  [Y] search  [ZL][ZR] tabs  [+] menu");
    // Each episode's watched circle answers a tap, on its row.
    LibHit circle = uiLibraryTap(GFX_WIDTH - MARGIN - 12, 196 + 58 + 29);
    if (circle.kind != LIBHIT_WATCHED || circle.a != 1) {
        printf("TOUCH: thumbs_show: an episode's watched circle doesn't answer a tap\n");
        g_problems++;
    }

    // The next episode, on the right of the player in the last seconds: with
    // the bar, and with it hidden (-).
    int next = g_lib.shows[g_view.show].episodes[3];
    Player playing = {.paused = false, .audio = 0, .subtitle = 0, .audio_count = 1, .subtitle_count = 2};
    for (int with_bar = 1; with_bar >= 0; with_bar--) {
        begin();
        drawPicture();
        uiDrawSubtitles("No, I'm not going back there.", with_bar);
        if (with_bar)
            uiDrawPlayerBar(&playing, "Prairie Road \xC2\xB7 S01E03 \xC2\xB7 Windmill Weather", NULL, -1);
        uiLibraryResetHits();
        uiDrawUpNextCard(&g_lib, next, 0.6, 18, with_bar);
        save(with_bar ? "up_next" : "up_next_hidden");
        // The card answers a tap (plays it), its X closes it, and the picture outside is neither.
        int card_x = GFX_WIDTH - MARGIN - 300, bottom = with_bar ? GFX_HEIGHT - 120 - 76 : GFX_HEIGHT - 40;
        LibHit card = uiLibraryTap(card_x + 100, bottom - 40), close = uiLibraryTap(GFX_WIDTH - MARGIN - 26,
                                                                                       bottom - (12 + 155 + 96) + 26);
        if (card.kind != LIBHIT_UP_NEXT || close.kind != LIBHIT_CLOSE || uiLibraryTap(300, 300).kind != LIBHIT_NONE) {
            printf("TOUCH: up_next: the card or its close button doesn't answer as drawn\n");
            g_problems++;
        }
    }
    g_view.show = -1;
    g_view.tab = TAB_HOME;

    // A server's library wears its color: Plex's here.
    Color chosen_accent = g_colors.accent;
    g_colors.accent = uiServerColor(SERVER_PLEX);
    uiLibrarySetSource("Plex", uiServerColor(SERVER_PLEX));
    begin();
    uiDrawLibrary(&g_view, &g_lib, &g_home, &g_resume, true, NULL);
    uiDrawFooterHelp(T("[A] open  [Y] search  [ZL][ZR] tabs  [-] settings  [+] menu"));
    save("library_plex");
    g_colors.accent = chosen_accent;
    // The network folder as the library's source (its color is app_library.c's NETWORK_COLOR).
    uiLibrarySetSource(T("Network folder"), GFX_RGB(0x2E, 0xC4, 0xB6));
    begin();
    uiDrawLibrary(&g_view, &g_lib, &g_home, &g_resume, true, NULL);
    uiDrawFooterHelp(T("[A] open  [Y] search  [ZL][ZR] tabs  [-] settings  [+] menu"));
    save("library_network");
    uiLibrarySetSource("", uiServerColor(SERVER_JELLYFIN));

    // Settings and About over Home, in another theme.
    Config config;
    configDefaults(&config);
    config.theme = 2;
    config.accent = 1;
    themeApply(config.theme, config.accent);
    begin();
    uiDrawLibrary(&g_view, &g_lib, &g_home, &g_resume, true, NULL);
    static const char *const USERS[SERVER_KIND_COUNT] = {"mizu", "", "mizu"};
    uiSettingsSetServers(USERS);
    uiDrawSettings(&config, SETTINGS_PAGE_MAIN, SETTING_OPEN_PLEX, "sdmc:/Media/Videos", "1.9.0");
    uiDrawFooterHelp(T("[^v] choose  [>][A] open  [B] close"));
    save("settings");
    // The main page's rows: 48 high from 76 below the panel's top (10 rows, a 60 footer).
    int top = (GFX_HEIGHT - (76 + 10 * 48 + 60)) / 2 + 76;
    LibHit about = uiLibraryTap(GFX_WIDTH / 2 - 200, top + 9 * 48 + 20), outside = uiLibraryTap(20, 400);
    LibHit plex_row = uiLibraryTap(GFX_WIDTH / 2 - 200, top + 4 * 48 + 20);
    if (about.kind != LIBHIT_SETTING || about.a != SETTING_ABOUT || outside.kind != LIBHIT_CLOSE ||
        plex_row.kind != LIBHIT_SETTING || plex_row.a != SETTING_OPEN_PLEX) {
        printf("TOUCH: settings: a row or the outside doesn't answer as drawn (%d %d %d)\n", about.kind, plex_row.kind,
               outside.kind);
        g_problems++;
    }
    begin();
    uiDrawLibrary(&g_view, &g_lib, &g_home, &g_resume, true, NULL);
    uiDrawSettings(&config, SETTINGS_PAGE_LIBRARY, SETTING_THUMBNAILS, "sdmc:/Media/Videos", "1.9.0");
    uiDrawFooterHelp(T("[^v] choose  [<>][A] change  [B] back"));
    save("settings_library");
    top = (GFX_HEIGHT - (76 + 3 * 48 + 60)) / 2 + 76;
    LibHit thumbs = uiLibraryTap((GFX_WIDTH + 760) / 2 - 40, top + 2 * 48 + 20);
    if (thumbs.kind != LIBHIT_SETTING_NEXT || thumbs.a != SETTING_THUMBNAILS) {
        printf("TOUCH: settings_library: Thumbnails' arrow doesn't answer a tap\n");
        g_problems++;
    }
    // The addons' page: two added, the second asked to be removed.
    static const char *const ADDON_NAMES[] = {"Kites", "Harbor Streams"};
    uiSettingsSetAddons(ADDON_NAMES, 2, 1, "Addons: 2, catalogs: 3");
    settingsSetAddonCount(2);
    begin();
    uiDrawLibrary(&g_view, &g_lib, &g_home, &g_resume, true, NULL);
    uiDrawSettings(&config, SETTINGS_PAGE_ADDONS, SETTING_ADDON_FIRST + 1, "sdmc:/Media/Videos", "1.9.0");
    uiDrawFooterHelp(T("[^v] choose  [<>][A] change  [B] back"));
    save("settings_addons");
    int addons_top = (GFX_HEIGHT - (76 + 30 + 4 * 48 + 60)) / 2 + 76 + 30;
    LibHit second = uiLibraryTap(GFX_WIDTH / 2 - 200, addons_top + 2 * 48 + 20);
    if (second.kind != LIBHIT_SETTING || second.a != SETTING_ADDON_FIRST + 1) {
        printf("TOUCH: settings_addons: an addon's row doesn't answer a tap (%d %d)\n", second.kind, second.a);
        g_problems++;
    }
    settingsSetAddonCount(0);
    begin();
    uiDrawLibrary(&g_view, &g_lib, &g_home, &g_resume, true, NULL);
    uiDrawSettings(&config, SETTINGS_PAGE_DISPLAY, SETTING_THEME, "sdmc:/Media/Videos", "1.9.0");
    uiDrawFooterHelp(T("[^v] choose  [<>][A] change  [B] back"));
    save("settings_display");
    top = (GFX_HEIGHT - (76 + 5 * 48 + 60)) / 2;
    if (uiLibraryTap((GFX_WIDTH - 760) / 2 + 60, top + 33).kind != LIBHIT_PAGE_BACK) {
        printf("TOUCH: settings_display: Back doesn't answer a tap\n");
        g_problems++;
    }
    begin();  // About: the credits, and updates, with a newer version found.
    uiDrawLibrary(&g_view, &g_lib, &g_home, &g_resume, true, NULL);
    char found[64];
    snprintf(found, sizeof(found), T("%s is out: A installs it"), "1.14.0");
    uiSettingsSetUpdate(found);
    uiDrawSettings(&config, SETTINGS_PAGE_ABOUT, SETTING_UPDATE_NOW, "sdmc:/Media/Videos", "1.9.0");
    uiDrawFooterHelp(T("[^v] choose  [<>][A] change  [B] back"));
    save("settings_about");
    begin();  // The IPTV page, an Xtream account set (made-up).
    uiDrawLibrary(&g_view, &g_lib, &g_home, &g_resume, true, NULL);
    uiSettingsSetIptv("Xtream", "http://tv.example:8080", "kai", "", "412 channels", true);
    uiDrawSettings(&config, SETTINGS_PAGE_IPTV, SETTING_IPTV_LOAD, "sdmc:/Media/Videos", "1.9.0");
    uiDrawFooterHelp(T("[^v] choose  [<>][A] change  [B] back"));
    save("settings_iptv");
    begin();  // A notice over Settings.
    uiDrawLibrary(&g_view, &g_lib, &g_home, &g_resume, true, NULL);
    uiDrawSettings(&config, SETTINGS_PAGE_ABOUT, SETTING_UPDATE_NOW, "sdmc:/Media/Videos", "1.9.0");
    uiDrawFooterHelp(T("[^v] choose  [<>][A] change  [B] back"));
    snprintf(found, sizeof(found), T("KaiVideo %s is installed: it runs from the next start."), "1.14.0");
    drawNotice(NOTICE_DONE, found);
    save("notice_settings");
    begin();
    uiDrawLibrary(&g_view, &g_lib, &g_home, &g_resume, true, NULL);
    uiSettingsSetJellyfin("192.168.0.10:8096", "");
    uiDrawSettings(&config, SETTINGS_PAGE_JELLYFIN, SETTING_JF_CODE, "sdmc:/Media/Videos", "1.9.0");
    uiDrawFooterHelp(T("[^v] choose  [<>][A] change  [B] back"));
    save("settings_jellyfin");
    top = (GFX_HEIGHT - (76 + 30 + 7 * 48 + 60)) / 2 + 76 + 30;  // A server's page: a line under its title.
    LibHit account = uiLibraryTap(GFX_WIDTH / 2 - 200, top + 3 * 48 + 20);
    if (account.kind != LIBHIT_SETTING || account.a != SETTING_JF_CODE) {
        printf("TOUCH: settings_jellyfin: the account row doesn't answer a tap\n");
        g_problems++;
    }
    begin();
    uiDrawLibrary(&g_view, &g_lib, &g_home, &g_resume, true, NULL);
    Config plex_config = config;
    plex_config.server_kind = SERVER_PLEX;
    uiSettingsSetJellyfin("", "");
    uiDrawSettings(&plex_config, SETTINGS_PAGE_JELLYFIN, SETTING_PLEX_CODE, "sdmc:/Media/Videos", "1.9.0");
    uiDrawFooterHelp(T("[^v] choose  [<>][A] change  [B] back"));
    save("settings_plex");
    top = (GFX_HEIGHT - (76 + 30 + 2 * 48 + 60)) / 2 + 76 + 30;
    LibHit plex_code = uiLibraryTap(GFX_WIDTH / 2 - 200, top + 20);
    if (plex_code.kind != LIBHIT_SETTING || plex_code.a != SETTING_PLEX_CODE) {
        printf("TOUCH: settings_plex: the sign-in row doesn't answer a tap\n");
        g_problems++;
    }
    begin();
    uiDrawLibrary(&g_view, &g_lib, &g_home, &g_resume, true, NULL);
    uiDrawSignInCode(true, "K7QX", "", false);
    uiDrawFooterHelp(T("[B] cancel"));
    save("plex_code");
    begin();
    uiDrawLibrary(&g_view, &g_lib, &g_home, &g_resume, true, NULL);
    uiDrawSignInCode(false, "482913", "192.168.0.10:8096", false);
    uiDrawFooterHelp(T("[B] cancel"));
    save("jellyfin_code");
    begin();
    uiDrawLoading("Paper Lanterns (1998)", 1.0f, false);
    uiDrawFooterHelp(T("[B] cancel"));
    save("loading");
    begin();
    uiLibrarySetSource("Jellyfin", uiServerColor(SERVER_JELLYFIN));
    uiDrawLibrary(&g_view, &g_lib, &g_home, &g_resume, true, NULL);
    bool source = false;
    for (int x = 0; x < GFX_WIDTH / 2; x += 4)
        source = source || uiLibraryTap(x, 30).kind == LIBHIT_SOURCE;
    if (!source) {
        printf("TOUCH: home: the library's source doesn't answer a tap\n");
        g_problems++;
    }
    LibraryItem *film = &g_lib.items[g_lib.films[0]];
    film->overview = strdup("When the last road out of the valley closes, a fire lookout and a retired surveyor have "
                            "one week to find the family who stayed behind, before the ash reaches the river.");
    film->runtime_ns = 6720ll * 1000000000;
    film->favorite = true;
    // The longest labels side by side: Resume, From start, Unwatched, In Favorites, Downloading.
    uiDrawDetails(&g_lib, g_lib.films[0], &g_resume,
                  "Still downloading: playing it streams it from the server, and its download and the others pause until you stop.",
                  true, "Downloading 42%");
    uiDrawFooterHelp(T("[A] play  [Y] from the start  [X] watched  [R] favorite  [L] download  [B] back"));
    save("details");
    film->favorite = false;
    bool played = false, marked = false, download = false;
    for (int y = 0; y < GFX_HEIGHT; y += 4)
        for (int x = 0; x < GFX_WIDTH; x += 8) {
            LibHitKind kind = uiLibraryTap(x, y).kind;
            played = played || kind == LIBHIT_DETAILS_PLAY;
            marked = marked || kind == LIBHIT_DETAILS_WATCHED;
            download = download || kind == LIBHIT_DETAILS_DOWNLOAD;
        }
    if (!played || !marked || !download || uiLibraryTap(10, 10).kind != LIBHIT_CLOSE) {
        printf("TOUCH: details: a button or the outside doesn't answer a tap\n");
        g_problems++;
    }
    begin();
    uiDrawLibrary(&g_view, &g_lib, &g_home, &g_resume, true, NULL);
    uiDrawAbout("1.0.0");
    uiDrawFooterHelp(T("[A][B] back"));
    save("about");
    downloadsScene();

    // The + menu, and its question.
    static const char *const MENU[] = {"Downloads", "Update the library", "Settings", "Exit KaiVideo"};
    begin();
    uiDrawLibrary(&g_view, &g_lib, &g_home, &g_resume, true, NULL);
    uiDrawMenu("KaiVideo", "1.0.0", MENU, 4, 0, 2);
    uiDrawFooterHelp(T("[A] choose  [B] close  [+] exit"));
    save("menu");
    checkPlusMenu("menu", 4, 0);
    // Over a video: its own item, the line, then the app's.
    static const char *const VIDEO_MENU[] = {"Back to the library", "Downloads", "Update the library", "Settings",
                                             "Exit KaiVideo"};
    begin();
    uiDrawLibrary(&g_view, &g_lib, &g_home, &g_resume, true, NULL);
    uiDrawMenu("KaiVideo", "1.0.0", VIDEO_MENU, 5, 1, 3);
    uiDrawFooterHelp(T("[A] choose  [B] close  [+] exit"));
    save("menu_video");
    checkPlusMenu("menu_video", 5, 1);
    static const char *const EXIT[] = {"Exit", "Cancel"};
    begin();
    uiDrawLibrary(&g_view, &g_lib, &g_home, &g_resume, true, NULL);
    uiDrawMenu("Exit KaiVideo?", NULL, EXIT, 2, 0, 0);
    uiDrawFooterHelp(T("[A] choose  [B] cancel  [+] exit"));
    save("menu_exit");
    // The first run's welcome: where are the videos? "Later" under a line.
    static const char *const WELCOME[] = {"A folder on the SD card", "A network folder (SMB, WebDAV, FTP...)",
                                          "A media server (Jellyfin, Emby, Plex)", "An IPTV list", "Stremio addons",
                                          "Later"};
    static const char *welcome[6];
    for (int i = 0; i < 6; i++)
        welcome[i] = T(WELCOME[i]);
    begin();
    uiDrawLibrary(&g_view, &g_lib, &g_home, &g_resume, true, NULL);
    uiDrawMenu(T("Welcome! Where are your videos?"), NULL, welcome, 6, 5, 0);
    uiDrawFooterHelp(T("[A] choose  [B] later  [+] exit"));
    save("welcome");
    themeApply(0, 0);

    libraryScene("lib_no_folder", false, NULL, NULL);
    libraryScene("lib_message", true, NULL, "The keyboard couldn't open.");

    // The network folder's page.
    uiSettingsSetNetwork("192.168.0.10:8098/videos", "kai", true, false, false);
    begin();
    uiDrawLibrary(&g_view, &g_lib, &g_home, &g_resume, true, NULL);
    uiDrawSettings(&config, SETTINGS_PAGE_NETWORK, SETTING_NET_ACCOUNT, "sdmc:/Media/Videos", "1.9.0");
    uiDrawFooterHelp(T("[^v] choose  [>][A] open  [B] close"));
    save("settings_network");

    // Files and the folder picker: the folder list.
    static Browser b;
    b.items = calloc(4, sizeof(BrowserEntry));
    snprintf(b.path, sizeof(b.path), "%s", "sdmc:/Media/Movies");
    const char *names[] = {"Paper Lanterns (1998)", "Quiet Harbor (2017)", "Ashfall (2010) 2160p.mkv", "Trailer.mp4"};
    for (int i = 0; i < 4; i++) {
        snprintf(b.items[i].name, sizeof(b.items[i].name), "%s", names[i]);
        b.items[i].is_dir = i < 2;
        b.items[i].size = i < 2 ? -1 : (int64_t)(i * 700) << 20;
    }
    b.count = 4;
    b.cursor = 1;
    begin();
    gfxClear(COLOR_BG);
    g_view.tab = TAB_FILES;
    uiDrawTabBar(&g_view, &g_lib, NULL);
    uiDrawBrowser(&b, &g_resume, BROWSE_FILES);
    uiDrawFooterStrip();
    uiDrawFooterHelp(T("[A] open  [B] folder above  [<>] page  [Y] search  [ZL][ZR] tabs  [+] menu"));
    save("lib_files");
    checkTabTaps("lib_files");

    // Files at the card's top, the network folder first; then inside it.
    static Browser top_b;
    top_b.items = calloc(3, sizeof(BrowserEntry));
    snprintf(top_b.path, sizeof(top_b.path), "%s", "sdmc:/");
    snprintf(top_b.network_name, sizeof(top_b.network_name), T("Network folder · %s"), "192.168.0.10:8098/videos");
    snprintf(top_b.items[0].name, sizeof(top_b.items[0].name), "%s", top_b.network_name);
    top_b.items[0].is_dir = top_b.items[0].is_network = true;
    snprintf(top_b.items[1].name, sizeof(top_b.items[1].name), "%s", "Media");
    snprintf(top_b.items[2].name, sizeof(top_b.items[2].name), "%s", "switch");
    top_b.items[1].is_dir = top_b.items[2].is_dir = true;
    top_b.items[0].size = top_b.items[1].size = top_b.items[2].size = -1;
    top_b.count = 3;
    begin();
    gfxClear(COLOR_BG);
    uiDrawTabBar(&g_view, &g_lib, NULL);
    uiDrawBrowser(&top_b, &g_resume, BROWSE_FILES);
    uiDrawFooterStrip();
    uiDrawFooterHelp(T("[A] open  [B] folder above  [<>] page  [Y] search  [ZL][ZR] tabs  [+] menu"));
    save("lib_files_network");
    snprintf(b.path, sizeof(b.path), "%s", "dav:/Movies");
    snprintf(b.network_name, sizeof(b.network_name), "%s", top_b.network_name);
    begin();
    gfxClear(COLOR_BG);
    uiDrawTabBar(&g_view, &g_lib, NULL);
    uiDrawBrowser(&b, &g_resume, BROWSE_FILES);
    uiDrawFooterStrip();
    uiDrawFooterHelp(T("[A] open  [B] folder above  [<>] page  [Y] search  [ZL][ZR] tabs  [+] menu"));
    save("lib_files_in_network");
    snprintf(b.path, sizeof(b.path), "%s", "sdmc:/Media/Movies");
    begin();
    gfxClear(COLOR_BG);
    uiDrawBrowser(&b, &g_resume, BROWSE_PICK);
    uiDrawFooterStrip();
    uiDrawFooterHelp(T("[A] open  [B] folder above  [X] use this folder  [-] cancel"));
    save("lib_picker");
    size_t row;
    if (uiTapBrowser(&b, GFX_WIDTH - 100, 34, BROWSE_PICK, &row) != TAP_USE_FOLDER ||
        uiTapBrowser(&b, GFX_WIDTH - 300, 34, BROWSE_PICK, &row) != TAP_CANCEL ||
        uiTapBrowser(&b, GFX_WIDTH - 300, 34, BROWSE_PICK_FIRST, &row) == TAP_CANCEL) {
        printf("TOUCH: lib_picker: Use or Cancel doesn't answer a tap as drawn\n");
        g_problems++;
    }
    free(b.items);
    libraryFree(&g_lib);
    watchedFree(&g_watched);
}

#define MARGIN_PREVIEW_SOURCE_X 250  // Inside the source pill after the title.

// Two servers' libraries as tabs: they fit between the source and the clock,
// or slide so the one chosen shows, without covering either.
static void serversScene(void)
{
    static const char *const NAMES[] = {"Movies (Emby)", "TV Shows (Emby)", "Movies (Plex)", "TV Shows (Plex)",
                                        "Home Videos (Plex)"};
    static LibView view;
    static Library lib;
    static Home home;
    static ResumeList resume;
    libraryInit(&lib, "jellyfin:");
    for (int i = 0; i < 5; i++)
        libraryAddCollection(&lib, NAMES[i], i % 2 ? COLLECTION_SERIES : COLLECTION_FILMS);
    libraryBuild(&lib);
    libviewInit(&view);
    libviewSetTabs(&view, &lib, true, true, false);
    SysInfo sys = {.has_time = true, .hour = 21, .minute = 47, .has_battery = true, .battery_percent = 82};
    uiLibrarySetStatus(&sys, false);
    uiLibrarySetSource("Plex", uiServerColor(SERVER_PLEX));
    for (int pass = 0; pass < 2; pass++) {
        view.tab = pass ? TAB_COLLECTION : TAB_HOME;
        view.collection = pass ? 4 : -1;
        uiLibraryResetHits();
        begin();
        uiDrawLibrary(&view, &lib, &home, &resume, true, NULL);
        uiDrawFooterHelp(T("[A] open  [Y] search  [ZL][ZR] tabs  [-] settings  [+] menu"));
        save(pass ? "tabs_servers_last" : "tabs_servers");
        if (uiLibraryTap(MARGIN_PREVIEW_SOURCE_X, 30).kind != LIBHIT_SOURCE) {
            printf("TOUCH: tabs_servers: the source doesn't answer a tap\n");
            g_problems++;
        }
    }
}

// With a language file as the second argument, the screens are drawn in it
// (to see what no longer fits): preview build/es ../../data/lang_es.txt
static void useLanguageFile(const char *path)
{
    FILE *f = fopen(path, "rb");
    static char text[1 << 20];
    size_t size = f ? fread(text, 1, sizeof(text), f) : 0;
    if (f)
        fclose(f);
    if (!size || !langLoadText(text, size))
        fprintf(stderr, "cannot read the language in %s\n", path);
}

int main(int argc, char **argv)
{
    g_dir = argc > 1 ? argv[1] : ".";
    if (argc > 2)
        useLanguageFile(argv[2]);
    if (!fontInit()) {
        fprintf(stderr, "fonts failed\n");
        return 1;
    }
    Menu closed = {0};
    Player p = {.paused = false, .audio = 0, .subtitle = 2, .audio_count = 3, .subtitle_count = 10};
    playerScene("player_bar", &p, UI_MENU_AUDIO, &closed, "No, I'm not going back there.\nNot ever.",
                "Resumed at 22:05  \xC2\xB7  ZL from the start");

    p.paused = true;
    playerScene("player_paused", &p, UI_MENU_AUDIO, &closed, NULL, NULL);

    statsScene();
    fontsScene();
    skipScene(&p);
    liveScene(&p);
    serversScene();

    Menu audio;
    menuOpen(&audio, p.audio_count, 0, p.audio);
    menuMove(&audio, 1);
    playerScene("menu_audio", &p, UI_MENU_AUDIO, &audio, NULL, NULL);
    checkMenuTaps("menu_audio", UI_MENU_AUDIO, &audio);

    Menu subs;
    configDefaults(&g_config);
    menuOpen(&subs, p.subtitle_count + 1 + SUBOPT_COUNT, SUBOPT_COUNT, p.subtitle + 1);
    p.paused = false;
    playerScene("menu_subtitles", &p, UI_MENU_SUBTITLES, &subs, "No, I'm not going back there.", NULL);
    checkMenuTaps("menu_subtitles", UI_MENU_SUBTITLES, &subs);
    // With a track that brings its own look, only the size is offered.
    Menu styled;
    int styled_options = subtitleOptionCount(false);
    menuOpen(&styled, p.subtitle_count + 1 + styled_options, styled_options, p.subtitle + 1);
    playerScene("menu_subtitles_styled", &p, UI_MENU_SUBTITLES, &styled, NULL, NULL);
    checkMenuTaps("menu_subtitles_styled", UI_MENU_SUBTITLES, &styled);
    // The options under the list: each answers a tap with its own row.
    for (int i = 0; i < SUBOPT_COUNT; i++) {
        int row = -1, want = subs.count - SUBOPT_COUNT + i, hit = -1;
        for (int y = 0; y < GFX_HEIGHT && hit < 0; y += 4)
            if (uiTapTrackMenu(UI_MENU_SUBTITLES, &subs, 1000, y, &row) == TAP_MENU_ROW && row == want)
                hit = y;
        if (hit < 0) {
            printf("TOUCH: menu_subtitles: option %d doesn't answer a tap\n", i);
            g_problems++;
        }
    }
    // Chapters above the speed and the delays, and the same without chapters.
    p.chapter_count = 9;
    p.speed = 1250;
    p.audio_delay = 300;
    p.subtitle_delay = -1200;
    Menu playback;
    menuOpen(&playback, p.chapter_count + PLAYBACK_OPT_COUNT, PLAYBACK_OPT_COUNT, 4);
    playerScene("menu_playback", &p, UI_MENU_PLAYBACK, &playback, NULL, NULL);
    checkMenuTaps("menu_playback", UI_MENU_PLAYBACK, &playback);
    for (int i = 0; i < PLAYBACK_OPT_COUNT; i++) {  // Each option answers on both of its halves.
        int row = -1, want = playback.count - PLAYBACK_OPT_COUNT + i, hit = -1;
        for (int y = 0; y < GFX_HEIGHT && hit < 0; y += 4)
            if (uiTapTrackMenu(UI_MENU_PLAYBACK, &playback, 700, y, &row) == TAP_MENU_ROW && row == want)
                hit = y;
        if (hit < 0 || uiTapOptionDelta(UI_MENU_PLAYBACK, &playback, 500) != -1 ||
            uiTapOptionDelta(UI_MENU_PLAYBACK, &playback, 800) != 1) {
            printf("TOUCH: menu_playback: option %d doesn't answer a tap on both sides\n", i);
            g_problems++;
        }
    }
    double unused;
    if (uiTapPlayer(830, GFX_HEIGHT - UI_PLAYER_BAR_H + 28, true, true, &unused) != TAP_PLAYBACK) {
        printf("TOUCH: player_bar: the Playback button doesn't answer a tap\n");
        g_problems++;
    }
    playerScene("player_chapters", &p, UI_MENU_AUDIO, &closed, NULL, NULL);
    p.chapter_count = 0;
    p.speed = 0;
    p.subtitle_count = 0;
    menuOpen(&playback, PLAYBACK_OPT_COUNT, PLAYBACK_OPT_COUNT, 0);
    playerScene("menu_playback_plain", &p, UI_MENU_PLAYBACK, &playback, NULL, NULL);
    p.subtitle_count = 10;
    p.audio_delay = p.subtitle_delay = 0;

    // Large yellow subtitles on a dark box, with the cursor on Style.
    g_config.subtitles = SUBTITLES_LARGE;
    g_config.subtitle_color = 1;
    g_config.subtitle_style = SUBTITLE_BOX;
    uiSetSubtitleStyle(settingSubtitlePx(&g_config), settingSubtitleColor(&g_config), true);
    menuMove(&subs, 99);
    playerScene("menu_subtitle_style", &p, UI_MENU_SUBTITLES, &subs, "No, I'm not going back there.\nNot ever.",
                NULL);
    uiSetSubtitleStyle(34, 0xF2F2F5, false);

    libraryScenes();
    fontExit();
    printf("%d touch problems\n", g_problems);
    return g_problems ? 1 : 0;
}
