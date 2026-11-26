// KaiVideo's state, shared by main.c (the loop and the player) and
// app_library.c (the library's screens, the Files tab and the choice of
// the library folder).
#pragma once

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#include "browser.h"
#include "downloads.h"
#include "dvr.h"
#include "home.h"
#include "iptv.h"
#include "iptvhome.h"
#include "iptvload.h"
#include "stremio.h"
#include "jfsync.h"
#include "kinetic.h"
#include "library.h"
#include "libview.h"
#include "menu.h"
#include "notice.h"
#include "player.h"
#include "resume.h"
#include "scroll.h"
#include "settings.h"
#include "stats.h"
#include "touch.h"
#include "trackprefs.h"
#include "ui.h"
#include "watched.h"

#define APP_NAME_MAX 300
#define APP_FAVORITE_CHANGES 8

// The + menu, its exit question, or the first run's "Where are your videos?".
typedef enum { APP_MENU_NONE, APP_MENU_MAIN, APP_MENU_EXIT, APP_MENU_WELCOME } AppMenu;

typedef struct App App;

struct App {
    Browser browser;            // The Files tab.
    Player *player;
    // A video being opened by a thread, while the loading screen shows.
    bool opening;
    volatile bool open_cancelled;  // Set by the main loop: the opening's requests stop at once.
    bool open_dismissed;        // B: the loading screen is gone; the opening ends out of sight.
    // A video asked for while a dismissed opening still ends: it opens after.
    bool queued;
    bool queued_from_start;
    int64_t queued_at_ns;
    char queued_name[APP_NAME_MAX];
    char queued_path[BROWSER_PATH_MAX];
    bool open_from_start;       // Not from where it was left.
    int64_t open_at_ns;         // From here (opened again, for another quality or sound); 0: as usual.
    int64_t open_start_ns;      // Where playback will begin: a conversion starts there.
    uint64_t open_asked;        // When the opening began, for the log.
    int next_audio_plus1;       // The server's number of the sound to open with, plus 1; 0: its choice.
    bool next_subtitle_set;     // Opened again: the picture subtitle for the server to draw is said,
    int next_subtitle;          // its number (-1 for none); else the one chosen before, if any.
    char open_sound[JF_TRACK_NAME_MAX];  // The sound track chosen before for this video or series.
    char open_subs[JF_TRACK_NAME_MAX + 16];  // And the subtitles.
    JfPlan open_plan;           // How the video being opened or played comes from the server.
    bool playing_converted;
    bool playing_live_stream;   // An addon's stream that is live (HLS without an end, or MPEG-TS sent as it is):
                                // played as a channel is, without seeking (appPlayingLive).
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
    int volume;                 // The sound's volume in percent (playback.h), kept likewise.
    PlaybackAspect aspect;      // How the picture fills the screen, kept likewise.
    uint64_t loop_jumped_at;    // When the A-B loop last went back to A.
    unsigned seeks;
    uint64_t started;
    Notices notices;            // Messages, in bubbles at the top (newest on top).
    NoticeView notice_views[NOTICE_STACK + 1];  // The ones to draw this frame,
    int notice_count;           // and how many.
    unsigned update_seen;       // The update status's serial already said on screen.
    bool update_manual;         // Settings asked: say "up to date" and failures too.
    Kinetic kinetic;            // A list following a finger, or gliding on after it.
    int scroll_target, scroll_row;  // Which list (app_library.c's ScrollTarget), and Home's row across.
    int scroll_view;            // The view it was in: another one stops it.
    Browser *scroll_browser;    // The file list being scrolled (Files, or choosing a folder).
    bool swallow_tap;           // A press stopped a glide: its tap doesn't open anything.
    bool quit;
    AppMenu app_menu;           // The + menu, or its exit question (app_menu.c).
    int app_menu_cursor;
    bool stop_requested;        // "Back to the library" from the + menu over a video.
    int menu_after_video;       // An item of that menu to run once the video has stopped, or 0 (app_menu.c).
    bool scrubbing;             // A finger drags the progress bar.
    double scrub_fraction;
    // Favorites changed here since the last catalog came: one read before
    // them may come later, and they're put back on it (app_library.c).
    char favorite_changes[APP_FAVORITE_CHANGES][LIBRARY_PATH_MAX];
    bool favorite_values[APP_FAVORITE_CHANGES];
    int favorite_change_count;
    // Live TV (app_live.c): the guide shown (the server's channels, then the
    // IPTV list's of the group chosen), the server's, and one being read by a thread.
    LiveGuide live;
    LiveGuide live_server;
    LiveGuide live_incoming;
    // The IPTV list (app_iptv.c): its channels and guide, and the group the Live tab shows.
    IptvList iptv;
    IptvGuide iptv_guide;
    unsigned iptv_seen;         // The load whose result was taken.
    int iptv_group;             // An index of its groups, -1 for all its channels.
    XtreamCatalog xtream;       // An Xtream account's films and series (app_xtream.c).
    int xtream_group[GRID_COLLECTIONS];   // The category Films and Series show (by grid), plus 1; 0 for all,
    int *xtream_only[GRID_COLLECTIONS];   // and what's in it in the library (LibView.only), or NULL.
    bool xtream_groups_open;    // The categories show over Films or Series (Y),
    int xtream_group_cursor;    // and the row under the cursor: 0 is all of them, then each category.
    // Stremio addons (app_stremio.c): what they gave, and the streams to choose from.
    StremioStore stremio;
    bool streams_open;          // The list of streams shows over the library.
    bool streams_loading;       // The addons are being asked.
    bool streams_from_start;    // The video was asked from its start.
    StremioStream *streams;
    int stream_count, stream_cursor, stream_first;
    char streams_note[512];     // Addons that failed.
    char streams_path[LIBRARY_PATH_MAX];  // The video they're for.
    char streams_name[APP_NAME_MAX];
    char stremio_go[LIBRARY_PATH_MAX];    // Its stream chosen: appStartVideo opens it this once.
    IptvMarks iptv_marks;       // Its channels watched last and favorites (app_iptvhome.c),
    IptvHome iptv_home;         // the rows of its view's Home,
    IptvHomeView iptv_home_view;  // and where the cursor is on them.
    bool live_groups_open;      // The list of groups shows over the guide (X).
    int live_group_cursor;      // Its row: 0 is "All channels", then each group.
    char open_url[STREMIO_URL_MAX * 2];  // The IPTV channel (or addon's HLS stream) opening: copied for the thread,
    bool open_any_certificate;    // and whether its certificate is checked.
    StreamHeaders open_headers;   // and the headers its list or addon asks for.
    pthread_t live_thread;
    bool live_reading;
    int live_read_count;        // What the thread found: channels, -1 if the server didn't answer.
    uint64_t live_read_at;
    bool live_read_soon;        // Read the guide again at once (appLiveReadSoon).
    bool live_pending_set;      // L or R was pressed while a channel opened (live_pending may add up to 0).
    int live_pending;           // L and R while a channel opens: channels to move on once it's let go.
    char live_pending_name[APP_NAME_MAX];  // The channel they lead to, for the loading screen.
    int live_switch;          // L or R while a channel plays: -1 or 1, taken by the main loop.
    int64_t live_window_from, live_window_to;  // The guide's hours read last (or being read).
    int live_drag_axis;       // A finger on the guide: 0 not known yet, 1 along the hours, 2 down the channels.
    int live_drag_rest;       // Pixels along the hours not turned into time yet.
    // The server's DVR (app_dvr.c): what it records and recorded, as last read (NULL until then),
    DvrLists *dvr;
    bool dvr_open;            // the Recordings list over the guide (L),
    Scroll dvr_scroll;
    int dvr_confirm;          // its row whose X was pressed once, -1 for none, and what's on it,
    char dvr_confirm_id[DVR_ID_MAX];
    bool dvr_choices_open;    // and what can be done with a program (R): its choices and cursor.
    DvrAction dvr_choices[DVR_DO_COUNT];
    int dvr_choice_count, dvr_choice_cursor;
    LiveProgram dvr_program;  // The program they're for,
    char dvr_channel[LIVE_NAME_MAX];  // and its channel.
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
    bool settings_from_welcome; // Opened by the welcome on a page: B there goes back to it.
    int settings_cursor;        // The SettingRow under the cursor.

