// KaiVideo: a video player for Switch 2 homebrew (kaiHEN + libounce). Video
// is decoded by the console's hardware decoder and drawn with the GPU; sound
// is decoded with FFmpeg and is the clock. Choose a film or an episode in
// the library (app_library.c), or a file in the Files tab, then A pauses,
// left and right go 10 seconds back or forward, L and R a minute, up opens
// the Playback list (chapters, speed, sync delays), X and Y the lists of
// sound tracks and subtitles, - hides the bar, and B goes back. In an episode's last 30 seconds a card offers the next one: ZR plays
// it now, and it plays on its own at the end. Every control works with
// touch too.
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <switch.h>

#include "app.h"
#include "approot.h"
#include "audio.h"
#include "browser.h"
#include "buttons.h"
#include "font.h"
#include "font_bold_ttf.h"
#include "font_regular_ttf.h"
#include "gfx.h"
#include "input.h"
#include "hlsio.h"
#include "mediafile.h"
#include "menu.h"
#include "playback.h"
#include "player.h"
#include "resume.h"
#include "sysinfo.h"
#include "thumbs.h"
#include "tpfetch.h"
#include "ui_library.h"
#include "touch.h"
#include "ui.h"
#include "util.h"

#define OLD_CONFIG_FOLDER "sdmc:/config/kaivideo"  // Before files went next to the NRO.
#define OVERLAY_NS 3000000000ull     // How long the player's bar stays after a press.
#define MESSAGE_NS 5000000000ull     // How long an error shows in the list.
#define NOTE_NS 3000000000ull        // How long "Subtitles: English" shows.
#define SUBTITLE_UNSAID (-2)         // Opening: no picture subtitle said; the one chosen before, if any.
#define SEEK_SHORT_NS 10000000000ll
#define STALL_NS 2000000000ull       // Playing without moving this long is logged.
#define WAITING_NS 1000000000ull     // Without moving this long, the wait is shown.
#define SEEK_LONG_NS 60000000000ll
#define UP_NEXT_NS 30000000000ll     // The next episode's card shows this long before the end.
#define SCREEN_W 1280
#define SCREEN_H 720
#define PANEL_W 1920  // In case the touch panel reports its own resolution.

#define SPINNER_TURN_NS 1000000000ull
#define SPINNER_DELAY_NS 400000000ull  // Quick waits show nothing.

static FILE *g_log;
// KaiVideo's own files, next to its NRO (approot.h): settings.txt,
// resume.txt, watched.txt, tracks.txt and kaivideo.log.
static char g_folder[APPROOT_PATH_MAX];

void appConfigPath(char *out, size_t size, const char *name)
{
    snprintf(out, size, "%s/%s", g_folder, name);
}

void appLog(const char *fmt, ...)
{
    if (!g_log)
        return;
    va_list args;
    va_start(args, fmt);
    vfprintf(g_log, fmt, args);
    va_end(args);
    fputc('\n', g_log);
    fflush(g_log);
}

static uint64_t nowNs(void)
{
    return armTicksToNs(armGetSystemTick());
}

// --- Settings kept between runs: the library folder and the Files tab's ---

static void loadSettings(App *app, char *folder, size_t size)
{
    snprintf(folder, size, "%s", BROWSER_ROOT);
    configDefaults(&app->config);
    char path[APPROOT_PATH_MAX + 32];
    appConfigPath(path, sizeof(path), "settings.txt");
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    char line[BROWSER_PATH_MAX];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strncmp(line, "folder=sdmc:/", 13) == 0)
            snprintf(folder, size, "%s", line + 7);
        else if (strncmp(line, "library=sdmc:/", 14) == 0)
            snprintf(app->library_root, sizeof(app->library_root), "%s", line + 8);
        else
            configReadLine(&app->config, line);
    }
    fclose(f);
}

void appSaveSettings(const App *app)
{
    char path[APPROOT_PATH_MAX + 32];
    appConfigPath(path, sizeof(path), "settings.txt");
    FILE *f = fopen(path, "w");
    if (!f)
        return;
    if (!strpbrk(app->browser.path, "\r\n"))
        fprintf(f, "folder=%s\n", app->browser.path);
    if (app->library_root[0] && !strpbrk(app->library_root, "\r\n"))
        fprintf(f, "library=%s\n", app->library_root);
    configWrite(&app->config, f);
    fclose(f);
}

void appSaveWatched(const App *app)
{
    char path[APPROOT_PATH_MAX + 32];
    appConfigPath(path, sizeof(path), "watched.txt");
    watchedSave(&app->watched, path);
}

// --- Touch ---

static TouchEvent readTouch(TouchTracker *tracker, uint64_t now)
{
    static bool full_hd;  // Seen coordinates beyond 1280x720: scale them down.
    HidTouchScreenState state = {0};
    bool touching = hidGetTouchScreenStates(&state, 1) > 0 && state.count > 0;
    int x = 0, y = 0;
    if (touching) {
        x = (int)state.touches[0].x;
        y = (int)state.touches[0].y;
        if (x >= SCREEN_W || y >= SCREEN_H)
            full_hd = true;
        if (full_hd) {
            x = x * SCREEN_W / PANEL_W;
            y = y * SCREEN_W / PANEL_W;
        }
    }
    return touchUpdate(tracker, touching, x, y, now);
}

// --- Playing ---

void appShowMessage(App *app, const char *message, uint64_t now)
{
    snprintf(app->message, sizeof(app->message), "%s", message);
    app->message_until = now + MESSAGE_NS;
}

// The series of the video playing, or NULL.
static const char *playingSeries(const App *app)
{
    int item = app->playing_item;
    int show = item >= 0 && (size_t)item < app->library.count ? app->library.items[item].show : -1;
    return show >= 0 ? app->library.shows[show].name : NULL;
}

// The sound and subtitles chosen last time for this video, or its series.
// Tracks are found by name: another episode may have them in another order.
static void applyTrackPrefs(App *app, uint64_t now)
{
    Player *p = app->player;
    const char *sound, *subs;
    if (!trackPrefsFind(&app->tracks, app->playing_path, playingSeries(app), &sound, &subs))
        return;
    // A converted video was already asked for with its sound.
    for (int i = 0; sound[0] && !playerIsConverted(p) && i < playerAudioTrackCount(p); i++)
        if (strcmp(playerAudioTrackName(p, i), sound) == 0) {
            if (i != playerAudioTrack(p))
                playerSetAudioTrack(p, i, now);
            break;
        }
    if (strcmp(subs, TRACKPREFS_OFF) == 0)
        playerSetSubtitle(p, -1);
    for (int i = 0; subs[0] && i < playerSubtitleCount(p); i++)
        if (strcmp(playerSubtitleLabel(p, i), subs) == 0) {
            playerSetSubtitle(p, i);
            break;
        }
}

