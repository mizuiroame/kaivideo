// KaiVideo's state, shared by main.c (the loop and the player) and
// app_library.c (the library's screens, the Files tab and the choice of
// the library folder).
#pragma once

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#include "browser.h"
#include "home.h"
#include "jfsync.h"
#include "library.h"
#include "libview.h"
#include "menu.h"
#include "player.h"
#include "resume.h"
#include "settings.h"
#include "stats.h"
#include "touch.h"
#include "trackprefs.h"
#include "ui.h"
#include "watched.h"

#define APP_NAME_MAX 300
#define APP_FAVORITE_CHANGES 8

typedef enum { APP_MENU_NONE, APP_MENU_MAIN, APP_MENU_EXIT } AppMenu;

typedef struct App App;

struct App {
    Browser browser;            // The Files tab.
    Player *player;
    // A video being opened by a thread, while the loading screen shows.
    bool opening, open_cancelled;
    bool open_from_start;       // Not from where it was left.
    int64_t open_at_ns;         // From here (opened again, for another quality or sound); 0: as usual.
    int64_t open_start_ns;      // Where playback will begin: a conversion starts there.
    uint64_t open_asked;        // When the opening began, for the log.
    int next_audio_plus1;       // The server's number of the sound to open with, plus 1; 0: its choice.
    char open_sound[JF_TRACK_NAME_MAX];  // The sound track chosen before for this video or series.
    JfPlan open_plan;           // How the video being opened or played comes from the server.
    bool playing_converted;
    int quality_applied;        // The quality the video playing was opened with.
    pthread_t open_thread;
    volatile bool open_done;
    Player *open_result;
    char open_error[160];
    uint64_t waiting_since;     // The picture has been awaited since then (0: it's there).
    char playing_name[APP_NAME_MAX];
    char playing_path[BROWSER_PATH_MAX];
    int playing_item;           // Its item in the library, or -1 (from Files).
    ResumeList resume;
    Watched watched;            // Watched to the end, or marked by hand.
    TrackPrefs tracks;          // The sound and subtitles chosen, by video and series.
    bool hud_hidden;            // - hides the player's bar until pressed again.
    int next_item;              // The episode after the one playing, or -1.
    bool next_dismissed;        // Its card was closed: it won't play on its own.
    bool play_next;             // ZR or a tap on the card: play it now.
    char bar_note[256];         // Said in the player's bar for a while ("Resumed at 12:34").
    uint64_t bar_note_until;
    uint64_t overlay_until;
    Menu menu;                  // The open drop-down list, if any, over the player.
    UiMenuKind menu_kind;
    int speed_index;            // The playback speed (playback.h), kept from video to video.
    unsigned seeks;
    uint64_t started;
    char message[200];
    uint64_t message_until;
    int drag_rows, drag_cols;
    bool quit;
    AppMenu app_menu;           // The + menu, or its exit question (app_menu.c).
    int app_menu_cursor;
    bool stop_requested;        // "Back to the library" from the + menu over a video.
    bool scrubbing;             // A finger drags the progress bar.
    double scrub_fraction;
    // Favorites changed here since the last catalog came: one read before
    // them may come later, and they're put back on it (app_library.c).
    char favorite_changes[APP_FAVORITE_CHANGES][LIBRARY_PATH_MAX];
    bool favorite_values[APP_FAVORITE_CHANGES];
    int favorite_change_count;
    // Live TV (app_live.c): the guide shown, and one being read by a thread.
    LiveGuide live;
    LiveGuide live_incoming;
    pthread_t live_thread;
    bool live_reading;
    int live_read_count;        // What the thread found: channels, -1 if the server didn't answer.
    uint64_t live_read_at;
    bool live_read_soon;        // Read the guide again at once (appLiveReadSoon).
    int live_pending;           // L and R while a channel opens: channels to move on once it's let go.
    char live_pending_name[APP_NAME_MAX];  // The channel they lead to, for the loading screen.
    int live_switch;          // L or R while a channel plays: -1 or 1, taken by the main loop.
    JfSegment segments[JF_SEGMENTS_MAX];  // The playing video's parts to skip (app_skip.c).
    int segment_count;
    bool stats_shown;           // ↓ shows the statistics panel (app_stats.c).
    StatsWindow stats;
    uint64_t stats_sampled;

    // The library (app_library.c).
    char library_root[LIBRARY_PATH_MAX];  // "" until one is chosen.
    Library library;
    Home home;
    LibView view;
    bool picking;               // Choosing the library folder.
    Browser picker;

    // Settings (app_settings.c).
    Config config;
    bool settings_open, about_open;
    int settings_page;          // SettingsPage.
    int settings_cursor;        // The SettingRow under the cursor.