    // Jellyfin (app_jellyfin.c).
    JfStatus jf;
    bool jf_signing;            // The sign-in panel is up.
    int jf_arriving;            // A server just signed in (ServerKind + 1): said when its videos come; 0 for none.
    JfCatalog server_catalogs[SERVER_KIND_COUNT];  // Each server's library, kept to show it at once.
    bool server_has[SERVER_KIND_COUNT];
    char jf_last_error[128];
    uint64_t jf_last_refresh, jf_last_report;
    bool jf_reported_paused;    // What the last report said: a pause or going on is told at once.

    // Downloads (app_downloads.c).
    DownloadList downloads;     // A copy of the download thread's list.
    unsigned dl_changes, dl_finished;  // What the thread's counts were when it was taken.
    Scroll downloads_scroll;    // The Downloads tab's rows (app_dltab.c).
    int downloads_confirm;      // The row whose X was pressed once (drawn red), -1 for none,
    char downloads_confirm_path[LIBRARY_PATH_MAX];  // and its download's file, so a second X is for it.
    int downloads_confirm_state;                     // In the part of the tab it was in then.
    // The server's video being opened or played comes from its download on the card: its file, else "".
    char open_copy[LIBRARY_PATH_MAX];
};

// main.c
// A short message in the notice bubble at the top: something to know, done, or failed.
void appNotify(App *app, NoticeKind kind, const char *message, uint64_t now);
// The same, as something to know.
void appShowMessage(App *app, const char *message, uint64_t now);