static void rememberTracks(App *app, const char *sound, const char *subs)
{
    trackPrefsRemember(&app->tracks, app->playing_path, playingSeries(app), sound, subs);
    char path[APPROOT_PATH_MAX + 32];
    appConfigPath(path, sizeof(path), "tracks.txt");
    trackPrefsSave(&app->tracks, path);
}

// The sound track with the name chosen before, if the server should use another.
static int preferredSound(const App *app, const JfPlan *plan)
{
    for (int i = 0; app->open_sound[0] && i < plan->audio_count; i++)
        if (!strcmp(plan->audio[i].name, app->open_sound))
            return i != plan->audio_current ? plan->audio[i].index : -1;
    return -1;
}

// The picture subtitle chosen before for this video or series (by its name
// in the list): the server's number, -1 if none of this video's is that one.
static int preferredPicture(const App *app, const JfPlan *plan)
{
    char label[JF_TRACK_NAME_MAX];  // As long as the list's (player.c), so a long name matches cut as it is.
    for (int i = 0; app->open_subs[0] && i < plan->picture_count; i++) {
        jfPictureLabel(&plan->pictures[i], label, sizeof(label));
        if (!strcmp(label, app->open_subs))
            return plan->pictures[i].index;
    }
    return -1;
}

// Live TV: the channel straight from its own HLS source, without the server
// converting it, when it has one the console can read. True if it plays so.
static bool openLiveDirect(App *app, long long bitrate, char *path, size_t size)
{
    JfPlan *plan = &app->open_plan;
    const volatile bool *cancel = &app->open_cancelled;
    if (!liveIsLocation(app->playing_path) || !jfSyncLiveSource(app->playing_path, plan, cancel))
        return false;
    if (!hlsStartDirect(plan->source_url, bitrate, cancel, app->open_error, sizeof(app->open_error))) {
        if (!app->open_cancelled)
            appLog("  %s: the server converts it instead", app->open_error);
        memset(plan, 0, sizeof(*plan));
        app->open_error[0] = '\0';
        return false;
    }
    appLog("  live from the channel's own source");
    snprintf(path, size, HLS_LOCATION);
    return true;
}

// Asks the server how to play the video, and starts its conversion if that's
// the way. What to open goes in path. False, with the reason, if it can't.
// B (open_cancelled) cuts each step short. subtitle_index: a picture subtitle
// for the server to draw (-1 none), or SUBTITLE_UNSAID for the one chosen before.
static bool prepareRemote(App *app, int audio_index, int subtitle_index, bool try_direct, char *path, size_t size)
{
    JfPlan *plan = &app->open_plan;
    long long bitrate = settingQualityBitrate(&app->config);
    char *error = app->open_error;
    const volatile bool *cancel = &app->open_cancelled;
    if (try_direct && audio_index < 0 && openLiveDirect(app, bitrate, path, size))
        return true;
    int subtitle = subtitle_index == SUBTITLE_UNSAID ? -1 : subtitle_index;
    if (app->open_cancelled || !jfSyncResolve(app->playing_path, bitrate, audio_index, subtitle, plan, error,
                                              sizeof(app->open_error), cancel))
        return false;
    // Asked again if the sound or the picture subtitles chosen before need another conversion.
    int sound = audio_index < 0 && !plan->direct ? preferredSound(app, plan) : -1;
    int picture = subtitle_index == SUBTITLE_UNSAID ? preferredPicture(app, plan) : -1;
    if ((sound >= 0 || picture >= 0) &&
        !jfSyncResolve(app->playing_path, bitrate, sound >= 0 ? sound : audio_index, picture >= 0 ? picture : subtitle,
                       plan, error, sizeof(app->open_error), cancel))
        return false;
    if (plan->direct) {
        snprintf(path, size, "%s", app->playing_path);
        return true;
    }
    char url[JF_URL_MAX + JF_TRANSCODE_URL_MAX], auth[JF_AUTH_MAX];
    jfSyncConvertedRequest(plan->transcode_url, url, sizeof(url), auth, sizeof(auth));
    snprintf(path, size, HLS_LOCATION);
    return hlsStart(url, auth, app->open_start_ns, cancel, error, sizeof(app->open_error));
}

static void *openThread(void *arg)
{
    App *app = arg;
    char path[BROWSER_PATH_MAX];
    bool remote = jfIsLocation(app->playing_path);
    snprintf(path, sizeof(path), "%s", app->playing_path);
    int audio_index = app->next_audio_plus1 - 1;
    int subtitle_index = app->next_subtitle_set ? app->next_subtitle : SUBTITLE_UNSAID;
    app->next_audio_plus1 = 0;
    app->next_subtitle_set = false;
    if (!remote || prepareRemote(app, audio_index, subtitle_index, true, path, sizeof(path)))
        app->open_result = playerOpen(path, nowNs(), app->open_error, sizeof(app->open_error));
    if (!app->open_result && app->open_plan.source_url[0] && !app->open_cancelled) {
        // The channel's own stream opened but won't play here: the server converts it.
        appLog("  the channel's own stream won't play (%s): the server converts it instead", app->open_error);
        hlsStop();
        memset(&app->open_plan, 0, sizeof(app->open_plan));
        app->open_error[0] = '\0';
        if (prepareRemote(app, audio_index, subtitle_index, false, path, sizeof(path)))
            app->open_result = playerOpen(path, nowNs(), app->open_error, sizeof(app->open_error));
    }
    if (app->open_result && remote && !liveIsLocation(app->playing_path))
        app->segment_count = jfSyncFetchSegments(app->playing_path, app->segments, JF_SEGMENTS_MAX);
    if (app->open_result && remote)
        playerAttachPlan(app->open_result, &app->open_plan, app->playing_path);
    app->open_done = true;
    return NULL;
}