    // Jellyfin (app_jellyfin.c).
    JfStatus jf;
    bool jf_signing;            // The sign-in panel is up.
    char jf_last_error[128];
    uint64_t jf_last_refresh, jf_last_report;
};

// main.c
void appShowMessage(App *app, const char *message, uint64_t now);
// Starts opening path, named name in the player's bar; the loading screen
// shows until it plays. False (with a message) if it can't even start.
bool appStartVideo(App *app, const char *path, const char *name, uint64_t now);
// Builds a file's path in KaiVideo's own folder.
void appConfigPath(char *out, size_t size, const char *name);
void appSaveSettings(const App *app);
void appLog(const char *fmt, ...);
// Keeps watched.txt after app->watched changed.
void appSaveWatched(const App *app);

// app_library.c
void appLibraryStart(App *app);  // After the settings are read.
void appLibraryExit(App *app);
// Takes a finished scan, and works out Home again after a video.
void appLibraryUpdate(App *app);
void appLibraryRefreshHome(App *app);
// Plays a film or episode of the library.
void appLibraryPlay(App *app, int item, uint64_t now);
void appLibraryInput(App *app, uint64_t pressed, TouchEvent touch, uint64_t now);
void appLibraryDraw(App *app, uint64_t now);
// Looks for videos again (Settings).
void appLibraryRescan(App *app, uint64_t now);
void appOpenPicker(App *app);
// Between the card and Jellyfin; to Jellyfin's settings if it isn't set up.
void appLibraryToggleSource(App *app, uint64_t now);
// Switches the library between the card and the Jellyfin server.
void appLibrarySetSource(App *app, bool jellyfin);
// Puts a library read from the server in place (taking it from c) with what it says was watched.
void appLibraryInstallCatalog(App *app, JfCatalog *c);

// app_jellyfin.c
// Starts the connection if it isn't running. False if the network can't be used.
bool appJellyfinEnsure(App *app);
// Once a frame: sign-in results, a new library, a refresh now and then.
void appJellyfinUpdate(App *app, uint64_t now);
void appJellyfinPlaybackStarted(App *app, uint64_t now);
void appJellyfinPlaybackTick(App *app, uint64_t now);
void appJellyfinPlaybackStopped(App *app, int64_t position_ns, bool watched);

// app_live.c
// Once a frame: reads the guide now and then (often while its tab shows), and takes it when read.
void appLiveUpdate(App *app, uint64_t now);
void appLiveExit(App *app);
// Reads the server's channels and guide again as soon as it can, not at the
// next refresh: after changing to Jellyfin, or a connection test that worked.
void appLiveReadSoon(App *app);
// The Live TV guide: up and down change channel, left and right move through
// its programs, A plays the channel.
void appLiveInput(App *app, uint64_t pressed, TouchEvent touch, uint64_t now);
void appLiveDraw(const App *app);
// Live TV is playing: no jumps, no resume point.
bool appPlayingLive(const App *app);
// The channel delta places from the one playing, wrapping (L/R in the player).
// False if it isn't in the guide, or it's the only one.
bool appLiveNeighbor(App *app, int delta, char *location, size_t location_size, char *name, size_t name_size);

// app_skip.c
// ZR, or a tap on its button, skips the part playing (intro, credits...).
// True if it took the press. up_next: the next episode's card shows, and ZR is its.
bool appSkipInput(App *app, uint64_t pressed, TouchEvent touch, bool up_next, uint64_t now);
void appSkipDraw(const App *app, bool bar_shown, bool up_next);

// app_trickplay.c
// The jump's picture over the screen while it's awaited (waiting), from the server's trickplay.
void appTrickplayBehind(const App *app, bool waiting);
// The picture where a finger drags the bar, above it (bar: the bar shows).
void appTrickplayAboveBar(const App *app, bool bar);

// app_stats.c
// Measures the video playing, and draws the panel if ↓ turned it on.
void appStatsUpdate(App *app, uint64_t now);
void appStatsDraw(const App *app);

// app_menu.c
void appOpenMenu(App *app);
void appMenuInput(App *app, uint64_t pressed, TouchEvent touch, uint64_t now);
void appMenuDraw(const App *app);
// The help line for the library's screens and overlays: "[A] open  [+] menu".
const char *appFooterHelp(const App *app);

// app_settings.c
// Applies the settings read at start: theme, background, thumbnails, subtitles.
void appSettingsStart(App *app);
// Applies the subtitles' size, color and style (changed in the player).
void appApplySubtitleStyle(const App *app);
void appOpenSettings(App *app);
void appOpenJellyfinSettings(App *app, uint64_t now);
void appSettingsInput(App *app, uint64_t pressed, TouchEvent touch, uint64_t now);
void appSettingsDraw(const App *app);