// app_update.c: About's update rows. nro_path: argv[0] (where KaiVideo runs from).
void appStartUpdates(App *app, const char *nro_path);
void appStopUpdates(void);
// Each frame: says on screen what the update check or install has come to.
void appUpdatesTick(App *app, uint64_t now_ns);
// Look for an update: looks now, or installs the version found.
void appUpdatesActivate(App *app, uint64_t now_ns);
// What a row of About's says on its right (SETTING_UPDATE_AUTO or SETTING_UPDATE_NOW).
void appUpdatesValue(const App *app, int row, char *out, size_t size);
// Starts opening path, named name in the player's bar; the loading screen
// shows until it plays. False (with a message) if it can't even start.
bool appStartVideo(App *app, const char *path, const char *name, uint64_t now);
// Builds a file's path in KaiVideo's own folder.
void appConfigPath(char *out, size_t size, const char *name);
// Where downloads go while no library folder is chosen: in KaiVideo's own folder.
void appDownloadsFolderFirstRun(char *out, size_t size);
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
// Shows the network folder's library (what was found last time, and a new look).
void appLibraryShowNetwork(App *app);
// The IPTV list's view: its Live TV.
void appLibraryShowIptv(App *app);
// The network folder's account changed: its library is looked for again, or, gone, the card's shows.
void appLibraryNetworkChanged(App *app);
// Puts a library read from the server in place (taking it from c) with what it says was watched.
void appLibraryInstallCatalog(App *app, JfCatalog *c);
// app_scroll.c
// A finger drags a list of the library's screens (Live TV's guide included).
void appLibraryDrag(App *app, TouchEvent touch, uint64_t now);
// A finger came down, let go, or nothing: a list glides on, or stops (a tap
// that stopped a glide is taken out of touch).
void appLibraryGlide(App *app, uint64_t pressed, TouchEvent *touch, uint64_t now);
// The accent is the server's color while its library (or a video of it) shows,
// else the one chosen in Display.
void appApplyViewAccent(const App *app);
// Shows a server's library (the one kept, at once, or empty until it comes).
void appLibraryShowServer(App *app, ServerKind kind);
// A server's library came, or its server signed out (c NULL): kept, and shown if it's the one viewed.
void appLibraryServerCatalog(App *app, ServerKind kind, JfCatalog *c);

