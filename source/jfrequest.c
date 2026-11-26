// What the player and the pictures ask a server from any thread: how to play
// a video, its file, its conversions, subtitles, pictures, parts to skip and
// Live TV's guide. Each goes to the server its location names (jellyfin:,
// emby: or plex:), with that server's session.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "jfsync.h"
#include "jfsync_internal.h"
#include "lang.h"
#include "livetv.h"
#include "net.h"
#include "plexsync.h"

#define API_TIMEOUT_S 30L
#define PLAN_MAX_BYTES (4u * 1024 * 1024)
#define PLAN_BODY_MAX 4096
#define SUBTITLE_MAX_BYTES (16u * 1024 * 1024)
#define PAGE_MAX_BYTES (32u * 1024 * 1024)
#define SMALL_MAX_BYTES (256u * 1024)
#define LIVE_NOW_CHANNELS_S 60  // What's on now, when the guide's hours are later: programs on in this minute.
#define SEGMENTS_TIMEOUT_S 5L    // Parts to skip are a nicety: opening doesn't wait long for them.
#define IMAGE_QUALITY 85
#define IMAGE_FORMAT "Jpg"  // What stb_image reads, whatever the server would prefer.

// The session of the Jellyfin or Emby server a location is on.
static JfSession sessionOf(const char *location)
{
    return jfSyncSessionFor(serverKindOf(location));
}

static bool plexResolve(const char *location, long long max_bitrate, int audio_index, int subtitle_index,
                        JfPlan *out, char *error, size_t error_size, const volatile bool *cancel)
{
    NetClient *net = netClientOpen();
    PlexSession s = jfSyncPlexSession();
    int64_t duration = 0;
    bool ok = net && plexNetResolve(net, &s, jfSyncVersionText(), location, max_bitrate, audio_index, subtitle_index,
                                    out, &duration, error, error_size, cancel);
    netClientClose(net);
    char item[PLEX_ID_MAX], part[PLEX_ID_MAX];
    if (ok && plexLocationIds(location, item, part))
        jfSyncNotePlexDuration(item, duration);
    return ok;
}

bool jfSyncResolve(const char *location, long long max_bitrate, int audio_index, int subtitle_index, JfPlan *out,
                   char *error, size_t error_size, const volatile bool *cancel)
{
    if (jfSyncIsUp() && plexIsLocation(location))
        return plexResolve(location, max_bitrate, audio_index, subtitle_index, out, error, error_size, cancel);
    char item[JF_ID_MAX], source[JF_ID_MAX], url[JF_URL_MAX], auth[JF_AUTH_MAX];
    char *body = malloc(PLAN_BODY_MAX);
    NetClient *net = netClientOpen();
    bool ok = false;
    snprintf(error, error_size, "%s", T("The server can't be reached."));
    if (jfSyncIsUp() && body && net && jfLocationIds(location, item, source)) {
        JfSession s = sessionOf(location);
        bool live = !strcmp(source, LIVE_SOURCE);  // A channel: the server opens its stream.
        snprintf(url, sizeof(url), "%s/Items/%s/PlaybackInfo?userId=%s%s%s", s.server, item, s.user_id,
                 live ? "" : "&mediaSourceId=", live ? "" : source);
        jfAuthHeader(&s, jfSyncVersionText(), auth, sizeof(auth));
        jfPlaybackBody(body, PLAN_BODY_MAX, s.user_id, max_bitrate, audio_index, subtitle_index);
        NetRequest request = {.url = url, .authorization = auth, .post_json = body, .timeout_s = API_TIMEOUT_S,
                              .cancel = cancel};
        NetResponse r;
        if (netFetchText(net, &request, PLAN_MAX_BYTES, &r)) {
            ok = jfPlanParse(r.data, out);
            netResponseFree(&r);
            if (!ok)
                snprintf(error, error_size, "%s", T("The server can't play this video here."));
        }
    }
    netClientClose(net);
    free(body);
    return ok;
}