bool appStartVideo(App *app, const char *path, const char *name, uint64_t now)
{
    if (app->opening && app->open_dismissed) {  // The one let go still ends: this one opens after it.
        app->queued = true;
        app->queued_from_start = app->open_from_start;
        app->queued_at_ns = app->open_at_ns;
        snprintf(app->queued_path, sizeof(app->queued_path), "%s", path);
        snprintf(app->queued_name, sizeof(app->queued_name), "%s", name);
        app->open_from_start = false;
        app->open_at_ns = 0;
        return true;
    }
    if (app->opening)
        return false;
    app->open_dismissed = false;
    thumbsPause(true);  // The decoder is the player's now.
    snprintf(app->playing_name, sizeof(app->playing_name), "%s", name);
    snprintf(app->playing_path, sizeof(app->playing_path), "%s", path);
    app->open_done = false;
    app->open_asked = now;
    app->segment_count = 0;
    app->open_cancelled = false;
    app->open_error[0] = '\0';
    app->open_result = NULL;
    memset(&app->open_plan, 0, sizeof(app->open_plan));  // Nothing of the last video's is ended for this one.
    app->open_sound[0] = '\0';
    app->open_subs[0] = '\0';
    app->quality_applied = app->config.jf_quality;
    app->open_start_ns = app->open_at_ns;
    if (!app->open_at_ns && !app->open_from_start && !resumeFind(&app->resume, path, &app->open_start_ns, NULL))
        app->open_start_ns = 0;
    int item = libraryFind(&app->library, path);
    int show = item >= 0 ? app->library.items[item].show : -1;
    const char *sound = NULL, *subs = NULL;
    if (jfIsLocation(path) && trackPrefsFind(&app->tracks, path, show >= 0 ? app->library.shows[show].name : NULL,
                                             &sound, &subs)) {
        snprintf(app->open_sound, sizeof(app->open_sound), "%s", sound);
        snprintf(app->open_subs, sizeof(app->open_subs), "%s", subs);
    }
    app->opening = pthread_create(&app->open_thread, NULL, openThread, app) == 0;
    if (!app->opening) {
        thumbsPause(false);
        appShowMessage(app, "The video can't be opened now.", now);
    }
    return app->opening;
}

// The video is open: everything that goes with starting it.
static void beginPlaying(App *app, uint64_t now)
{
    const char *path = app->playing_path;
    app->waiting_since = now;
    app->playing_item = libraryFind(&app->library, path);
    app->next_item = libraryNextEpisode(&app->library, app->playing_item);
    app->next_dismissed = false;
    app->play_next = false;
    app->hud_hidden = false;
    app->bar_note[0] = '\0';
    menuClose(&app->menu);
    playerSetSpeed(app->player, playbackSpeedPermille(app->speed_index), now);
    applyTrackPrefs(app, now);
    int64_t resume_at;
    bool from_start = app->open_from_start;
    int64_t open_at = app->open_at_ns;
    app->open_from_start = false;
    app->open_at_ns = 0;
    app->playing_converted = playerIsConverted(app->player);
    if (jfIsLocation(path)) {
        jfSyncSetPlayback(app->open_plan.play_session, app->playing_converted, app->open_plan.live_stream_id);
        if (!liveIsLocation(path))
            tpfetchStart(path);  // Its pictures for the bar, if the server made them.
    }
    if (open_at > 0) {
        playerSeekTo(app->player, open_at, now);
    } else if (!from_start && resumeFind(&app->resume, path, &resume_at, NULL)) {
        // Where it was left: go on from there, and say how to start over.
        playerSeekTo(app->player, resume_at, now);
        char at[16];
        utilFormatTime(at, sizeof(at), resume_at / 1e9);
        snprintf(app->bar_note, sizeof(app->bar_note), "Resumed at %s  \xC2\xB7  ZL from the start", at);
        app->bar_note_until = now + OVERLAY_NS + 2000000000ull;
    }
    appLog("%s: %s; %s", path, playerVideoInfo(app->player), playerAudioInfo(app->player));
    appLog("  opened in %.1f s", (now - app->open_asked) / 1e9);
    for (int i = 0; i < playerAudioTrackCount(app->player); i++)
        appLog("  sound %d: %s%s", i + 1, playerAudioTrackName(app->player, i),
               i == playerAudioTrack(app->player) ? " (on)" : "");
    for (int i = 0; i < playerSubtitleCount(app->player); i++)
        appLog("  subtitles %d: %s%s", i + 1, playerSubtitleLabel(app->player, i),
               i == playerSubtitle(app->player) ? " (on)" : "");
    app->overlay_until = now + OVERLAY_NS;
    app->seeks = 0;
    app->started = now;
    appJellyfinPlaybackStarted(app, now);
}

// Takes the opening thread's result once it has finished.
// An opening that won't play: what the server started for it (a conversion,
// a channel's stream holding a tuner) is ended.
static void releaseServerStream(App *app)
{
    if (!jfIsLocation(app->playing_path))
        return;
    if (!app->open_plan.direct && app->open_plan.play_session[0]) {
        jfSyncSetPlayback(app->open_plan.play_session, true, NULL);  // This opening's, not the last video's.
        jfSyncEndConversion(app->playing_path);
    }
    if (liveIsLocation(app->playing_path))
        jfSyncCloseLiveStream(app->playing_path, app->open_plan.live_stream_id);
    app->open_plan.live_stream_id[0] = '\0';
}

static void finishOpening(App *app, uint64_t now)
{
    pthread_join(app->open_thread, NULL);
    app->opening = false;
    app->player = app->open_result;
    if (app->player && app->open_cancelled) {
        playerClose(app->player);
        app->player = NULL;
        thumbsPause(false);
        hlsStop();
        hlsStopAudio();
        releaseServerStream(app);
    } else if (app->player) {
        beginPlaying(app, now);
    } else {
        hlsStop();
        hlsStopAudio();
        releaseServerStream(app);
        thumbsPause(false);
        appLog("%s: %s", app->playing_path, app->open_error);
        if (!app->open_cancelled)
            appShowMessage(app, app->open_error[0] ? app->open_error : "The video couldn't be loaded.", now);
    }
}

// A turn of the loading symbol, from the time.
static float spinnerAngle(uint64_t now)
{
    return (float)((now % SPINNER_TURN_NS) * (2.0 * 3.14159265) / SPINNER_TURN_NS);
}

static void stopVideo(App *app, const char *error, bool ended)
{
    // Remember where it was left (or, watched to the end, forget it).
    int64_t duration = playerDuration(app->player), position = ended ? duration : playerPosition(app->player);
    bool live = appPlayingLive(app);  // A channel: nothing to resume or mark watched.
    if (!live)
        resumeRemember(&app->resume, app->playing_path, position, duration);
    bool finished = !live && duration > 0 && duration - position < RESUME_END_MARGIN_NS;
    if (finished) {  // Watched, as resuming sees it.
        watchedSet(&app->watched, app->playing_path, true);
        appSaveWatched(app);
    }
    appJellyfinPlaybackStopped(app, position, finished);
    char path[APPROOT_PATH_MAX + 32];
    appConfigPath(path, sizeof(path), "resume.txt");
    resumeSave(&app->resume, path);
    char state[256];
    playerDescribe(app->player, state, sizeof(state));
    appLog("  at the end: %s", state);
    VideoStats s = playerStats(app->player);
    appLog("  %s%s%.1f s, %u seeks: %u frames decoded, %u shown, %u dropped as late", error && error[0] ? error : "",
            error && error[0] ? "; " : "", (nowNs() - app->started) / 1e9, app->seeks, s.decoded, s.shown,
            s.dropped);
    mediaFileInterruptAll();  // Nothing waits for the network now.
    tpfetchStop();
    playerClose(app->player);
    app->player = NULL;
    hlsStop();
    hlsStopAudio();
    if (app->playing_converted)
        jfSyncEndConversion(app->playing_path);
    if (live)
        jfSyncCloseLiveStream(app->playing_path, app->open_plan.live_stream_id);
    app->playing_converted = false;
    app->live_switch = 0;  // A switch asked for as it ended isn't for the next video.
    thumbsPause(false);
    appLibraryRefreshHome(app);  // Continue watching changed.
}

