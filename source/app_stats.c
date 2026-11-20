// The statistics panel over a playing video: sampled while it plays, drawn on ↓.
#include "app.h"
#include "hlsio.h"
#include "jfsync.h"
#include "mediafile.h"
#include "net.h"
#include "ui_stats.h"

void appStatsUpdate(App *app, uint64_t now)
{
    if (!app->player || now - app->stats_sampled < STATS_EVERY_NS)
        return;
    app->stats_sampled = now;
    VideoStats v = playerStats(app->player);
    statsAdd(&app->stats, (StatsSample){.at_ns = now, .shown = v.shown, .dropped = v.dropped,
                                        .bytes = netBytesReceived()});
}

void appStatsDraw(const App *app)
{
    if (!app->stats_shown || !app->player)
        return;
    const Player *p = app->player;
    char quality[32];
    settingValue(&app->config, SETTING_JF_QUALITY, quality, sizeof(quality));
    bool converted = playerIsConverted(p);
    StatsInfo info = {
        .source = converted ? STATS_CONVERTED : jfIsRemoteLocation(app->playing_path) ? STATS_DIRECT : STATS_CARD,
        .quality = quality,
        .video = playerVideoInfo(p),
        .sound = playerAudioInfo(p),
        .rates = statsRates(&app->stats),
        .ahead_s = converted ? hlsSecondsReady(playerPosition(p)) : -1,
        .ahead_mb = converted ? -1 : mediaFileMegabytesReady(),
        .speed_permille = playerSpeed(p),
    };
    char lines[STATS_LINES][STATS_LINE_MAX];
    uiDrawStats(lines, statsFormat(&info, lines));
}