// app_jellyfin.c
// Starts the connection if it isn't running. False if the network can't be used.
bool appJellyfinEnsure(App *app);
// Once a frame: sign-in results, a new library, a refresh now and then.
void appJellyfinUpdate(App *app, uint64_t now);
void appJellyfinPlaybackStarted(App *app, uint64_t now);
void appJellyfinPlaybackTick(App *app, uint64_t now);
void appJellyfinPlaybackStopped(App *app, int64_t position_ns, bool watched);

// app_network.c: the network folder (dav.h).
// Reads its account, and puts it at the top of Files.
void appNetworkStart(App *app);
// A Settings row of its page: true if it was one.
bool appNetworkActivate(App *app, SettingRow row, uint64_t now);
// Before using the network folder: its password, when it isn't remembered and hasn't been
// typed since KaiVideo started, is asked for now. False if it wasn't given.
bool appNetworkEnsurePassword(App *app, uint64_t now);
// Once a frame: a test's answer.
void appNetworkUpdate(App *app, uint64_t now);
// Closes its connections and lets the console's network go.
void appNetworkExit(void);
// What its rows say, before Settings is drawn.
void appNetworkFeedSettings(void);

// app_iptv.c: the IPTV list. Reads its account and loads its channels.
void appIptvStart(App *app);
// A Settings row of its page: true if it was one.
bool appIptvActivate(App *app, SettingRow row, uint64_t now);
// Once a frame: a load that finished, taken (and said).
void appIptvUpdate(App *app, uint64_t now);
void appIptvExit(App *app);
// What its rows say, before Settings is drawn.
void appIptvFeedSettings(const App *app);
// Its Certificate check is off: its channels' streams take any certificate.
bool appIptvAnyCertificate(void);
// There's a list set: X goes to its view.
bool appIptvConfigured(void);

// app_live.c
// The IPTV list's channels or guide changed: the Live tab is made again.
void appLiveIptvChanged(App *app);
// Plays the IPTV list's index'th channel.
void appLivePlayIptv(App *app, int index, uint64_t now);
// The IPTV channel the Live guide's cursor is on, -1 if none.
int appLiveIptvSelected(const App *app);

// app_xtream.c: an Xtream account's films and series in the IPTV view.
// The load's catalog (taken).
void appXtreamTake(App *app, XtreamCatalog *c);
// The IPTV view's library made again from the catalog.
void appXtreamInstall(App *app);
// Once a frame: a series' episodes read when its page opens.
void appXtreamUpdate(App *app);
void appXtreamExit(App *app);
// What Films and Series show after the library or the category changed (appLibraryRefreshHome).
void appXtreamApplyGroups(App *app);
// Whether Films or Series is shown with categories to choose from (Y opens them).
bool appXtreamHasGroups(const App *app);
// The categories' pills' names for ui_library.c, each frame.
void appXtreamSetGridLabels(const App *app);
// The categories over Films or Series: opened, their buttons and taps, and drawn.
void appXtreamOpenGroups(App *app);
void appXtreamGroupsInput(App *app, uint64_t pressed, TouchEvent touch, uint64_t now);
void appXtreamGroupsDraw(const App *app);
// app_library.c: a library made for the IPTV or Addons view replaces the app's (taken).
void appLibraryInstallBuilt(App *app, Library *built);
// The Stremio addons' view.
void appLibraryShowAddons(App *app);