bool jfSyncLiveSource(const char *location, JfPlan *out, const volatile bool *cancel)
{
    char item[JF_ID_MAX], source[JF_ID_MAX], url[JF_URL_MAX], auth[JF_AUTH_MAX], body[256];
    NetClient *net = netClientOpen();
    bool ok = false;
    if (jfSyncIsUp() && net && jfLocationIds(location, item, source) && !strcmp(source, LIVE_SOURCE)) {
        JfSession s = sessionOf(location);
        snprintf(url, sizeof(url), "%s/Items/%s/PlaybackInfo?userId=%s", s.server, item, s.user_id);
        jfAuthHeader(&s, jfSyncVersionText(), auth, sizeof(auth));
        jfLiveInfoBody(body, sizeof(body), s.user_id);
        NetRequest request = {.url = url, .authorization = auth, .post_json = body, .timeout_s = API_TIMEOUT_S,
                              .cancel = cancel};
        NetResponse r;
        if (netFetchText(net, &request, PLAN_MAX_BYTES, &r)) {
            ok = jfLivePlanParse(r.data, out);
            netResponseFree(&r);
        }
    }
    netClientClose(net);
    return ok;
}

// The answer to an API path of a session's server, with a client of the caller's own.
static bool fetchWith(NetClient *net, const JfSession *s, const char *path, size_t max, NetResponse *out)
{
    char url[JF_URL_MAX + 256], auth[JF_AUTH_MAX];
    snprintf(url, sizeof(url), "%s%s", s->server, path);
    jfAuthHeader(s, jfSyncVersionText(), auth, sizeof(auth));
    NetRequest request = {.url = url, .authorization = auth, .timeout_s = API_TIMEOUT_S};
    return netFetchText(net, &request, max, out);
}

static void isoTime(int64_t s, char *out, size_t size)
{
    time_t t = (time_t)s;
    struct tm tm;
    gmtime_r(&t, &tm);
    strftime(out, size, "%Y-%m-%dT%H:%M:%SZ", &tm);
}

// Programs from `from` to `to`, for the guide (or only what's on now).
static bool fetchPrograms(NetClient *net, const JfSession *s, int64_t from, int64_t to, NetResponse *r)
{
    char path[512], min_end[32], max_start[32];
    isoTime(from, min_end, sizeof(min_end));
    isoTime(to, max_start, sizeof(max_start));
    snprintf(path, sizeof(path),
             "/LiveTv/Programs?UserId=%s&MinEndDate=%s&MaxStartDate=%s&EnableImages=false&SortBy=StartDate&Limit=%d",
             s->user_id, min_end, max_start, LIVE_PROGRAMS_MAX);
    return fetchWith(net, s, path, PAGE_MAX_BYTES, r);
}

int jfSyncFetchLive(LiveGuide *out, int64_t now_s, int64_t from, int64_t to)
{
    JfSession s = jfSyncSessionFor(SERVER_JELLYFIN);  // Live TV comes from Jellyfin.
    if (!jfSyncIsUp() || !s.token[0])
        return -1;
    NetClient *net = netClientOpen();
    char path[256];
    NetResponse r;
    int count = -1;
    snprintf(path, sizeof(path), "/LiveTv/Channels?userId=%s&EnableImages=false&Limit=%d", s.user_id, LIVE_CHANNELS_MAX);
    if (net && fetchWith(net, &s, path, PAGE_MAX_BYTES, &r)) {
        count = liveGuideSetChannels(out, r.data);
        netResponseFree(&r);
    }
    if (count > 0 && fetchPrograms(net, &s, from, to, &r)) {
        liveGuideSetPrograms(out, r.data, now_s, from, to);
        netResponseFree(&r);
        // The hours shown are later: what's on now still names the channel in the player.
        if (from > now_s && fetchPrograms(net, &s, now_s, now_s + LIVE_NOW_CHANNELS_S, &r)) {
            liveGuideSetNow(out, r.data, now_s);
            netResponseFree(&r);
        }
        DvrTimer *timers = malloc(DVR_TIMERS_MAX * sizeof(DvrTimer));
        int marked = timers ? jfSyncDvrTimers(net, timers, DVR_TIMERS_MAX) : -1;
        if (marked >= 0)  // Programs say it too, but timers made a moment ago may not be in them yet.
            dvrMarkGuide(out, timers, marked);
        free(timers);
    }
    netClientClose(net);
    return count;
}

