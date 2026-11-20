// KaiVideo's side of Jellyfin: starting the connection, taking the library
// when it arrives, and telling the server what's being watched.
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "jfprobe.h"

#ifndef KAIVIDEO_VERSION
#define KAIVIDEO_VERSION "dev"
#endif

#define REFRESH_NS (5ull * 60 * 1000000000ull)  // While idle: what other devices did.
#define REPORT_NS (10ull * 1000000000ull)
#define FILE_NAME_MAX 600

bool appJellyfinEnsure(App *app)
{
    if (jfSyncRunning())
        return true;
    static char paths[6][FILE_NAME_MAX];
    static const char *const NAMES[6] = {"jellyfin.txt", "jellyfin_library.txt", "emby.txt",
                                         "emby_library.txt", "plex.txt", "plex_library.txt"};
    for (int i = 0; i < 6; i++)
        appConfigPath(paths[i], sizeof(paths[i]), NAMES[i]);
    JfSyncFiles files = {paths[0], paths[1], paths[2], paths[3], paths[4], paths[5]};
    jfProbeSetKind((ServerKind)app->config.server_kind);
    return jfSyncStart(&files, KAIVIDEO_VERSION, app->config.use_jellyfin, (ServerKind)app->config.server_kind);
}

static void finishSignIn(App *app, uint64_t now)
{
    if (app->jf.state == JF_SIGN_IN_STARTING || app->jf.state == JF_SIGN_IN_WAITING)
        return;
    app->jf_signing = false;
    if (app->jf.state != JF_SIGNED_IN)
        return;
    char message[JF_SERVER_MAX + JF_NAME_MAX + 64];
    const char *where = app->config.server_kind == SERVER_PLEX ? app->jf.server
                                                               : serverKindName((ServerKind)app->config.server_kind);
    snprintf(message, sizeof(message), "Connected to %s as %s. Loading its library...", where, app->jf.user);
    app->jf_arriving = app->config.server_kind + 1;
    app->settings_open = false;
    appLibraryShowServer(app, (ServerKind)app->config.server_kind);  // The one just signed in.
    appShowMessage(app, message, now);
}

static void sayNewError(App *app, uint64_t now)
{
    if (!strcmp(app->jf.error, app->jf_last_error))
        return;
    snprintf(app->jf_last_error, sizeof(app->jf_last_error), "%s", app->jf.error);
    if (app->jf.error[0])
        appShowMessage(app, app->jf.error, now);
}

// A test or search from Settings finished: say what it found, and use a server found.
static void takeProbe(App *app, uint64_t now)
{
    JfProbeResult r;
    if (!jfProbeTake(&r))
        return;
    appLog("Jellyfin %s: %s%s%s", r.finding ? "search" : "test", r.message, r.detail[0] ? " | " : "", r.detail);
    if (r.finding && r.ok && app->jf.state != JF_SIGNED_IN && !jfSyncSetServer(r.found))
        snprintf(r.message, sizeof(r.message), "The server found can't be used: type its address under Server.");
    if (!r.finding && r.ok)
        appLiveReadSoon(app);  // A test that worked also looks for Live TV channels again.
    appShowMessage(app, r.message, now);
}

void appJellyfinUpdate(App *app, uint64_t now)
{
    takeProbe(app, now);
    if (!jfSyncRunning())
        return;
    jfSyncGetStatus(&app->jf);
    if (app->jf_signing)
        finishSignIn(app, now);
    sayNewError(app, now);
    for (int k = 0; k < SERVER_KIND_COUNT; k++)  // A server signed out: its library goes.
        if (app->server_has[k] && !jfSyncSignedIn((ServerKind)k) && !app->jf_signing)
            appLibraryServerCatalog(app, (ServerKind)k, NULL);
    if (app->config.use_jellyfin && !jfSyncAnySignedIn() && !app->jf_signing) {  // No server left: back to the card.
        appLibrarySetSource(app, false);
        return;
    }
    JfCatalog catalog;
    bool took = false;
    for (int k = 0; k < SERVER_KIND_COUNT; k++)
        if (jfSyncTakeCatalog((ServerKind)k, &catalog)) {
            appLibraryServerCatalog(app, (ServerKind)k, &catalog);  // Kept: the app frees it.
            took = true;
        }
    if (took) {
        app->jf_last_refresh = now;
        if (app->jf_arriving && !app->jf.refreshing) {  // The server just signed in: its videos are here.
            char message[96];
            snprintf(message, sizeof(message), "%s's videos are in the library.",
                     serverKindName((ServerKind)(app->jf_arriving - 1)));
            appShowMessage(app, message, now);
            app->jf_arriving = 0;
        }
    }
    bool idle = !app->player && !app->opening && !app->jf.refreshing && jfSyncAnySignedIn();
    if (idle && app->config.use_jellyfin && now - app->jf_last_refresh > REFRESH_NS) {
        app->jf_last_refresh = now;
        jfSyncRefresh();
    }
}

// Live TV is reported too, so the server shows the channel watched; it has
// no place to resume, so its position says nothing and is sent as 0.
static int64_t reportedPosition(const App *app, int64_t position_ns)
{
    return liveIsLocation(app->playing_path) ? 0 : position_ns;
}

void appJellyfinPlaybackStarted(App *app, uint64_t now)
{
    if (!jfIsRemoteLocation(app->playing_path))
        return;
    app->jf_last_report = now;
    app->jf_reported_paused = false;
    jfSyncReport(JF_REPORT_START, app->playing_path, reportedPosition(app, playerPosition(app->player)), false);
}

// Every so often, and at once when it's paused or goes on again.
void appJellyfinPlaybackTick(App *app, uint64_t now)
{
    if (!jfIsRemoteLocation(app->playing_path))
        return;
    bool paused = playerPaused(app->player);
    if (paused == app->jf_reported_paused && now - app->jf_last_report < REPORT_NS)
        return;
    app->jf_last_report = now;
    app->jf_reported_paused = paused;
    jfSyncReport(JF_REPORT_PROGRESS, app->playing_path, reportedPosition(app, playerPosition(app->player)), paused);
}

void appJellyfinPlaybackStopped(App *app, int64_t position_ns, bool watched)
{
    if (!jfIsRemoteLocation(app->playing_path))
        return;
    jfSyncReport(JF_REPORT_STOP, app->playing_path, reportedPosition(app, position_ns), false);
    if (watched && !liveIsLocation(app->playing_path))
        jfSyncSetPlayed(app->playing_path, true);
}