// True if playback really got to the end, and didn't just stop reading.
static bool reachedEnd(const Player *p)
{
    int64_t duration = playerDuration(p);
    return duration <= 0 || duration - playerPosition(p) <= RESUME_END_MARGIN_NS;
}

// Opens the video again at position, with this sound and this picture
// subtitle drawn on it (the server's numbers; -1 for no subtitle).
static void reopenVideo(App *app, int audio_index, int subtitle_index, int64_t position, uint64_t now)
{
    char path[BROWSER_PATH_MAX], name[APP_NAME_MAX];
    snprintf(path, sizeof(path), "%s", app->playing_path);
    snprintf(name, sizeof(name), "%s", app->playing_name);
    bool live = appPlayingLive(app);  // A channel opens anew at what's on now, not at a position.
    stopVideo(app, NULL, false);
    app->open_at_ns = live ? 0 : position;
    app->open_from_start = live || position <= 0;
    app->next_audio_plus1 = audio_index + 1;
    app->next_subtitle_set = true;
    app->next_subtitle = subtitle_index;
    appStartVideo(app, path, name, now);
}

// The server's number for the sound track playing, -1 if unknown.
static int currentSound(const App *app)
{
    int track = playerAudioTrack(app->player);
    return track >= 0 && track < app->open_plan.audio_count ? app->open_plan.audio[track].index : -1;
}

// The server's number for the picture subtitle it draws, -1 if none.
static int currentPicture(const App *app)
{
    const JfPlan *plan = &app->open_plan;
    int burned = plan->picture_burned;
    return burned >= 0 && burned < plan->picture_count ? plan->pictures[burned].index : -1;
}

// L or R on a channel: the one before or after it in the guide, at once.
static void switchChannel(App *app, uint64_t now)
{
    int delta = app->live_switch;
    app->live_switch = 0;
    char location[BROWSER_PATH_MAX], name[APP_NAME_MAX];
    if (!appLiveNeighbor(app, delta, location, sizeof(location), name, sizeof(name)))
        return;
    appLog("  channel: %s", name);
    stopVideo(app, NULL, false);
    app->open_from_start = true;  // A channel has no place to resume.
    if (!appStartVideo(app, location, name, now))
        app->open_from_start = false;
}

// L or R while a channel opens: that opening is let go, and the channel
// they lead to (counting every press) opens after it.
static void switchWhileOpening(App *app, uint64_t pressed)
{
    if (!liveIsLocation(app->playing_path) || !(pressed & (HidNpadButton_L | HidNpadButton_R)))
        return;
    int delta = app->live_pending + ((pressed & HidNpadButton_L) ? -1 : 1);
    char location[BROWSER_PATH_MAX], name[APP_NAME_MAX];
    if (!appLiveNeighbor(app, delta, location, sizeof(location), name, sizeof(name)))
        return;
    app->live_pending = delta;  // Back round to the channel opening (0) opens it again.
    app->live_pending_set = true;
    snprintf(app->live_pending_name, sizeof(app->live_pending_name), "%s", name);
    if (!app->open_cancelled) {
        app->open_cancelled = true;
        mediaFileInterruptAll();
    }
}

// The channel L and R led to while another opened, once that one is let go.
static void openPendingChannel(App *app, uint64_t now)
{
    int delta = app->live_pending;
    app->live_pending = 0;
    app->live_pending_set = false;
    char location[BROWSER_PATH_MAX], name[APP_NAME_MAX];
    if (!appLiveNeighbor(app, delta, location, sizeof(location), name, sizeof(name)))
        return;
    appLog("  channel: %s", name);
    app->open_from_start = true;
    if (!appStartVideo(app, location, name, now))
        app->open_from_start = false;
}

// The video asked for while a dismissed opening ended.
static void openQueued(App *app, uint64_t now)
{
    app->queued = false;
    app->open_from_start = app->queued_from_start;
    app->open_at_ns = app->queued_at_ns;
    if (!appStartVideo(app, app->queued_path, app->queued_name, now)) {
        app->open_from_start = false;
        app->open_at_ns = 0;
    }
}

static void reopenIfAsked(App *app, uint64_t now)
{
    int audio, picture;
    int64_t at;
    if (!jfIsLocation(app->playing_path))
        return;
    if (playerTakeAudioRestart(app->player, &audio)) {
        reopenVideo(app, audio, currentPicture(app), playerPosition(app->player), now);
    } else if (playerTakeSubtitleRestart(app->player, &picture)) {
        reopenVideo(app, currentSound(app), picture, playerPosition(app->player), now);
    } else if (playerTakeReopen(app->player, now, &at)) {
        reopenVideo(app, currentSound(app), currentPicture(app), at, now);
    } else if (!app->menu.open && app->quality_applied != app->config.jf_quality) {
        appSaveSettings(app);
        reopenVideo(app, currentSound(app), currentPicture(app), playerPosition(app->player), now);
    }
}

static void sayInBar(App *app, uint64_t now, const char *fmt, const char *arg)
{
    snprintf(app->bar_note, sizeof(app->bar_note), fmt, arg);
    app->bar_note_until = now + NOTE_NS;
    app->overlay_until = now + NOTE_NS;
}

// The options under the Playback list: the quality only for videos from a server.
static int playbackOptionRows(const App *app)
{
    return jfIsLocation(app->playing_path) ? PLAYBACK_OPT_COUNT : PLAYBACK_OPT_QUALITY;
}

// Opens the list of sound tracks or subtitles (or says there's nothing to choose).
// The options under the subtitle list, for the track chosen now.
static int subtitleOptionRows(const Player *p)
{
    return subtitleOptionCount(playerSubtitleColorApplies(p));
}

