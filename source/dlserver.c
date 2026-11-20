// Downloads from the media servers (Jellyfin, Emby, Plex): the original file,
// read in one stream from where the card's part ends, and its subtitle file.
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dlworker.h"
#include "hlslist.h"
#include "jfsync.h"

#define STALL_S 30L
#define CONVERSION_STALL_S 120L  // A part may wait while the server converts up to it.
#define LIST_MAX_BYTES (8u * 1024 * 1024)
#define SRT_EXT ".srt"
#define HTTP_UNAUTHORIZED 401
#define HTTP_FORBIDDEN 403
#define HTTP_NOT_FOUND 404

static bool handles(const char *origin)
{
    return jfIsRemoteLocation(origin);
}

static void sayStatus(long status, const char *error, char *out, size_t size)
{
    if (status == HTTP_UNAUTHORIZED || status == HTTP_FORBIDDEN)
        snprintf(out, size, "The server didn't let it be downloaded.");
    else if (status == HTTP_NOT_FOUND)
        snprintf(out, size, "The server doesn't have this video any more.");
    else if (status)
        snprintf(out, size, "The server answered %ld.", status);
    else
        snprintf(out, size, "%s", error[0] ? error : "The server can't be reached.");
}

static bool fetch(const char *origin, int64_t offset, NetSink sink, void *user, long long *total,
                  const volatile bool *cancel, char *error, size_t error_size)
{
    char url[JF_URL_MAX], auth[JF_AUTH_MAX], range[48], why[NET_ERROR_MAX] = "";
    if (!jfSyncStreamRequest(origin, url, sizeof(url), auth, sizeof(auth))) {
        snprintf(error, error_size, "Sign in to its server to download it.");
        return false;
    }
    NetClient *net = netClientOpen();
    if (!net) {
        snprintf(error, error_size, "The network can't be used.");
        return false;
    }
    snprintf(range, sizeof(range), "%" PRId64 "-", offset);
    NetRequest request = {.url = url, .authorization = auth[0] ? auth : NULL, .range = range,
                          .total_size = total, .cancel = cancel, .stall_s = STALL_S};
    long status = 0;
    bool ok = netFetch(net, &request, sink, user, &status, why, sizeof(why));
    netClientClose(net);
    if (!ok)
        sayStatus(status, why, error, error_size);
    return ok;
}

// The video's name without its extension, then ".srt" (what the player looks for).
static bool srtPathFor(const char *video_path, char *out, size_t size)
{
    const char *dot = strrchr(video_path, '.');
    const char *slash = strrchr(video_path, '/');
    size_t base = dot && (!slash || dot > slash) ? (size_t)(dot - video_path) : strlen(video_path);
    int n = snprintf(out, size, "%.*s" SRT_EXT, (int)base, video_path);
    return n > 0 && (size_t)n < size;
}

// A list of the conversion, at url (with its header).
static bool fetchList(NetClient *net, const char *url, const char *auth, const volatile bool *cancel, NetResponse *out)
{
    NetRequest request = {.url = url, .authorization = auth[0] ? auth : NULL, .cancel = cancel,
                          .stall_s = CONVERSION_STALL_S};
    return netFetchText(net, &request, LIST_MAX_BYTES, out);
}

// The conversion's parts: its media list (the best variant of a master list).
static bool readParts(NetClient *net, const char *url, const char *auth, long long max_bitrate,
                      const volatile bool *cancel, HlsPlaylist *out, char *error, size_t error_size)
{
    NetResponse master, media;
    if (!fetchList(net, url, auth, cancel, &master)) {
        sayStatus(master.status, master.error, error, error_size);
        return false;
    }
    char variant[HLS_URL_MAX], media_url[HLS_URL_MAX];
    bool ok;
    if (hlsBestVariant(master.data, max_bitrate, variant, sizeof(variant)) &&
        hlsResolveUrl(url, variant, media_url, sizeof(media_url))) {
        ok = fetchList(net, media_url, auth, cancel, &media);
        if (ok) {
            ok = hlsPlaylistParse(media.data, media_url, 0, out);
            netResponseFree(&media);
        } else {
            sayStatus(media.status, media.error, error, error_size);
        }
    } else {
        ok = hlsPlaylistParse(master.data, url, 0, out);
    }
    netResponseFree(&master);
    if (!ok && !error[0])
        snprintf(error, error_size, "The server's conversion can't be read.");
    return ok;
}

