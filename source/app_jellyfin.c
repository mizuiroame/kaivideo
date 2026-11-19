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
    char session[FILE_NAME_MAX], catalog[FILE_NAME_MAX];
    appConfigPath(session, sizeof(session), "jellyfin.txt");
    appConfigPath(catalog, sizeof(catalog), "jellyfin_library.txt");
    return jfSyncStart(session, catalog, KAIVIDEO_VERSION, app->config.use_jellyfin);
}

static void finishSignIn(App *app, uint64_t now)
{
    if (app->jf.state == JF_SIGN_IN_STARTING || app->jf.state == JF_SIGN_IN_WAITING)
        return;
    app->jf_signing = false;
    if (app->jf.state != JF_SIGNED_IN)
        return;
    char message[160];
    snprintf(message, sizeof(message), "Connected to Jellyfin as %s.", app->jf.user);
    app->settings_open = false;
    appLibrarySetSource(app, true);
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
    if (app->config.use_jellyfin && app->jf.state == JF_SIGNED_OUT) {  // No session: back to the card.
        appLibrarySetSource(app, false);
        return;
    }
    JfCatalog catalog;
    if (jfSyncTakeCatalog(&catalog)) {
        if (app->config.use_jellyfin)
            appLibraryInstallCatalog(app, &catalog);
        jfCatalogFree(&catalog);
        app->jf_last_refresh = now;
    }
    bool idle = !app->player && !app->opening && !app->jf.refreshing && app->jf.state == JF_SIGNED_IN;
    if (idle && app->config.use_jellyfin && now - app->jf_last_refresh > REFRESH_NS) {
        app->jf_last_refresh = now;
        jfSyncRefresh();
    }
}

void appJellyfinPlaybackStarted(App *app, uint64_t now)
{
    if (!jfIsLocation(app->playing_path) || liveIsLocation(app->playing_path))
        return;
    app->jf_last_report = now;
    jfSyncReport(JF_REPORT_START, app->playing_path, playerPosition(app->player), false);
}

void appJellyfinPlaybackTick(App *app, uint64_t now)
{
    if (!jfIsLocation(app->playing_path) || liveIsLocation(app->playing_path) || now - app->jf_last_report < REPORT_NS)
        return;
    app->jf_last_report = now;
    jfSyncReport(JF_REPORT_PROGRESS, app->playing_path, playerPosition(app->player), playerPaused(app->player));
}

void appJellyfinPlaybackStopped(App *app, int64_t position_ns, bool watched)
{
    if (!jfIsLocation(app->playing_path) || liveIsLocation(app->playing_path))
        return;
    jfSyncReport(JF_REPORT_STOP, app->playing_path, position_ns, false);
    if (watched)
        jfSyncSetPlayed(app->playing_path, true);
}