static void openMenu(App *app, UiMenuKind kind, uint64_t now)
{
    Player *p = app->player;
    menuClose(&app->menu);
    if (kind == UI_MENU_PLAYBACK) {
        // On the chapter playing; without chapters, on the speed.
        int chapter = playerChapter(p);
        int rows = playbackOptionRows(app);
        menuOpen(&app->menu, playerChapterCount(p) + rows, rows, chapter > 0 ? chapter : 0);
    } else if (kind == UI_MENU_AUDIO && playerAudioTrackCount(p) < 2)
        sayInBar(app, now, "%s", "This video has one sound track.");
    else if (kind == UI_MENU_SUBTITLES && playerSubtitleCount(p) == 0)
        sayInBar(app, now, "%s", "This video has no subtitles.");
    else if (kind == UI_MENU_AUDIO)
        menuOpen(&app->menu, playerAudioTrackCount(p), 0, playerAudioTrack(p));
    else {
        // Row 0 is "Off"; the options follow the tracks.
        int options = subtitleOptionRows(p);
        menuOpen(&app->menu, playerSubtitleCount(p) + 1 + options, options, playerSubtitle(p) + 1);
    }
    app->menu_kind = kind;
    app->drag_rows = 0;
}

// Changes one of the Playback list's options, for this video (the speed
// stays for the next ones).
static void stepPlaybackOption(App *app, PlaybackOption option, int delta, uint64_t now)
{
    Player *p = app->player;
    if (option == PLAYBACK_OPT_SPEED) {
        app->speed_index = playbackSpeedStep(app->speed_index, delta);
        playerSetSpeed(p, playbackSpeedPermille(app->speed_index), now);
    } else if (option == PLAYBACK_OPT_AUDIO_DELAY && playerAudioTrackCount(p) > 0) {
        playerSetAudioDelay(p, playbackDelayStep(playerAudioDelay(p), delta));
    } else if (option == PLAYBACK_OPT_SUBTITLE_DELAY && playerSubtitleCount(p) > 0) {
        playerSetSubtitleDelay(p, playbackDelayStep(playerSubtitleDelay(p), delta));
    } else if (option == PLAYBACK_OPT_QUALITY) {
        settingStep(&app->config, SETTING_JF_QUALITY, delta);  // Applied when the list closes.
    }
}

// Changes one of the list's options (its fixed rows); the subtitles' are kept.
static void stepOption(App *app, int row, int delta, uint64_t now)
{
    int index = row - (app->menu.count - app->menu.fixed);
    if (app->menu_kind == UI_MENU_PLAYBACK) {
        stepPlaybackOption(app, (PlaybackOption)index, delta, now);
        return;
    }
    SubtitleOption option = (SubtitleOption)index;
    subtitleOptionStep(&app->config, option, delta);
    appApplySubtitleStyle(app);
    appSaveSettings(app);
}

// Plays or shows the list's row, said in the bar, and kept for next time.
// The sound list closes; the subtitles' stays, for their size, color and
// style below. A chapter is gone to. An option row changes instead.
static void chooseRow(App *app, int row, uint64_t now)
{
    Player *p = app->player;
    if (row >= app->menu.count - app->menu.fixed) {
        stepOption(app, row, 1, now);
        return;
    }
    if (app->menu_kind == UI_MENU_PLAYBACK) {
        playerSeekTo(p, playerChapterStart(p, row), now);
        app->seeks++;
        sayInBar(app, now, "%s", playerChapterName(p, row));
        menuClose(&app->menu);
    } else if (app->menu_kind == UI_MENU_AUDIO) {
        if (row != playerAudioTrack(p))
            playerSetAudioTrack(p, row, now);
        sayInBar(app, now, "Sound: %s", playerAudioTrackName(p, row));
        rememberTracks(app, playerAudioTrackName(p, row), NULL);
        menuClose(&app->menu);
    } else {
        playerSetSubtitle(p, row - 1);
        menuSetFixed(&app->menu, subtitleOptionRows(p));
        if (row == 0)
            sayInBar(app, now, "%s", "Subtitles off");
        else
            sayInBar(app, now, "Subtitles: %s", playerSubtitleLabel(p, row - 1));
        rememberTracks(app, NULL, row == 0 ? TRACKPREFS_OFF : playerSubtitleLabel(p, row - 1));
    }
}

// A tap outside the open list closes it; on the other list's button, that
// list opens instead.
static void tapOutsideMenu(App *app, int x, int y, uint64_t now)
{
    Player *p = app->player;
    double fraction;
    TapTarget target = uiTapPlayer(x, y, playerSubtitleCount(p) > 0, playerAudioTrackCount(p) > 1, &fraction);
    if (target == TAP_PLAYBACK && app->menu_kind != UI_MENU_PLAYBACK)
        openMenu(app, UI_MENU_PLAYBACK, now);
    else if (target == TAP_AUDIO && app->menu_kind != UI_MENU_AUDIO)
        openMenu(app, UI_MENU_AUDIO, now);
    else if (target == TAP_SUBTITLES && app->menu_kind != UI_MENU_SUBTITLES)
        openMenu(app, UI_MENU_SUBTITLES, now);
    else
        menuClose(&app->menu);
}

// The open list takes the buttons and touches: up and down move, A
// chooses, B closes; X and Y go to their list (or close it if it's open).
static void menuInput(App *app, uint64_t pressed, TouchEvent touch, uint64_t now)
{
    Menu *m = &app->menu;
    app->overlay_until = now + OVERLAY_NS;  // The bar stays while the list is open.
    if (pressed & BUTTONS_UP) menuMove(m, -1);
    if (pressed & BUTTONS_DOWN) menuMove(m, 1);
    if (menuOnFixed(m) && (pressed & (BUTTONS_LEFT | BUTTONS_RIGHT)))
        stepOption(app, m->cursor, (pressed & BUTTONS_LEFT) ? -1 : 1, now);
    if (pressed & HidNpadButton_A) {
        chooseRow(app, m->cursor, now);
        return;
    }
    if (pressed & HidNpadButton_B) {
        menuClose(m);
        return;
    }
    if (pressed & HidNpadButton_Plus) {  // The + menu, as everywhere.
        menuClose(m);
        appOpenMenu(app);
        return;
    }
    if (pressed & (HidNpadButton_X | HidNpadButton_Y)) {
        UiMenuKind kind = (pressed & HidNpadButton_X) ? UI_MENU_AUDIO : UI_MENU_SUBTITLES;
        if (kind == app->menu_kind)
            menuClose(m);
        else
            openMenu(app, kind, now);
        return;
    }
    int row;
    if (touch.type == TOUCH_TAP) {
        TapTarget target = uiTapTrackMenu(app->menu_kind, m, touch.x, touch.y, &row);
        if (target == TAP_MENU_ROW && app->menu_kind == UI_MENU_PLAYBACK && row >= m->count - m->fixed)
            stepOption(app, row, uiTapOptionDelta(app->menu_kind, m, touch.x), now);  // Delays go both ways.
        else if (target == TAP_MENU_ROW)
            chooseRow(app, row, now);
        else if (target == TAP_OUTSIDE)
            tapOutsideMenu(app, touch.x, touch.y, now);
    } else if (touch.type == TOUCH_DRAG &&
               uiTapTrackMenu(app->menu_kind, m, touch.start_x, touch.start_y, &row) != TAP_OUTSIDE) {
        // Dragging scrolls a long list, as on a phone.
        app->drag_rows -= touch.dy;
        for (; app->drag_rows >= UI_MENU_ROW_H; app->drag_rows -= UI_MENU_ROW_H)
            menuMove(m, 1);
        for (; app->drag_rows <= -UI_MENU_ROW_H; app->drag_rows += UI_MENU_ROW_H)
            menuMove(m, -1);
    } else if (touch.type == TOUCH_DRAG_END) {
        app->drag_rows = 0;
    }
}