int jfSyncFetchSegments(const char *location, JfSegment *out, int max)
{
    if (plexIsLocation(location)) {
        NetClient *net = jfSyncIsUp() ? netClientOpen() : NULL;
        PlexSession s = jfSyncPlexSession();
        int count = net ? plexNetFetchMarkers(net, &s, jfSyncVersionText(), location, out, max) : 0;
        netClientClose(net);
        return count;
    }
    char item[JF_ID_MAX], source[JF_ID_MAX], url[JF_URL_MAX], auth[JF_AUTH_MAX];
    NetClient *net = netClientOpen();
    int count = 0;
    if (jfSyncIsUp() && net && jfLocationIds(location, item, source)) {
        JfSession s = sessionOf(location);
        snprintf(url, sizeof(url), "%s/MediaSegments/%s", s.server, item);
        jfAuthHeader(&s, jfSyncVersionText(), auth, sizeof(auth));
        NetRequest request = {.url = url, .authorization = auth, .timeout_s = SEGMENTS_TIMEOUT_S};
        NetResponse r;
        if (netFetchText(net, &request, SMALL_MAX_BYTES, &r)) {
            count = jfSegmentsParse(r.data, out, max);
            netResponseFree(&r);
        }
    }
    netClientClose(net);
    return count;
}

bool jfSyncAudioConversion(const char *location, int audio_index, char *url, size_t url_size, char *authorization,
                           size_t auth_size, char *play_session, size_t session_size)
{
    if (plexIsLocation(location)) {
        NetClient *net = jfSyncIsUp() ? netClientOpen() : NULL;
        PlexSession s = jfSyncPlexSession();
        bool ok = net && plexNetAudioConversion(net, &s, jfSyncVersionText(), location, audio_index, url, url_size,
                                                play_session, session_size);
        netClientClose(net);
        if (auth_size)
            authorization[0] = '\0';
        return ok;
    }
    char item[JF_ID_MAX], source[JF_ID_MAX];
    if (!jfLocationIds(location, item, source) || !strcmp(source, LIVE_SOURCE))
        return false;
    JfSession s = sessionOf(location);
    // Its own play session, so it can be ended without ending the video's.
    snprintf(play_session, session_size, "%08x%08x%08x%08x", (unsigned)rand(), (unsigned)rand(), (unsigned)time(NULL),
             (unsigned)audio_index);
    // The server's /Audio conversion always takes the default track: a video
    // conversion takes the one asked for. Its picture is made tiny (the
    // smallest its hardware encoder takes well): what comes is nearly all sound.
    int n = snprintf(url, url_size,
                     "%s/Videos/%s/main.m3u8?MediaSourceId=%s&DeviceId=%s&PlaySessionId=%s&AudioStreamIndex=%d"
                     "&AudioCodec=aac&MaxAudioChannels=2&AudioBitrate=192000&AudioSampleRate=48000"
                     "&VideoCodec=h264&MaxWidth=320&MaxHeight=180&VideoBitrate=150000&SubtitleStreamIndex=-1"
                     "&SegmentContainer=ts&BreakOnNonKeyFrames=True",
                     s.server, item, source, s.device_id, play_session, audio_index);
    jfAuthHeader(&s, jfSyncVersionText(), authorization, auth_size);
    return n > 0 && (size_t)n < url_size;
}

void jfSyncConvertedRequest(const char *location, const char *url, char *full, size_t full_size, char *authorization,
                            size_t auth_size)
{
    if (plexIsLocation(location)) {  // The token goes in the address; the conversion's own lists and pieces need none.
        PlexSession ps = jfSyncPlexSession();
        plexServerUrl(&ps, jfSyncVersionText(), url, full, full_size);
        if (auth_size)
            authorization[0] = '\0';
        return;
    }
    JfSession s = sessionOf(location);
    snprintf(full, full_size, "%s%s", s.server, url);
    jfAuthHeader(&s, jfSyncVersionText(), authorization, auth_size);
}

