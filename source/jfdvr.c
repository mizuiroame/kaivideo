// The Jellyfin server's DVR, from any thread: what it records and recorded,
// recording a program or its series, cancelling, and deleting a recording.
// Each call waits for the server.
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dvr.h"
#include "jfsync.h"
#include "jfsync_internal.h"
#include "net.h"

#define DVR_TIMEOUT_S 30L
#define DVR_MAX_BYTES (8u * 1024 * 1024)
#define DVR_BODY_MAX 8192
#define HTTP_FORBIDDEN 403

// A request to the Jellyfin server signed in: method NULL for GET (POST with a body).
static bool ask(NetClient *net, const JfSession *s, const char *method, const char *path, const char *body,
                NetResponse *out)
{
    char url[JF_URL_MAX + 256], auth[JF_AUTH_MAX];
    snprintf(url, sizeof(url), "%s%s", s->server, path);
    jfAuthHeader(s, jfSyncVersionText(), auth, sizeof(auth));
    NetRequest request = {.url = url, .authorization = auth, .post_json = body, .custom_method = method,
                          .timeout_s = DVR_TIMEOUT_S};
    return netFetchText(net, &request, DVR_MAX_BYTES, out);
}

static JfSession session(void)
{
    return jfSyncSessionFor(SERVER_JELLYFIN);  // Live TV comes from Jellyfin.
}

static bool usable(const JfSession *s)
{
    return jfSyncIsUp() && s->token[0] && s->user_id[0];
}

int jfSyncDvrTimers(NetClient *net, DvrTimer *out, int max)
{
    JfSession s = session();
    NetClient *own = !net && usable(&s) ? netClientOpen() : NULL;
    NetResponse r;
    int count = -1;
    if (usable(&s) && (net || own) && ask(net ? net : own, &s, NULL, "/LiveTv/Timers", NULL, &r)) {
        count = dvrParseTimers(r.data, out, max);
        netResponseFree(&r);
    }
    netClientClose(own);
    return count;
}

bool jfSyncDvrFetch(DvrLists *out)
{
    JfSession s = session();
    NetClient *net = usable(&s) ? netClientOpen() : NULL;
    if (!net)
        return false;
    char path[256];
    NetResponse r;
    int timers = jfSyncDvrTimers(net, out->timers, DVR_TIMERS_MAX), series = -1, recordings = -1;
    if (timers >= 0 && ask(net, &s, NULL, "/LiveTv/SeriesTimers", NULL, &r)) {
        series = dvrParseSeries(r.data, out->series, DVR_SERIES_MAX);
        netResponseFree(&r);
    }
    snprintf(path, sizeof(path), "/LiveTv/Recordings?UserId=%s&Fields=CanDelete&EnableImages=false&SortBy=DateCreated&SortOrder=Descending&Limit=%d",
             s.user_id, DVR_RECORDINGS_MAX);
    if (series >= 0 && ask(net, &s, NULL, path, NULL, &r)) {
        recordings = dvrParseRecordings(r.data, out->recordings, DVR_RECORDINGS_MAX);
        netResponseFree(&r);
    }
    netClientClose(net);
    out->timer_count = timers > 0 ? timers : 0;
    out->series_count = series > 0 ? series : 0;
    out->recording_count = recordings > 0 ? recordings : 0;
    return timers >= 0 && series >= 0 && recordings >= 0;
}

// What a change's answer means.
static JfDvrResult resultOf(bool ok, const NetResponse *r)
{
    if (ok)
        return JF_DVR_DONE;
    return r->status == HTTP_FORBIDDEN ? JF_DVR_NOT_ALLOWED
           : r->status                                                    ? JF_DVR_REFUSED
                                                                          : JF_DVR_NO_ANSWER;
}

// An id that can go in an address as it is.
static bool plainId(const char *id)
{
    for (const char *c = id; *c; c++)
        if (!isalnum((unsigned char)*c) && *c != '-' && *c != '_')
            return false;
    return id[0] != 0;
}

JfDvrResult jfSyncDvrRecord(const char *program_id, bool series)
{
    JfSession s = session();
    NetClient *net = usable(&s) && plainId(program_id) ? netClientOpen() : NULL;
    if (!net)
        return JF_DVR_NO_ANSWER;
    char path[256], *body = malloc(DVR_BODY_MAX);
    NetResponse r = {0};
    JfDvrResult result = JF_DVR_NO_ANSWER;
    snprintf(path, sizeof(path), "/LiveTv/Timers/Defaults?programId=%s", program_id);
    bool ok = body && ask(net, &s, NULL, path, NULL, &r);
    if (ok) {
        ok = dvrTimerBody(r.data, program_id, body, DVR_BODY_MAX);
        netResponseFree(&r);
        result = JF_DVR_REFUSED;  // Defaults for another program: something's wrong.
    } else if (body) {
        result = resultOf(false, &r);
    }
    if (ok) {
        ok = ask(net, &s, NULL, series ? "/LiveTv/SeriesTimers" : "/LiveTv/Timers", body, &r);
        result = resultOf(ok, &r);
        if (ok)
            netResponseFree(&r);
    }
    free(body);
    netClientClose(net);
    return result;
}

// DELETE of path's kind of thing, id.
static JfDvrResult removeIt(const char *kind, const char *id)
{
    JfSession s = session();
    NetClient *net = usable(&s) && plainId(id) ? netClientOpen() : NULL;
    if (!net)
        return JF_DVR_NO_ANSWER;
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", kind, id);
    NetResponse r = {0};
    bool ok = ask(net, &s, "DELETE", path, NULL, &r);
    if (ok)
        netResponseFree(&r);
    netClientClose(net);
    return resultOf(ok, &r);
}

JfDvrResult jfSyncDvrCancel(const char *timer_id)
{
    return removeIt("/LiveTv/Timers", timer_id);
}

JfDvrResult jfSyncDvrCancelSeries(const char *series_id)
{
    return removeIt("/LiveTv/SeriesTimers", series_id);
}

JfDvrResult jfSyncDvrDelete(const char *recording_id)
{
    return removeIt("/Items", recording_id);  // A recording is one of the library's items.
}