// Whether the next episode's card shows (in the last UP_NEXT_NS), and the time left.
static bool upNextShowing(const App *app, int64_t *left)
{
    if (app->next_item < 0 || !app->config.autoplay || app->next_dismissed || app->menu.open ||
        app->app_menu != APP_MENU_NONE)
        return false;
    int64_t duration = playerDuration(app->player), rest = duration - playerPosition(app->player);
    if (left)
        *left = rest > 0 ? rest : 0;
    return duration > UP_NEXT_NS && rest < UP_NEXT_NS;
}

// False when playback should end (back to the list, or on to the next episode).
static bool playerInput(App *app, uint64_t pressed, TouchEvent touch, uint64_t now)
{
    Player *p = app->player;
    if (app->app_menu != APP_MENU_NONE) {
        appMenuInput(app, pressed, touch, now);
        bool back = app->stop_requested;
        app->stop_requested = false;
        return !back;
    }
    if (app->menu.open) {
        menuInput(app, pressed, touch, now);
        return true;
    }
    if (upNextShowing(app, NULL)) {  // ZR or a tap on the card plays it now; its X closes it.
        LibHit hit = touch.type == TOUCH_TAP ? uiLibraryTap(touch.x, touch.y) : (LibHit){LIBHIT_NONE, 0, 0};
        if ((pressed & HidNpadButton_ZR) || hit.kind == LIBHIT_UP_NEXT) {
            app->play_next = true;
            return false;
        }
        if (hit.kind == LIBHIT_CLOSE) {
            app->next_dismissed = true;
            return true;
        }
    }
    if (appSkipInput(app, pressed, touch, upNextShowing(app, NULL), now))
        return true;
    if (app->hud_hidden && (touch.type == TOUCH_TAP || (pressed & HidNpadButton_Minus))) {
        app->hud_hidden = false;  // A tap brings the bar back, as - does.
        app->overlay_until = now + OVERLAY_NS;
        return true;
    }
    bool overlay = !app->hud_hidden && (playerPaused(p) || now < app->overlay_until);
    if (pressed || touch.type == TOUCH_TAP)
        app->overlay_until = now + OVERLAY_NS;
    if (pressed & HidNpadButton_Minus) {
        app->hud_hidden = true;
        app->scrubbing = false;
        return true;
    }
    if (pressed & HidNpadButton_B)
        return false;
    if (pressed & HidNpadButton_Plus) {
        appOpenMenu(app);
        return true;
    }
    if (pressed & HidNpadButton_A) {
        playerTogglePause(p, now);
        appLog("  A: %s at %.1f s", playerPaused(p) ? "pause" : "play", playerPosition(p) / 1e9);
    }
    if (pressed & BUTTONS_UP)
        openMenu(app, UI_MENU_PLAYBACK, now);
    if (pressed & BUTTONS_DOWN)
        app->stats_shown = !app->stats_shown;
    if (pressed & HidNpadButton_Y)
        openMenu(app, UI_MENU_SUBTITLES, now);
    if (pressed & HidNpadButton_X)
        openMenu(app, UI_MENU_AUDIO, now);
    if (appPlayingLive(app)) {  // Live TV: L and R change channel; what's on now is the only place, no jumps.
        if (pressed & (HidNpadButton_L | HidNpadButton_R)) {
            app->live_switch = (pressed & HidNpadButton_L) ? -1 : 1;
            return true;
        }
        pressed &= ~(uint64_t)(HidNpadButton_ZL | BUTTONS_LEFT | BUTTONS_RIGHT);
    }
    if (pressed & HidNpadButton_ZL) {  // From the start.
        playerSeekTo(p, 0, now);
        app->seeks++;
        app->bar_note[0] = '\0';
    }
    int64_t seek = (pressed & BUTTONS_LEFT)    ? -SEEK_SHORT_NS
                   : (pressed & BUTTONS_RIGHT) ? SEEK_SHORT_NS
                   : (pressed & HidNpadButton_L) ? -SEEK_LONG_NS
                   : (pressed & HidNpadButton_R) ? SEEK_LONG_NS
                                                 : 0;
    if (seek) {
        playerSeekBy(p, seek, now);
        app->seeks++;
    }
    // A finger on the progress bar drags along it; the video goes there when it lets go.
    double fraction = 0;
    if (touch.type == TOUCH_DRAG && overlay && !appPlayingLive(app) &&
        (app->scrubbing || uiTapPlayer(touch.start_x, touch.start_y, playerSubtitleCount(p) > 0,
                                       playerAudioTrackCount(p) > 1, &fraction) == TAP_SEEK)) {
        app->scrubbing = true;
        app->scrub_fraction = uiSeekFraction(touch.x);
        app->overlay_until = now + OVERLAY_NS;
    } else if (touch.type == TOUCH_DRAG_END && app->scrubbing) {
        app->scrubbing = false;
        int64_t target = (int64_t)(app->scrub_fraction * playerDuration(p));
        appLog("  seek to %.1f s (drag)", target / 1e9);
        playerSeekTo(p, target, now);
        app->seeks++;
    }
    if (touch.type == TOUCH_TAP && overlay) {  // A first tap only shows the bar.
        switch (uiTapPlayer(touch.x, touch.y, playerSubtitleCount(p) > 0, playerAudioTrackCount(p) > 1, &fraction)) {
        case TAP_CLOSE: return false;
        case TAP_SUBTITLES: openMenu(app, UI_MENU_SUBTITLES, now); break;
        case TAP_AUDIO: openMenu(app, UI_MENU_AUDIO, now); break;
        case TAP_PLAYBACK: openMenu(app, UI_MENU_PLAYBACK, now); break;
        case TAP_SEEK:
            if (appPlayingLive(app))
                break;
            appLog("  seek to %.1f s (tap)", fraction * playerDuration(p) / 1e9);
            playerSeekTo(p, (int64_t)(fraction * playerDuration(p)), now);
            app->seeks++;
            break;
        case TAP_PAUSE:
        case TAP_PICTURE:
            playerTogglePause(p, now);
            appLog("  tap: %s at %.1f s", playerPaused(p) ? "pause" : "play", playerPosition(p) / 1e9);
            break;
        default: break;
        }
    }
    return true;
}