// The parts from first on, each whole before the next.
static bool fetchParts(NetClient *net, const HlsPlaylist *p, const char *auth, int first, NetSink sink,
                       DlPartDone part_done, void *user, const volatile bool *cancel, char *error, size_t error_size)
{
    for (int i = first; i < p->count; i++) {
        NetRequest request = {.url = p->urls[i], .authorization = auth[0] ? auth : NULL, .cancel = cancel,
                              .stall_s = CONVERSION_STALL_S};
        long status = 0;
        char why[NET_ERROR_MAX] = "";
        if (!netFetch(net, &request, sink, user, &status, why, sizeof(why))) {
            sayStatus(status, why, error, error_size);
            return false;
        }
        part_done(user, i, p->count);
    }
    return true;
}

static bool fetchConverted(const char *origin, long long max_bitrate, int first, NetSink sink, DlPartDone part_done,
                           void *user, const volatile bool *cancel, char *error, size_t error_size)
{
    JfPlan *plan = malloc(sizeof(JfPlan));
    char *url = malloc(JF_URL_MAX + JF_TRANSCODE_URL_MAX), auth[JF_AUTH_MAX];
    NetClient *net = plan && url ? netClientOpen() : NULL;
    bool ok = false;
    if (!net) {
        snprintf(error, error_size, "The network can't be used.");
    } else if (!jfSyncResolve(origin, max_bitrate, -1, -1, plan, error, error_size, cancel)) {
        // error says why.
    } else if (plan->direct) {  // The server sends it as it is: one part, the file.
        long long total = -1;
        ok = first > 0 || fetch(origin, 0, sink, user, &total, cancel, error, error_size);
        if (ok)
            part_done(user, 0, 1);
    } else {
        jfSyncConvertedRequest(origin, plan->transcode_url, url, JF_URL_MAX + JF_TRANSCODE_URL_MAX, auth, sizeof(auth));
        HlsPlaylist parts = {0};
        ok = readParts(net, url, auth, max_bitrate, cancel, &parts, error, error_size) &&
             fetchParts(net, &parts, auth, first, sink, part_done, user, cancel, error, error_size);
        hlsPlaylistFree(&parts);
        if (plan->play_session[0])
            jfSyncEndConversionOf(origin, plan->play_session);  // Done, or to begin again later.
    }
    netClientClose(net);
    free(url);
    free(plan);
    return ok;
}

// The subtitle file the server has beside the video (the one it marks to
// show first, else the first), as an .srt next to the download. Subtitles
// inside the video come with it, except in a conversion: then a text track
// inside it comes as the .srt.
static void extras(const char *origin, const char *video_path, bool converted, const volatile bool *cancel)
{
    JfPlan *plan = malloc(sizeof(JfPlan));
    char error[128], srt[LIBRARY_PATH_MAX + 8];
    if (!plan || !srtPathFor(video_path, srt, sizeof(srt)) ||
        !jfSyncResolve(origin, JF_MAX_BITRATE, -1, -1, plan, error, sizeof(error), cancel)) {
        free(plan);
        return;
    }
    int pick = -1;
    if (plan->sub_default >= 0 && plan->sub_default < plan->sub_count &&
        (converted || plan->subs[plan->sub_default].external))
        pick = plan->sub_default;
    for (int i = 0; i < plan->sub_count && pick < 0; i++)
        if (converted || plan->subs[i].external)
            pick = i;
    if (!plan->direct && plan->play_session[0])
        jfSyncEndConversionOf(origin, plan->play_session);  // Asking how to play it may have begun one.
    NetResponse r;
    if (pick >= 0 && !*cancel && jfSyncFetchSubtitle(origin, plan->subs[pick].index, cancel, &r)) {
        FILE *f = fopen(srt, "wb");
        if (f) {
            fwrite(r.data, 1, r.size, f);
            fclose(f);
        }
        netResponseFree(&r);
    }
    free(plan);
}

const DlSource DL_SERVER_SOURCE = {.handles = handles, .fetch = fetch, .fetch_converted = fetchConverted,
                                   .extras = extras};
