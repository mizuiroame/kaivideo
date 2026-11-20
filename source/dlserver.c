// Downloads from the media servers (Jellyfin, Emby, Plex): the original file,
// read in one stream from where the card's part ends, and its subtitle file.
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dlworker.h"
#include "jfsync.h"

#define STALL_S 30L
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

// The subtitle file the server has beside the video (the one it marks to
// show first, else the first), as an .srt next to the download. Subtitles
// inside the video come with it.
static void extras(const char *origin, const char *video_path, const volatile bool *cancel)
{
    JfPlan *plan = malloc(sizeof(JfPlan));
    char error[128], srt[LIBRARY_PATH_MAX + 8];
    if (!plan || !srtPathFor(video_path, srt, sizeof(srt)) ||
        !jfSyncResolve(origin, JF_MAX_BITRATE, -1, -1, plan, error, sizeof(error), cancel)) {
        free(plan);
        return;
    }
    int pick = -1;
    if (plan->sub_default >= 0 && plan->sub_default < plan->sub_count && plan->subs[plan->sub_default].external)
        pick = plan->sub_default;
    for (int i = 0; i < plan->sub_count && pick < 0; i++)
        if (plan->subs[i].external)
            pick = i;
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

const DlSource DL_SERVER_SOURCE = {.handles = handles, .fetch = fetch, .extras = extras};
