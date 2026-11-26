// Records Live TV with a real Jellyfin server's DVR on a PC, as the console
// does: reads the guide's hours (now, and late in the day), records a program
// and a series, sees them in the timers and marked in the guide, cancels
// them, and (with WAIT) records what's on now until it ends, finds it among
// the recordings, reads a piece of it and deletes it.
//   JF_URL=http://192.168.0.10:8096 JF_TOKEN=... JF_USER=<user id> host_dvr CHANNEL [wait | stop LONG_CHANNEL]
// stop: records what's on LONG_CHANNEL now and stops it part way, saying what the server kept.
// CHANNEL: the name of a channel with short programs (the test server's
// "KT Hora Exacta"). The token is never printed; for personal testing only.
// Build (in tests/, image kaivideo-host):
//   cc -std=gnu11 -DKAIVIDEO_VERSION='"host"' -I../source -Ihost_video -I../third_party host_live/host_dvr.c
//      $(for f in jfdvr dvr jfrequest livetv jellyfin net certtrust lang genres library resume watched plex
//      plexsync naming util browser dav scroll md4; do echo ../source/$f.c; done) -ljansson -lcurl -lm -o /tmp/host_dvr
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "dvr.h"
#include "jfsync.h"
#include "jfsync_internal.h"

#define WAIT_STEP_S 15
#define AFTER_END_S 45   // The server closes a recording a moment after its program ends.
#define LISTED_WAIT_S 300  // How long the server may take to list it.
#define SOON_S 60        // A program to schedule starts at least this far ahead.

static int g_failures;
static JfSession g_session;

static void expect(bool ok, const char *what)
{
    printf("%s %s\n", ok ? "  ok  " : "  FAIL", what);
    g_failures += !ok;
}

// What jfsync.c gives the requests, here from the environment.
bool jfSyncIsUp(void)
{
    return true;
}

const char *jfSyncVersionText(void)
{
    return "host";
}

JfSession jfSyncSessionFor(ServerKind kind)
{
    (void)kind;
    return g_session;
}

PlexSession jfSyncPlexSession(void)
{
    PlexSession p;
    memset(&p, 0, sizeof(p));
    return p;
}

void jfSyncNotePlexDuration(const char *item, int64_t duration_ns)
{
    (void)item;
    (void)duration_ns;
}

static const LiveChannel *findChannel(const LiveGuide *g, const char *name)
{
    for (int i = 0; i < g->count; i++)
        if (!strcmp(g->channels[i].name, name))
            return &g->channels[i];
    return NULL;
}

// A program of c starting at least SOON_S from now, its series one if series.
static const LiveProgram *upcoming(const LiveChannel *c, int64_t now_s, bool series, int skip)
{
    for (int i = 0; c && i < c->program_count; i++)
        if (c->programs[i].start >= now_s + SOON_S && (!series || (c->programs[i].marks & LIVE_IS_SERIES)) &&
            skip-- <= 0)
            return &c->programs[i];
    return NULL;
}

static bool readLists(DvrLists *d)
{
    memset(d, 0, sizeof(*d));
    return jfSyncDvrFetch(d);
}

static const DvrSeries *seriesNamed(const DvrLists *d, const char *name)
{
    for (int i = 0; i < d->series_count; i++)
        if (!strcmp(d->series[i].name, name))
            return &d->series[i];
    return NULL;
}

// The guide's hours: now and late in the day.
static void tryGuide(LiveGuide *g, const char *channel, int64_t now_s)
{
    int count = jfSyncFetchLive(g, now_s, now_s + 18 * 3600, now_s + LIVE_GUIDE_DAY_S);
    expect(count > 0, "the channels came");
    printf("        %d channels, %d programs from 18 to 24 hours ahead\n", count, g->pool_count);
    const LiveChannel *c = findChannel(g, channel);
    expect(c && c->program_count > 0 && c->programs[0].end > now_s + 18 * 3600, "the guide's late hours");
    expect(c && c->now.title[0], "what's on now, though the hours read are later");
    count = jfSyncFetchLive(g, now_s, now_s, now_s + LIVE_WINDOW_S);
    c = findChannel(g, channel);
    expect(count > 0 && c && c->program_count > 0 && c->programs[0].id[0], "the next hours, with the programs' ids");
}

