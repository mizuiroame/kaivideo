// Recording Live TV: the DVR's answers (made up, in Jellyfin 10.11's shape),
// marks in the guide, what can be done with a program, and the request to record.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "dvr.h"

static const char TIMERS[] =
    "{\"Items\":["
    "{\"Id\":\"t2\",\"Type\":\"Timer\",\"Status\":\"New\",\"ChannelId\":\"c1\",\"ChannelName\":\"KT Hora Exacta\","
    "\"ProgramId\":\"p2\",\"Name\":\"Resumen Nacional\",\"StartDate\":\"2026-11-19T11:00:00.0000000Z\","
    "\"EndDate\":\"2026-11-19T11:05:00.0000000Z\",\"SeriesTimerId\":\"s1\",\"PrePaddingSeconds\":0},"
    "{\"Id\":\"t1\",\"Status\":\"InProgress\",\"ChannelId\":\"c1\",\"ChannelName\":\"KT Hora Exacta\",\"ProgramId\":\"p1\","
    "\"Name\":\"Clima y Tráfico\",\"StartDate\":\"2026-11-19T10:00:00Z\",\"EndDate\":\"2026-11-19T10:10:00Z\"},"
    "{\"Id\":\"t3\",\"Status\":\"Cancelled\",\"ProgramId\":\"p3\",\"Name\":\"Gone\"},"
    "{\"Id\":\"t4\",\"Status\":\"ConflictedNotOk\",\"ProgramId\":\"p4\",\"Name\":\"Busy\","
    "\"StartDate\":\"2026-11-19T12:00:00Z\",\"EndDate\":\"2026-11-19T12:30:00Z\"},"
    "{\"Status\":\"New\",\"ProgramId\":\"p5\",\"Name\":\"No id\"}"
    "],\"TotalRecordCount\":5,\"StartIndex\":0}";

static const char SERIES[] =
    "{\"Items\":[{\"Id\":\"s2\",\"Name\":\"Zeta\",\"ChannelName\":\"KT Cine\",\"RecordAnyChannel\":false,"
    "\"RecordNewOnly\":true},{\"Id\":\"s1\",\"Name\":\"Resumen Nacional\",\"ChannelName\":\"KT Hora Exacta\","
    "\"RecordAnyChannel\":true,\"RecordNewOnly\":false}],\"TotalRecordCount\":2}";

static const char RECORDINGS[] =
    "{\"Items\":["
    "{\"Id\":\"r1\",\"Name\":\"Clima y Tráfico\",\"ChannelName\":\"KT Hora Exacta\",\"Status\":\"Completed\","
    "\"StartDate\":\"2026-11-18T10:00:00Z\",\"EndDate\":\"2026-11-18T10:10:00Z\",\"CanDelete\":true},"
    "{\"Id\":\"r2\",\"Name\":\"Resumen Nacional\",\"EpisodeTitle\":\"E12\",\"Status\":\"InProgress\","
    "\"StartDate\":\"2026-11-19T10:00:00Z\",\"EndDate\":\"2026-11-19T10:05:00Z\",\"CanDelete\":false},"
    "{\"Id\":\"r3\",\"Name\":\"Old\",\"DateCreated\":\"2026-11-01T08:00:00Z\"}"
    "],\"TotalRecordCount\":3}";

static void testParse(void)
{
    DvrTimer timers[8];
    CHECK(dvrParseTimers("nope", timers, 8) == -1);
    CHECK(dvrParseTimers(TIMERS, timers, 8) == 3);  // Not the cancelled one, nor one without an id.
    CHECK(!strcmp(timers[0].id, "t1") && timers[0].state == DVR_RECORDING);  // By start.
    CHECK(!strcmp(timers[1].id, "t2") && timers[1].state == DVR_SCHEDULED && !strcmp(timers[1].series_id, "s1"));
    CHECK(!strcmp(timers[1].program_id, "p2") && !strcmp(timers[1].channel, "KT Hora Exacta"));
    CHECK(timers[1].end - timers[1].start == 300);
    CHECK(timers[2].state == DVR_CONFLICT);
    CHECK(dvrParseTimers(TIMERS, timers, 1) == 1);  // Within the room given.

    DvrSeries series[4];
    CHECK(dvrParseSeries(SERIES, series, 4) == 2);
    CHECK(!strcmp(series[0].name, "Resumen Nacional") && series[0].any_channel && !series[0].new_only);
    CHECK(!strcmp(series[1].id, "s2") && series[1].new_only);
    CHECK(dvrParseSeries("{}", series, 4) == -1);

    DvrRecording recs[4];
    CHECK(dvrParseRecordings(RECORDINGS, recs, 4) == 3);
    CHECK(!strcmp(recs[0].id, "r2") && recs[0].recording && !recs[0].can_delete);  // The latest first.
    CHECK(!strcmp(recs[0].episode, "E12"));
    CHECK(!strcmp(recs[1].id, "r1") && recs[1].can_delete && dvrMinutes(recs[1].start, recs[1].end) == 10);
    CHECK(!strcmp(recs[2].id, "r3") && recs[2].start > 0 && dvrMinutes(recs[2].start, recs[2].end) == 0);
    char location[96];
    dvrRecordingLocation(&recs[1], location, sizeof(location));
    CHECK(!strcmp(location, "jellyfin:r1/r1"));
}

