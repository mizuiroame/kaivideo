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
#include "library.h"
#include "libview.h"
#include "menu.h"
#include "player.h"
#include "resume.h"
#include "ui.h"
#include "ui_library.h"
#include "settings.h"
#include "theme.h"
#include "thumbs.h"
#include "ui_settings.h"
#include "ui_style.h"
#include "sample_paths.h"

// --- A fake player: what the bar and the lists ask of it ---

struct Player {
    bool paused;
    int audio, subtitle;
    int audio_count, subtitle_count;
};

static const char *const AUDIO[] = {"English - DD5.1", "Español (Latinoamérica) - DD5.1", "Commentary - AAC2.0"};
static const char *const SUBS[] = {"English", "English (SDH)", "Español", "Français", "Deutsch", "Italiano",
                                   "Português (Brasil)", "日本語", "Polski", "Svenska"};

bool playerPaused(const Player *p) { return p->paused; }
int64_t playerPosition(const Player *p) { (void)p; return 1325ll * 1000000000; }
int64_t playerDuration(const Player *p) { (void)p; return 2710ll * 1000000000; }
const char *playerVideoInfo(const Player *p) { (void)p; return "H.264 1920x1080, 24 fps"; }
const char *playerAudioInfo(const Player *p) { (void)p; return "ac3, 48 kHz, 6 channels"; }
int playerAudioTrackCount(const Player *p) { return p->audio_count; }
const char *playerAudioTrackName(const Player *p, int track) { (void)p; return AUDIO[track]; }
int playerAudioTrack(const Player *p) { return p->audio; }
int playerSubtitleCount(const Player *p) { return p->subtitle_count; }
const char *playerSubtitleLabel(const Player *p, int track) { (void)p; return SUBS[track]; }
int playerSubtitle(const Player *p) { return p->subtitle; }

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
    uiDrawPlayerBar(p, "Rook - S01E03 - Reckoning.mkv", m->open ? NULL : note, -1);
    uiDrawTrackMenu(p, kind, m, &g_config);
    save(name);
}