// app_stremio.c: Stremio addons.
void appStremioStart(App *app);
void appStremioExit(App *app);
bool appStremioConfigured(void);
// Once a frame: what the addons answered.
void appStremioUpdate(App *app, uint64_t now);
// The Addons view's library made again.
void appStremioInstall(App *app);
// Searches the addons too.
void appStremioSearch(App *app, const char *query);
// Whether a catalog's next page is being read (said where a scan's progress is).
bool appStremioReadingMore(const App *app);
// appStartVideo asks: false (and the streams are asked for, to choose one) until one is chosen.
bool appStremioMayOpen(App *app, const char *path, const char *name, uint64_t now);
void appStremioStreamsInput(App *app, uint64_t pressed, TouchEvent touch, uint64_t now);
void appStremioStreamsDraw(const App *app);
bool appStremioActivate(App *app, SettingRow row, uint64_t now);
void appStremioFeedSettings(const App *app);

// app_iptvhome.c: the IPTV view's Home, its recents and favorites.
void appIptvHomeStart(App *app);
void appIptvHomeExit(App *app);
// The list or the marks changed: its rows again, the cursor on the same row.
void appIptvHomeRebuild(App *app);
// On the first channel (entering the view: the one watched last).
void appIptvHomeReset(App *app);
// A channel opened (its location): first in the recents.
void appIptvHomeWatched(App *app, const char *location);
// The channel in the favorites, or out of them.
void appIptvToggleFavorite(App *app, int channel, uint64_t now);
bool appIptvIsFavorite(const App *app, int channel);
void appIptvHomeInput(App *app, uint64_t pressed, TouchEvent touch, uint64_t now);
void appIptvHomeDraw(const App *app);
// The IPTV list's channels and guide for the Showcase's rows of channels (none outside its view).
void appIptvShowcaseFeed(const App *app);
// Seconds the local time is ahead of UTC (app_live.c).
int64_t appUtcOffset(void);
// A finger moves its rows (up and down), or one of them (across).
bool appIptvHomeScrollPx(App *app, int px);
bool appIptvHomeRowScrollPx(App *app, int row, int px);
// The IPTV list's groups over the Live tab (Y): opened, and their buttons and taps.
void appLiveOpenGroups(App *app);
void appLiveGroupsInput(App *app, uint64_t pressed, TouchEvent touch, uint64_t now);
// Makes the guide shown from the server's channels and the IPTV group chosen.
void appLiveRebuild(App *app);
// A channel's location (a server's or an IPTV list's).
bool appIsLiveLocation(const char *location);
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

// app_dvr.c: recording Live TV with the Jellyfin server's DVR.
// Live TV is the Jellyfin server's here: its DVR records.
bool appDvrAvailable(const App *app);
// Once a frame (from appLiveUpdate): what the server answered.
void appDvrUpdate(App *app, uint64_t now);
void appDvrExit(App *app);
// The Live tab's buttons and taps for recording (R, L, their pills, the
// choices and the Recordings list). True if it took them.
bool appDvrInput(App *app, uint64_t pressed, TouchEvent touch, uint64_t now);
// Draws the Recordings list (true: the guide isn't drawn) or the choices over it.
bool appDvrDrawPanel(const App *app);
void appDvrDrawChoices(const App *app);
// The help line while the list or the choices show, or NULL.
const char *appDvrHelp(const App *app);
// The guide's marks, with what this console did lately over them (the server's lists may lag).
void appDvrMarkGuide(LiveGuide *g);
// Whether the server will record p (or records it now).
bool appDvrScheduled(const App *app, const LiveProgram *p);
// While a Jellyfin channel plays: ZR or its button records what's on now; drawn with the bar.
bool appDvrPlayerInput(App *app, uint64_t pressed, TouchEvent touch, uint64_t now);
void appDvrPlayerDraw(const App *app, bool bar_shown);

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

