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

static void playerScene(const char *name, Player *p, UiMenuKind kind, const Menu *m, const char *subtitle,
                        const char *note)
{
    begin();
    drawPicture();
    if (subtitle)
        uiDrawSubtitles(subtitle, true);
    if (p->paused && !m->open)
        uiDrawPausedSymbol();
    uiDrawPlayerBar(p, "Rook - S01E03 - Reckoning.mkv", m->open ? NULL : note);
    uiDrawTrackMenu(p, kind, m);
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

// --- The library's screens ---

static Library g_lib;
static ResumeList g_resume;
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
    homeCompute(&g_home, &g_lib, &g_resume);
    libviewInit(&g_view);
}

static void libraryScene(const char *name, bool folder_set, const char *status, const char *message)
{
    begin();
    uiDrawLibrary(&g_view, &g_lib, &g_home, &g_resume, folder_set, status, message);
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
        else if (hit.kind == LIBHIT_FOLDER)
            found |= 1 << TAB_COUNT;
    }
    if (found != (1 << (TAB_COUNT + 1)) - 1) {
        printf("TOUCH: %s: a tab or the Folder button doesn't answer a tap\n", name);
        g_problems++;
    }
}

static void libraryScenes(void)
{
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
    uiDrawBrowser(&b, &g_resume, NULL, BROWSE_FILES);
    save("lib_files");
    checkTabTaps("lib_files");
    begin();
    gfxClear(COLOR_BG);
    uiDrawBrowser(&b, &g_resume, NULL, BROWSE_PICK);
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
    menuOpen(&audio, p.audio_count, p.audio);
    menuMove(&audio, 1);
    playerScene("menu_audio", &p, UI_MENU_AUDIO, &audio, NULL, NULL);
    checkMenuTaps("menu_audio", UI_MENU_AUDIO, &audio);

    Menu subs;
    menuOpen(&subs, p.subtitle_count + 1, p.subtitle + 1);
    p.paused = false;
    playerScene("menu_subtitles", &p, UI_MENU_SUBTITLES, &subs, "No, I'm not going back there.", NULL);
    checkMenuTaps("menu_subtitles", UI_MENU_SUBTITLES, &subs);

    libraryScenes();
    fontExit();
    printf("%d touch problems\n", g_problems);
    return g_problems ? 1 : 0;
}