// Logs the player's state once when it should be playing but its position
// hasn't moved for STALL_NS: what it waits for. True while it hasn't moved for
// WAITING_NS.
static bool watchForStall(App *app, uint64_t now)
{
    static int64_t last_pos = -1;
    static uint64_t moved_at;
    static bool logged;
    int64_t pos = playerPosition(app->player);
    if (playerPaused(app->player) || pos != last_pos) {
        last_pos = pos;
        moved_at = now;
        logged = false;
        return false;
    }
    if (!logged && now - moved_at > STALL_NS) {
        char state[256];
        playerDescribe(app->player, state, sizeof(state));
        appLog("  stuck for 2 s: %s", state);
        logged = true;
    }
    return now - moved_at > WAITING_NS;
}

// Subtitles with their own styles, over the bw x bh box the video is in; the
// lines at the bottom go up if the bar would cover them. False if the track
// shown isn't one of those.
static bool drawStyledSubtitles(App *app, float bw, float bh, bool bar)
{
    static unsigned drawn_id;
    float detail = gfxDetail();
    float covered = gfxToFb(uiSubtitleBarLift()) - (GFX_FB_HEIGHT - bh) / 2.0f;  // Black bars help.
    AssArea area = {
        .width = (int)(bw * detail + 0.5f),
        .height = (int)(bh * detail + 0.5f),
        .font_scale = settingSubtitleScale(&app->config),
        .lift_percent = bar && covered > 0 ? covered * 100.0 / bh : 0.0,
    };
    AssPicture picture;
    if (!playerSubtitlePicture(app->player, &area, &picture))
        return false;
    if (picture.w > 0) {
        if (picture.id != drawn_id) {
            gfxImageChanged(picture.pixels);
            drawn_id = picture.id;
        }
        gfxDrawImageScaledFb((GFX_FB_WIDTH - bw) / 2.0f + picture.x / detail,
                             (GFX_FB_HEIGHT - bh) / 2.0f + picture.y / detail, picture.w / detail, picture.h / detail,
                             picture.w, picture.h, picture.pixels);
    }
    return true;
}

static void drawPlayer(App *app, const GfxVideoFrame *frame, uint64_t now)
{
    bool stalled = watchForStall(app, now);
    uiLibraryResetHits();
    int w, h;
    double aspect;
    playerSize(app->player, &w, &h, &aspect);
    // The largest box with the video's shape that fits the screen, centered.
    float bw = GFX_FB_WIDTH, bh = (float)(GFX_FB_WIDTH / aspect);
    if (bh > GFX_FB_HEIGHT) {
        bh = GFX_FB_HEIGHT;
        bw = (float)(GFX_FB_HEIGHT * aspect);
    }
    gfxClear(GFX_RGB(0, 0, 0));
    if (frame)
        gfxDrawVideo(frame, (GFX_FB_WIDTH - bw) / 2.0f, (GFX_FB_HEIGHT - bh) / 2.0f, bw, bh);
    // Waiting for the picture (the start, a jump): the symbol, if it takes a moment.
    if (!playerWaitingForPicture(app->player))
        app->waiting_since = 0;
    else if (!app->waiting_since)
        app->waiting_since = now;
    bool waiting = app->waiting_since && now - app->waiting_since > SPINNER_DELAY_NS;
    appTrickplayBehind(app, waiting);
    if (waiting && appPlayingLive(app))
        uiDrawWaiting(spinnerAngle(now), "Tuning in...");  // Live TV waits once at the start, a moment behind the edge.
    else if (waiting)
        uiDrawSpinner(GFX_WIDTH / 2.0f, GFX_HEIGHT / 2.0f, spinnerAngle(now));
    else if (stalled && jfIsLocation(app->playing_path))
        uiDrawWaiting(spinnerAngle(now), "Waiting for the server...");
    char error[160];
    if (playerTakeAudioError(app->player, error, sizeof(error))) {  // A track that can't be played.
        snprintf(app->bar_note, sizeof(app->bar_note), "Sound: %s. %s",
                 playerAudioTrackName(app->player, playerAudioTrack(app->player)), error);
        app->bar_note_until = now + NOTE_NS;
        app->overlay_until = now + NOTE_NS;
        appLog("  sound track %d: %s", playerAudioTrack(app->player) + 1, error);
    }
    // Hidden with -, the bar shows only for an open list.
    bool bar = app->menu.open || (!app->hud_hidden && (playerPaused(app->player) || now < app->overlay_until));
    if (!drawStyledSubtitles(app, bw, bh, bar)) {
        char subtitle[1024];
        if (playerSubtitleText(app->player, subtitle, sizeof(subtitle)))
            uiDrawSubtitles(subtitle, bar);
    }
    if (playerPaused(app->player) && !app->menu.open && !app->hud_hidden)
        uiDrawPausedSymbol();
    if (bar)
        uiDrawPlayerStatus();
    if (bar)
        uiDrawPlayerBar(app->player, app->playing_name,
                        now < app->bar_note_until && !app->menu.open ? app->bar_note : NULL,
                        app->scrubbing ? app->scrub_fraction : -1);
    int64_t left;
    if (upNextShowing(app, &left))
        uiDrawUpNextCard(&app->library, app->next_item, (double)left / UP_NEXT_NS,
                         (int)((left + 999999999) / 1000000000), bar);
    appTrickplayAboveBar(app, bar);
    appSkipDraw(app, bar, upNextShowing(app, NULL));
    appStatsUpdate(app, now);
    appStatsDraw(app);
    uiDrawTrackMenu(app->player, app->menu_kind, &app->menu, &app->config);
    if (app->app_menu != APP_MENU_NONE) {  // The + menu over the video.
        uiLibraryResetHits();
        appMenuDraw(app);
        uiDrawFooterHelp(appFooterHelp(app));
    }
}

// While a video plays the console doesn't dim its screen or go to sleep, as
// when KaiPlayer plays. Paused or in the library, it does as usual.
static void updateScreenKeeping(const App *app)
{
    static bool kept;
    bool keep = app->opening || (app->player && !playerPaused(app->player));
    if (keep != kept) {
        appletSetMediaPlaybackState(keep);
        kept = keep;
    }
}