// app_downloads.c
void appDownloadsStart(App *app);  // After the settings are read.
void appDownloadsExit(App *app);
// Once a frame: downloads wait while a server's video streams; a finished one
// is looked for in the card's library.
void appDownloadsUpdate(App *app, uint64_t now);
// The whole download of a server's video (any user's), or NULL.
const char *appDownloadedCopy(const App *app, const char *location);
// The same, if its file is still on the card: what a server's video plays from.
const char *appPlayableCopy(const App *app, const char *location);
// What the details panel says above the summary: that playing it makes the
// downloads wait (before A is pressed), then how the server plays it; "" for nothing.
const char *appDetailsNote(const App *app, int item, char *out, size_t size);
// What the details panel's download button says for item, NULL for no button.
const char *appDownloadLabel(const App *app, int item, char *out, size_t size);
// L on a film or episode's details: downloads it, or pauses, goes on, tries again.
void appDownloadItem(App *app, int item, uint64_t now);
// What a series page's button says for its season shown; NULL for none (not a server's).
// done: every episode is on the card.
const char *appSeasonDownloadLabel(const App *app, int show, int season, bool *done, char *out, size_t size);
// L on a series page: the season shown, every episode not yet downloaded.
void appDownloadSeason(App *app, int show, int season, uint64_t now);
// A video stopped: if it's a download (or a server's video played from one),
// both its places keep where it was left, and its server will be told.
void appDownloadsPlayed(App *app, int64_t position_ns, bool finished);
// A video was marked watched or not by hand: the same for its download's other place, and its server is told.
void appDownloadsMarked(App *app, const char *path, bool watched);
// A server's library came (kind): downloads of it take what it says of them,
// unless they were watched here since; what waits to be told goes now.
void appDownloadsCatalogCame(App *app, ServerKind kind);
// After a server's library replaced resume points and watched marks: those of
// downloads watched offline, not yet told to their server, are put back.
void appDownloadsOverlayOffline(App *app);
// Downloads on the card take their server's names (the card's library, built again).
void appDownloadsNameLibrary(const App *app, Library *lib);
// Takes the download thread's list again (after asking it something).
void appDownloadsRefresh(App *app);
// The library has a Downloads tab: something was downloaded, or a server is signed in.
bool appDownloadsTabShown(const App *app);

// app_dltab.c: the Downloads tab, in sections: downloading, on the card, deleted from it.
// Goes to it (+ menu, the header's pill), or says there's nothing to show.
void appDownloadsOpen(App *app, uint64_t now);
void appDownloadsInput(App *app, uint64_t pressed, TouchEvent touch, uint64_t now);
void appDownloadsDraw(const App *app);
// The help line for the row under the cursor.
const char *appDownloadsHelp(const App *app);
// A finger moves its list by px. False at an end.
bool appDownloadsScrollPx(App *app, int px);
// The list changed: the cursor stays on a download, the second X is asked again.
void appDownloadsTabChanged(App *app);

// app_menu.c
void appOpenMenu(App *app);
void appMenuInput(App *app, uint64_t pressed, TouchEvent touch, uint64_t now);
void appMenuDraw(const App *app);
// After a video stops: runs the + menu's item chosen over it (Downloads, Update the library, Settings).
void appMenuAfterVideo(App *app, uint64_t now);
// The help line for the library's screens and overlays: "[A] open  [+] menu".
const char *appFooterHelp(const App *app);

// app_settings.c
// Applies the settings read at start: theme, background, thumbnails, subtitles.
void appSettingsStart(App *app);
// Applies the subtitles' size, color and style (changed in the player).
void appApplySubtitleStyle(const App *app);
// The Subtitles list's Font: the built-in one, then each file in config/fonts.
// False, with what to say in note, if there are none or one can't be read.
bool appStepSubtitleFont(App *app, int delta, char *note, size_t note_size);
void appOpenSettings(App *app);
// Settings, on one of its pages (B goes back to the main page from it).
void appOpenSettingsAt(App *app, SettingsPage page);
// Once per frame: the first run's welcome while nothing says where the videos
// are and it wasn't answered (again after a page it opened is left without one).
void appWelcomeTick(App *app);
void appOpenJellyfinSettings(App *app, uint64_t now);
void appSettingsInput(App *app, uint64_t pressed, TouchEvent touch, uint64_t now);
void appSettingsDraw(const App *app);