// Every row drawn in an open list must answer a tap with its own index.
static void checkMenuTaps(const char *name, UiMenuKind kind, const Menu *m)
{
    for (int i = 0; i < menuVisible(m); i++) {
        int found = -1, row = -1;
        // The middle of row i: scan down the list's right half for it.
        for (int y = 0; y < GFX_HEIGHT && found < 0; y += 4) {
            if (uiTapTrackMenu(kind, m, kind == UI_MENU_AUDIO ? 900 : 1000, y, &row) == TAP_MENU_ROW &&
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
    for (size_t i = 0; i < g_lib.count; i += 9)
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

static void libraryScene(const char *name, bool folder_set, const char *status, const char *message)
{
    begin();
    uiDrawLibrary(&g_view, &g_lib, &g_home, &g_resume, folder_set, status, message);
    uiDrawFooterHelp(HOME_HELP);
    save(name);
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
            found |= 1 << TAB_COUNT;
    }
    if (found != (1 << (TAB_COUNT + 1)) - 1) {
        printf("TOUCH: %s: a tab or the Folder button doesn't answer a tap\n", name);
        g_problems++;
    }
}

static void libraryScenes(void)
{
    SysInfo sys = {.has_time = true, .hour = 21, .minute = 47, .has_battery = true, .battery_percent = 82};
    uiLibrarySetStatus(&sys, false);
    makeFakeThumbs();
    uiLibrarySetThumbnails(false);  // The color cards first.
    buildSampleLibrary();
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
    g_view.tab = TAB_SERIES;
    libraryScene("lib_series", true, NULL, NULL);

    for (size_t s = 0; s < g_lib.show_count; s++)
        if (strstr(g_lib.shows[s].name, "Prairie"))
            libviewOpenShow(&g_view, &g_lib, (int)s, -1);
    libviewShowMove(&g_view, &g_lib, 1, 3);
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
    for (size_t s = 0; s < g_lib.show_count; s++)
        if (strstr(g_lib.shows[s].name, "Prairie"))
            libviewOpenShow(&g_view, &g_lib, (int)s, -1);
    libraryScene("thumbs_show", true, NULL, NULL);
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
            uiDrawPlayerBar(&playing, "Prairie Road \xC2\xB7 S01E03 \xC2\xB7 Texas Twister", NULL, -1);
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

    // Settings and About over Home, in another theme.
    Config config;
    configDefaults(&config);
    config.theme = 2;
    config.accent = 1;
    themeApply(config.theme, config.accent);
    begin();
    uiDrawLibrary(&g_view, &g_lib, &g_home, &g_resume, true, NULL, NULL);
    uiDrawSettings(&config, SETTINGS_PAGE_MAIN, SETTING_OPEN_DISPLAY, "sdmc:/Media/Videos", "1.0.0");
    uiDrawFooterHelp("[^v] choose  [>][A] open  [B] close");
    save("settings");
    // The main page's rows: 48 high from 76 below the panel's top (6 rows, a 60 footer).
    int top = (GFX_HEIGHT - (76 + 6 * 48 + 60)) / 2 + 76;
    LibHit about = uiLibraryTap(GFX_WIDTH / 2 - 200, top + 5 * 48 + 20), outside = uiLibraryTap(20, 400);
    LibHit thumbs = uiLibraryTap((GFX_WIDTH + 760) / 2 - 40, top + 2 * 48 + 20);
    if (about.kind != LIBHIT_SETTING || about.a != SETTING_ABOUT || outside.kind != LIBHIT_CLOSE ||
        thumbs.kind != LIBHIT_SETTING_NEXT || thumbs.a != SETTING_THUMBNAILS) {
        printf("TOUCH: settings: a row, its arrow or the outside doesn't answer as drawn (%d %d %d)\n", about.kind,
               thumbs.kind, outside.kind);
        g_problems++;
    }
    begin();
    uiDrawLibrary(&g_view, &g_lib, &g_home, &g_resume, true, NULL, NULL);
    uiDrawSettings(&config, SETTINGS_PAGE_DISPLAY, SETTING_THEME, "sdmc:/Media/Videos", "1.0.0");
    uiDrawFooterHelp("[^v] choose  [<>][A] change  [B] back");
    save("settings_display");
    top = (GFX_HEIGHT - (76 + 5 * 48 + 60)) / 2;
    if (uiLibraryTap((GFX_WIDTH - 760) / 2 + 60, top + 33).kind != LIBHIT_PAGE_BACK) {
        printf("TOUCH: settings_display: Back doesn't answer a tap\n");
        g_problems++;
    }
    begin();
    uiDrawLibrary(&g_view, &g_lib, &g_home, &g_resume, true, NULL, NULL);
    uiDrawAbout("1.0.0");
    uiDrawFooterHelp("[A][B] back");
    save("about");

    // The + menu, and its question.
    static const char *const MENU[] = {"Look for new videos", "Settings", "Exit KaiVideo"};
    begin();
    uiDrawLibrary(&g_view, &g_lib, &g_home, &g_resume, true, NULL, NULL);
    uiDrawMenu("KaiVideo", "1.0.0", MENU, 3, 1);
    uiDrawFooterHelp("[A] choose  [B] close  [+] exit");
    save("menu");
    LibHit item = uiLibraryTap(GFX_WIDTH / 2, (GFX_HEIGHT - (76 + 3 * 48 + 20)) / 2 + 76 + 2 * 48 + 20);
    if (item.kind != LIBHIT_MENU_ITEM || item.a != 2 || uiLibraryTap(20, 400).kind != LIBHIT_CLOSE) {
        printf("TOUCH: menu: an item or the outside doesn't answer as drawn\n");
        g_problems++;
    }
    static const char *const EXIT[] = {"Exit", "Cancel"};
    begin();
    uiDrawLibrary(&g_view, &g_lib, &g_home, &g_resume, true, NULL, NULL);
    uiDrawMenu("Exit KaiVideo?", NULL, EXIT, 2, 0);
    uiDrawFooterHelp("[A] choose  [B] cancel  [+] exit");
    save("menu_exit");
    themeApply(0, 0);

    libraryScene("lib_no_folder", false, NULL, NULL);
    libraryScene("lib_message", true, NULL, "The keyboard couldn't open.");

    // Files and the folder picker: the folder list.
    static Browser b;
    b.items = calloc(4, sizeof(BrowserEntry));
    snprintf(b.path, sizeof(b.path), "%s", "sdmc:/Media/Movies");
    const char *names[] = {"Paper Moon (1998)", "Quiet Harbor (2017)", "Ashfall (2010) 2160p.mkv", "Trailer.mp4"};
    for (int i = 0; i < 4; i++) {
        snprintf(b.items[i].name, sizeof(b.items[i].name), "%s", names[i]);
        b.items[i].is_dir = i < 2;
        b.items[i].size = i < 2 ? -1 : (int64_t)(i * 700) << 20;
    }
    b.count = 4;
    b.cursor = 1;
    begin();
    gfxClear(COLOR_BG);
    uiDrawTabBar(TAB_FILES, NULL);
    uiDrawBrowser(&b, &g_resume, BROWSE_FILES);
    uiDrawFooterStrip();
    uiDrawFooterHelp("[A] open  [B] folder above  [<>] page  [Y] search  [ZL][ZR] tabs  [+] menu");
    save("lib_files");
    checkTabTaps("lib_files");
    begin();
    gfxClear(COLOR_BG);
    uiDrawBrowser(&b, &g_resume, BROWSE_PICK);
    uiDrawFooterStrip();
    uiDrawFooterHelp("[A] open  [B] folder above  [X] use this folder  [-] cancel");
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

int main(int argc, char **argv)
{
    g_dir = argc > 1 ? argv[1] : ".";
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