// A program recorded, then cancelled: in the timers and marked in the guide between.
static void tryProgram(LiveGuide *g, const char *channel, int64_t now_s)
{
    static DvrLists d;
    const LiveProgram *p = upcoming(findChannel(g, channel), now_s, false, 0);
    expect(p != NULL, "a program to record");
    if (!p)
        return;
    char id[LIVE_PROGRAM_ID_MAX];
    snprintf(id, sizeof(id), "%s", p->id);
    expect(jfSyncDvrRecord(id, false) == JF_DVR_DONE, "recorded the program");
    expect(readLists(&d), "the DVR's lists came");
    const DvrTimer *t = dvrTimerFor(&d, id);
    expect(t && t->state == DVR_SCHEDULED && !t->series_id[0], "its timer is scheduled");
    jfSyncFetchLive(g, now_s, now_s, now_s + LIVE_WINDOW_S);
    const LiveChannel *c = findChannel(g, channel);
    bool marked = false;
    for (int i = 0; c && i < c->program_count; i++)
        marked |= !strcmp(c->programs[i].id, id) && (c->programs[i].marks & LIVE_TIMER);
    expect(marked, "marked in the guide");
    expect(t && jfSyncDvrCancel(t->id) == JF_DVR_DONE, "cancelled it");
    expect(readLists(&d) && !dvrTimerFor(&d, id), "its timer went");
    expect(jfSyncDvrCancel("00000000000000000000000000000000") != JF_DVR_DONE, "a timer that isn't there: refused");
}

// A series recorded, then stopped.
static void trySeries(LiveGuide *g, const char *channel, int64_t now_s)
{
    static DvrLists d;
    const LiveProgram *p = upcoming(findChannel(g, channel), now_s, true, 1);
    expect(p != NULL, "a series' episode to record");
    if (!p)
        return;
    char id[LIVE_PROGRAM_ID_MAX], name[LIVE_NAME_MAX];
    snprintf(id, sizeof(id), "%s", p->id);
    snprintf(name, sizeof(name), "%s", p->title);
    expect(jfSyncDvrRecord(id, true) == JF_DVR_DONE, "recorded the series");
    sleep(3);  // The server makes its episodes' timers.
    expect(readLists(&d), "the DVR's lists came");
    const DvrSeries *s = seriesNamed(&d, name);
    const DvrTimer *t = dvrTimerFor(&d, id);
    expect(s != NULL, "the series is in its list");
    expect(t && s && !strcmp(t->series_id, s->id), "its episode's timer is the series'");
    printf("        %d timers with it\n", d.timer_count);
    expect(s && jfSyncDvrCancelSeries(s->id) == JF_DVR_DONE, "stopped recording the series");
    sleep(2);
    expect(readLists(&d) && !seriesNamed(&d, name) && !dvrTimerFor(&d, id), "the series and its timers went");
}

// What's on now, recorded until it ends; then read and deleted.
static void tryRecording(LiveGuide *g, const char *channel)
{
    static DvrLists d;
    int64_t now_s = (int64_t)time(NULL);
    jfSyncFetchLive(g, now_s, now_s, now_s + LIVE_WINDOW_S);
    const LiveChannel *c = findChannel(g, channel);
    const LiveProgram *p = c && c->program_count && c->programs[0].start <= now_s ? &c->programs[0] : NULL;
    expect(p && p->id[0], "what's on now");
    if (!p)
        return;
    char id[LIVE_PROGRAM_ID_MAX], name[LIVE_NAME_MAX];
    snprintf(id, sizeof(id), "%s", p->id);
    snprintf(name, sizeof(name), "%s", p->title);
    int64_t end = p->end;
    expect(jfSyncDvrRecord(id, false) == JF_DVR_DONE, "recording what's on now");
    sleep(WAIT_STEP_S);
    expect(readLists(&d), "the DVR's lists came");
    const DvrTimer *t = dvrTimerFor(&d, id);
    expect(t && t->state == DVR_RECORDING, "its timer records");
    bool listed = false;
    for (int i = 0; i < d.recording_count; i++)
        listed |= !strcmp(d.recordings[i].name, name) && d.recordings[i].recording;
    printf("        %s among the recordings while it records\n", listed ? "it's" : "it isn't");
    printf("        waiting %d s for it to end\n", (int)(end + AFTER_END_S - (int64_t)time(NULL)));
    while ((int64_t)time(NULL) < end + AFTER_END_S)
        sleep(WAIT_STEP_S);
    const DvrRecording *r = NULL;
    for (int wait = 0; !r && wait < LISTED_WAIT_S; wait += WAIT_STEP_S) {  // The server's library takes it in.
        if (wait)
            sleep(WAIT_STEP_S);
        readLists(&d);
        for (int i = 0; i < d.recording_count && !r; i++)
            if (!strcmp(d.recordings[i].name, name) && !d.recordings[i].recording)
                r = &d.recordings[i];
    }
    expect(r != NULL, "it's among the recordings, done");
    if (!r)
        return;
    printf("        %d min, can delete: %d\n", dvrMinutes(r->start, r->end), r->can_delete);
    char location[256], url[2048], auth[JF_AUTH_MAX];
    dvrRecordingLocation(r, location, sizeof(location));
    expect(jfSyncStreamRequest(location, url, sizeof(url), auth, sizeof(auth)), "its file's address");
    NetClient *net = netClientOpen();
    NetResponse resp;
    NetRequest request = {.url = url, .authorization = auth, .range = "0-1023", .timeout_s = 30};
    bool read = netFetchText(net, &request, 4096, &resp);
    expect(read && resp.size == 1024, "a piece of it, as other videos are read");
    if (read)
        netResponseFree(&resp);
    char error[160];
    JfPlan plan;
    expect(jfSyncResolve(location, 25000000, -1, -1, &plan, error, sizeof(error), NULL), "how to play it");
    printf("        direct=%d\n", plan.direct);
    netClientClose(net);
    char rid[DVR_ID_MAX];
    snprintf(rid, sizeof(rid), "%s", r->id);
    expect(r->can_delete && jfSyncDvrDelete(rid) == JF_DVR_DONE, "deleted it");
    bool gone = readLists(&d);
    for (int i = 0; i < d.recording_count; i++)
        gone &= strcmp(d.recordings[i].id, rid) != 0;
    expect(gone, "it's gone");
}