int main(int argc, char **argv)
{
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    hidInitializeTouchScreen();
    // Files next to the NRO; the first time, what the old folder had is copied there.
    bool next_to_nro = appRootFolder(argc > 0 ? argv[0] : NULL, "kaivideo", g_folder, sizeof(g_folder));
    appRootMakeFolder(g_folder);
    int moved = next_to_nro ? appRootMigrate(OLD_CONFIG_FOLDER, g_folder) : 0;
    char path[APPROOT_PATH_MAX + 32];
    appConfigPath(path, sizeof(path), "kaivideo.log");
    g_log = fopen(path, "w");
    appLog("Files in %s%s", g_folder, moved ? " (copied from " OLD_CONFIG_FOLDER ")" : "");
    if (!gfxInit() || !fontInit()) {
        appLog("The GPU or the fonts couldn't start.");
        if (g_log)
            fclose(g_log);
        return 1;
    }
    static AssFont subtitle_fonts[2];
    subtitle_fonts[0] = (AssFont){"font_regular.ttf", font_regular_ttf, font_regular_ttf_size};
    subtitle_fonts[1] = (AssFont){"font_bold.ttf", font_bold_ttf, font_bold_ttf_size};
    videoSetSubtitleFonts(subtitle_fonts, 2);
    if (!audioSystemInit())
        appLog("The audio output couldn't start: videos play silent.");

    static App app;
    app.speed_index = PLAYBACK_SPEED_NORMAL;
    char folder[BROWSER_PATH_MAX];
    loadSettings(&app, folder, sizeof(folder));
    appConfigPath(path, sizeof(path), "resume.txt");
    resumeLoad(&app.resume, path);
    appConfigPath(path, sizeof(path), "watched.txt");
    watchedLoad(&app.watched, path);
    appConfigPath(path, sizeof(path), "tracks.txt");
    trackPrefsLoad(&app.tracks, path);
    if (!browserOpen(&app.browser, folder))
        browserOpen(&app.browser, BROWSER_ROOT);  // The folder kept is gone.
    appConfigPath(path, sizeof(path), "thumbs");
    thumbsInit(path);
    sysinfoInit();
    appSettingsStart(&app);
    appLibraryStart(&app);
    InputRepeat repeat;
    inputRepeatInit(&repeat, BUTTONS_REPEATING);
    TouchTracker touch;
    touchReset(&touch);

    while (!app.quit && appletMainLoop()) {
        padUpdate(&pad);
        uint64_t now = nowNs();
        uint64_t pressed = inputRepeatUpdate(&repeat, padGetButtonsDown(&pad), padGetButtons(&pad), now);
        TouchEvent t = readTouch(&touch, now);
        appLibraryUpdate(&app);
        appJellyfinUpdate(&app, now);
        appLiveUpdate(&app, now);
        updateScreenKeeping(&app);
        thumbsUpdate();
        SysInfo sys;
        sysinfoGet(&sys, now);
        uiLibrarySetStatus(&sys, app.config.clock_24h);
        gfxBegin();
        if (app.player && !app.opening && app.live_switch)
            switchChannel(&app, now);
        if (app.player && !app.opening)
            reopenIfAsked(&app, now);
        int start_ms, cushion_ms;
        if (app.player && !app.opening && playerTakeStartTimes(app.player, &start_ms, &cushion_ms))
            appLog("  playing after %d ms (%d ms gathering video)", start_ms, cushion_ms);
        if (app.opening) {
            if (!app.open_dismissed) {
                if (pressed & HidNpadButton_B) {
                    // B: back to the library at once; the opening ends out of sight
                    // (a server or a name lookup may take a moment to let go).
                    app.live_pending = 0;
                    app.live_pending_set = false;
                    app.open_dismissed = true;
                    if (!app.open_cancelled) {
                        app.open_cancelled = true;
                        mediaFileInterruptAll();
                    }
                } else {
                    switchWhileOpening(&app, pressed);
                }
                pressed = 0;  // Not for the video that's about to show.
            } else if (app.queued) {
                if (pressed & HidNpadButton_B)
                    app.queued = false;  // The one waiting to open isn't wanted either.
                pressed = 0;
            }
            if (app.open_done)
                finishOpening(&app, now);
            if (!app.opening && app.live_pending_set)
                openPendingChannel(&app, now);
            else if (!app.opening && app.queued)
                openQueued(&app, now);
        }
        if (app.opening && app.open_dismissed && app.queued) {
            uiDrawLoading(app.queued_name, spinnerAngle(now), false);  // Waiting for the last one to let go.
            uiDrawFooterHelp("[B] cancel");
        } else if (app.opening && !app.open_dismissed) {
            bool switching = app.live_pending_set;
            uiDrawLoading(switching ? app.live_pending_name : app.playing_name, spinnerAngle(now),
                          app.open_cancelled && !switching);
            uiDrawFooterHelp(liveIsLocation(app.playing_path) ? "[L][R] channel  [B] cancel" : "[B] cancel");
        } else if (app.player) {
            char error[160] = "";
            bool keep = playerInput(&app, pressed, t, now);
            bool ended = false;
            if (keep && playerFinished(app.player, error, sizeof(error))) {
                keep = false;
                ended = !error[0] && reachedEnd(app.player);
            }
            if (!keep) {
                // On to the next episode: asked for, or at the end unless its card was closed.
                int next = app.next_item;
                bool play_next = app.play_next || (ended && app.config.autoplay && !app.next_dismissed);
                stopVideo(&app, error, ended);
                if (error[0])
                    appShowMessage(&app, error, now);
                else if (play_next && next >= 0)
                    appLibraryPlay(&app, next, now);
                appLibraryDraw(&app, now);
            } else {
                appJellyfinPlaybackTick(&app, now);
                drawPlayer(&app, playerUpdate(app.player, now), now);
            }
        } else {
            appLibraryInput(&app, pressed, t, now);
            if (app.player)
                drawPlayer(&app, playerUpdate(app.player, now), now);
            else
                appLibraryDraw(&app, now);
        }
        gfxEnd();
    }
    if (app.opening) {
        app.open_cancelled = true;
        mediaFileInterruptAll();
        finishOpening(&app, nowNs());
    }
    if (app.player)
        stopVideo(&app, NULL, false);
    appletSetMediaPlaybackState(false);
    appSaveSettings(&app);
    appLiveExit(&app);
    appLibraryExit(&app);
    watchedFree(&app.watched);
    trackPrefsFree(&app.tracks);
    thumbsExit();
    jfSyncStop();
    sysinfoExit();
    browserFree(&app.browser);
    appLog("Done.");
    audioSystemExit();
    fontExit();
    gfxExit();
    if (g_log)
        fclose(g_log);
    return 0;
}