static void testMarksAndChoices(void)
{
    static DvrLists d;
    d.timer_count = dvrParseTimers(TIMERS, d.timers, DVR_TIMERS_MAX);
    CHECK(dvrTimerFor(&d, "p2") == &d.timers[1] && dvrTimerFor(&d, "p9") == NULL && dvrTimerFor(&d, "") == NULL);

    LiveGuide g;
    liveGuideInit(&g);
    CHECK(liveGuideReserve(&g, 1, 3));
    LiveChannel c = {.id = "c1"};
    LiveProgram programs[3] = {{.title = "A", .id = "p1", .marks = LIVE_IS_SERIES},
                               {.title = "B", .id = "p2", .marks = LIVE_TIMER},
                               {.title = "C", .id = "p9", .marks = LIVE_TIMER | LIVE_SERIES_TIMER}};
    CHECK(liveGuideAdd(&g, &c, programs, 3));
    dvrMarkGuide(&g, d.timers, d.timer_count);
    CHECK(g.pool[0].marks == (LIVE_TIMER | LIVE_IS_SERIES));  // Recorded now; still a series.
    CHECK(g.pool[1].marks == (LIVE_TIMER | LIVE_SERIES_TIMER));  // By its series.
    CHECK(g.pool[2].marks == 0);  // Its timer went.
    liveGuideFree(&g);

    const int64_t now = 1795082400;
    DvrAction a[DVR_DO_COUNT];
    LiveProgram p = {.title = "News", .id = "p7", .start = now + 600, .end = now + 1200};
    CHECK(dvrChoices(&p, now, a) == 1 && a[0] == DVR_DO_RECORD);
    p.marks = LIVE_IS_SERIES;
    CHECK(dvrChoices(&p, now, a) == 2 && a[0] == DVR_DO_RECORD && a[1] == DVR_DO_RECORD_SERIES);
    p.marks = LIVE_IS_SERIES | LIVE_TIMER;
    CHECK(dvrChoices(&p, now, a) == 2 && a[0] == DVR_DO_CANCEL && a[1] == DVR_DO_RECORD_SERIES);
    p.marks = LIVE_IS_SERIES | LIVE_TIMER | LIVE_SERIES_TIMER;
    CHECK(dvrChoices(&p, now, a) == 2 && a[0] == DVR_DO_CANCEL && a[1] == DVR_DO_CANCEL_SERIES);
    CHECK(dvrChoices(&p, now + 1200, a) == 0);  // Over.
    p.id[0] = '\0';
    CHECK(dvrChoices(&p, now, a) == 0);  // An IPTV list's: no server to record it.
    CHECK(dvrChoices(NULL, now, a) == 0);
    CHECK(!strcmp(dvrActionLabel(DVR_DO_RECORD), "Record this program"));
}

static void testBody(void)
{
    static const char DEFAULTS[] =
        "{\"RecordAnyTime\":false,\"SkipEpisodesInLibrary\":false,\"RecordAnyChannel\":false,\"KeepUpTo\":0,"
        "\"RecordNewOnly\":false,\"Days\":[],\"DayPattern\":\"Daily\",\"ImageTags\":{},\"ParentThumbImageTag\":null,"
        "\"ChannelId\":\"c1\",\"ProgramId\":\"p2\",\"Name\":\"Resumen Nacional\",\"StartDate\":\"2026-11-19T11:00:00.0000000Z\","
        "\"EndDate\":\"2026-11-19T11:05:00.0000000Z\",\"Priority\":0,\"PrePaddingSeconds\":0,\"PostPaddingSeconds\":0,"
        "\"IsPrePaddingRequired\":false,\"IsPostPaddingRequired\":false,\"KeepUntil\":\"UntilDeleted\"}";
    char body[2048];
    CHECK(dvrTimerBody(DEFAULTS, "p2", body, sizeof(body)));
    CHECK(strstr(body, "\"ProgramId\":\"p2\"") && strstr(body, "\"KeepUntil\":\"UntilDeleted\""));
    CHECK(strstr(body, "\"ParentThumbImageTag\":null"));  // As the server gave it.
    CHECK(!dvrTimerBody(DEFAULTS, "p3", body, sizeof(body)));  // Another program's.
    CHECK(!dvrTimerBody(DEFAULTS, "p2", body, 16));            // No room.
    CHECK(!dvrTimerBody("[]", "p2", body, sizeof(body)) && !dvrTimerBody(NULL, "p2", body, sizeof(body)));
}

void testDvr(void)
{
    testParse();
    testMarksAndChoices();
    testBody();
}