#define STOP_AFTER_S 40  // A recording stopped this long after it started.

// What's on now recorded, then stopped part way (its timer cancelled, as the player's Stop recording
// does): what the server keeps of it, which is then deleted.
static void tryStop(LiveGuide *g, const char *channel)
{
    static DvrLists d;
    int64_t now_s = (int64_t)time(NULL);
    jfSyncFetchLive(g, now_s, now_s, now_s + LIVE_WINDOW_S);
    const LiveChannel *c = findChannel(g, channel);
    const LiveProgram *p = c && c->program_count && c->programs[0].start <= now_s ? &c->programs[0] : NULL;
    expect(p && p->id[0] && p->end > now_s + STOP_AFTER_S + 15, "a program on now, long enough to stop part way");
    if (!p)
        return;
    char id[LIVE_PROGRAM_ID_MAX], name[LIVE_NAME_MAX];
    snprintf(id, sizeof(id), "%s", p->id);
    snprintf(name, sizeof(name), "%s", p->title);
    expect(jfSyncDvrRecord(id, false) == JF_DVR_DONE, "recording what's on now");
    sleep(STOP_AFTER_S);
    expect(readLists(&d), "the DVR's lists came");
    const DvrTimer *t = dvrTimerFor(&d, id);
    expect(t && t->state == DVR_RECORDING, "its timer records");
    expect(t && jfSyncDvrCancel(t->id) == JF_DVR_DONE, "stopped it (its timer cancelled)");
    sleep(5);
    expect(readLists(&d) && !dvrTimerFor(&d, id), "its timer went");
    const DvrRecording *r = NULL;
    for (int wait = 0; !r && wait < LISTED_WAIT_S; wait += WAIT_STEP_S) {
        if (wait)
            sleep(WAIT_STEP_S);
        readLists(&d);
        for (int i = 0; i < d.recording_count && !r; i++)
            if (!strcmp(d.recordings[i].name, name))
                r = &d.recordings[i];
    }
    printf("        what was recorded before it stopped: %s", r ? "kept" : "not among the recordings");
    if (r)
        printf(", %d min (recording: %d)", dvrMinutes(r->start, r->end), r->recording);
    printf("\n");
    if (r) {
        char rid[DVR_ID_MAX];
        snprintf(rid, sizeof(rid), "%s", r->id);
        expect(jfSyncDvrDelete(rid) == JF_DVR_DONE, "deleted what was kept");
    }
}

int main(int argc, char **argv)
{
    const char *url = getenv("JF_URL"), *token = getenv("JF_TOKEN"), *user = getenv("JF_USER");
    if (argc < 2 || !url || !token || !user) {
        fprintf(stderr, "JF_URL=... JF_TOKEN=... JF_USER=... %s CHANNEL [WAIT]\n", argv[0]);
        return 2;
    }
    snprintf(g_session.server, sizeof(g_session.server), "%s", url);
    snprintf(g_session.token, sizeof(g_session.token), "%s", token);
    snprintf(g_session.user_id, sizeof(g_session.user_id), "%s", user);
    snprintf(g_session.device_id, sizeof(g_session.device_id), "kaivideo-host-dvr");
    static LiveGuide g;
    liveGuideInit(&g);
    int64_t now_s = (int64_t)time(NULL);
    tryGuide(&g, argv[1], now_s);
    tryProgram(&g, argv[1], now_s);
    trySeries(&g, argv[1], now_s);
    if (argc > 2 && !strcmp(argv[2], "stop") && argc > 3)
        tryStop(&g, argv[3]);
    else if (argc > 2)
        tryRecording(&g, argv[1]);
    liveGuideFree(&g);
    printf("%d failures\n", g_failures);
    return g_failures ? 1 : 0;
}