bool jfSyncFetchSubtitle(const char *location, int stream_index, long wait_s, const volatile bool *cancel,
                         NetResponse *out)
{
    memset(out, 0, sizeof(*out));
    if (plexIsLocation(location)) {
        NetClient *net = jfSyncIsUp() ? netClientOpen() : NULL;
        PlexSession s = jfSyncPlexSession();
        bool ok = net && plexNetFetchSubtitle(net, &s, jfSyncVersionText(), stream_index, cancel, out);
        netClientClose(net);
        return ok;
    }
    char item[JF_ID_MAX], source[JF_ID_MAX], url[JF_URL_MAX], auth[JF_AUTH_MAX];
    NetClient *net = jfSyncIsUp() && jfLocationIds(location, item, source) ? netClientOpen() : NULL;
    if (!net)
        return false;
    JfSession s = sessionOf(location);
    snprintf(url, sizeof(url), "%s/Videos/%s/%s/Subtitles/%d/0/Stream.srt", s.server, item, source, stream_index);
    jfAuthHeader(&s, jfSyncVersionText(), auth, sizeof(auth));
    // Nothing comes while the server reads the subtitle out of the file.
    NetRequest request = {.url = url, .authorization = auth, .cancel = cancel, .timeout_s = wait_s, .quiet_s = wait_s};
    bool ok = netFetchText(net, &request, SUBTITLE_MAX_BYTES, out);
    netClientClose(net);
    return ok;
}

bool jfSyncStreamRequest(const char *location, char *url, size_t url_size, char *authorization, size_t auth_size)
{
    if (plexIsLocation(location)) {
        PlexSession s = jfSyncPlexSession();
        if (auth_size)
            authorization[0] = '\0';
        return jfSyncIsUp() && plexStreamUrl(&s, jfSyncVersionText(), location, url, url_size);
    }
    char item[JF_ID_MAX], source[JF_ID_MAX];
    if (!jfSyncIsUp() || !jfLocationIds(location, item, source))
        return false;
    JfSession s = sessionOf(location);
    snprintf(url, url_size, "%s/Videos/%s/stream?static=true&mediaSourceId=%s", s.server, item, source);
    jfAuthHeader(&s, jfSyncVersionText(), authorization, auth_size);
    return s.token[0] != '\0';
}

bool jfSyncTrickplayRequest(const char *location, int width, int sheet, char *url, size_t url_size,
                            char *authorization, size_t auth_size)
{
    char item[JF_ID_MAX], source[JF_ID_MAX];
    if (!jfSyncIsUp() || !jfLocationIds(location, item, source))
        return false;
    JfSession s = sessionOf(location);
    if (width > 0)
        snprintf(url, url_size, "%s/Videos/%s/Trickplay/%d/%d.jpg?mediaSourceId=%s", s.server, item, width, sheet, source);
    else
        snprintf(url, url_size, "%s/Items?userId=%s&ids=%s&Fields=Trickplay&EnableImages=false", s.server, s.user_id, item);
    jfAuthHeader(&s, jfSyncVersionText(), authorization, auth_size);
    return s.token[0] != '\0';
}

bool jfSyncImageRequest(const char *location, const char *kind, int max_width, char *url, size_t url_size,
                        char *authorization, size_t auth_size)
{
    if (plexIsLocation(location)) {
        PlexSession s = jfSyncPlexSession();
        if (auth_size)
            authorization[0] = '\0';
        return jfSyncIsUp() && plexImageUrl(&s, jfSyncVersionText(), location, kind, max_width, url, url_size);
    }
    char item[JF_ID_MAX], source[JF_ID_MAX];
    if (!jfSyncIsUp() || !jfLocationIds(location, item, source))
        return false;
    JfSession s = sessionOf(location);
    snprintf(url, url_size, "%s/Items/%s/Images/%s?maxWidth=%d&quality=%d&format=" IMAGE_FORMAT, s.server, item, kind,
             max_width, IMAGE_QUALITY);
    jfAuthHeader(&s, jfSyncVersionText(), authorization, auth_size);
    return s.token[0] != '\0';
}
