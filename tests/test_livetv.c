// Live TV: the server's channels and what's on them now and next.
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "check.h"
#include "livetv.h"

void testLiveTv(void)
{
    static const char CHANNELS[] =
        "{\"Items\":[{\"Id\":\"c2\",\"Name\":\"Two\",\"ChannelNumber\":\"2\"},"
        "{\"Id\":\"c1\",\"Name\":\"One\",\"ChannelNumber\":\"1\"},"
        "{\"Id\":\"c10\",\"Name\":\"Ten\",\"ChannelNumber\":\"10\"},"
        "{\"Name\":\"No id\"}],\"TotalRecordCount\":4}";
    static const char PROGRAMS[] =
        "{\"Items\":["
        "{\"ChannelId\":\"c1\",\"Name\":\"Morning Lanterns\",\"StartDate\":\"2026-11-19T10:00:00.0000000Z\",\"EndDate\":\"2026-11-19T10:30:00.0000000Z\"},"
        "{\"ChannelId\":\"c1\",\"Name\":\"Harbor Weather\",\"StartDate\":\"2026-11-19T10:30:00.0000000Z\",\"EndDate\":\"2026-11-19T11:00:00.0000000Z\"},"
        "{\"ChannelId\":\"c1\",\"Name\":\"Later On\",\"StartDate\":\"2026-11-19T11:00:00.0000000Z\",\"EndDate\":\"2026-11-19T11:30:00.0000000Z\"},"
        "{\"ChannelId\":\"c2\",\"Name\":\"Old News\",\"StartDate\":\"2026-11-19T09:00:00.0000000Z\",\"EndDate\":\"2026-11-19T10:00:00.0000000Z\"},"
        "{\"ChannelId\":\"zz\",\"Name\":\"Elsewhere\",\"StartDate\":\"2026-11-19T10:00:00Z\",\"EndDate\":\"2026-11-19T11:00:00Z\"}]}";
    LiveGuide g;
    liveGuideInit(&g);
    CHECK(liveGuideSetChannels(&g, CHANNELS) == 3);
    // By number, as a remote's numbers go (10 after 2).
    CHECK(!strcmp(g.channels[0].id, "c1") && !strcmp(g.channels[1].id, "c2") && !strcmp(g.channels[2].id, "c10"));
    CHECK(!strcmp(g.channels[0].number, "1") && !strcmp(g.channels[0].name, "One"));

    const int64_t now = 1795082400;  // 2026-11-19 10:00:00 UTC, plus 10 minutes below.
    liveGuideSetPrograms(&g, PROGRAMS, now + 600, 0, 0);
    const LiveChannel *one = &g.channels[0];
    CHECK(!strcmp(one->now.title, "Morning Lanterns") && !strcmp(one->next.title, "Harbor Weather"));
    CHECK(one->now.start == now && one->now.end == now + 1800);
    CHECK(liveProgress(&one->now, now + 600) > 0.33 && liveProgress(&one->now, now + 600) < 0.34);
    CHECK(g.channels[1].now.title[0] == '\0' && g.channels[1].next.title[0] == '\0');  // Only something ended.
    CHECK(liveProgress(&g.channels[1].now, now) < 0);

    char line[96];
    liveTimeRange(&one->next, 0, true, line, sizeof(line));
    CHECK(!strcmp(line, "10:30 - 11:00"));
    liveTimeRange(&one->next, -6 * 3600, false, line, sizeof(line));  // Six hours behind UTC.
    CHECK(!strcmp(line, "4:30 AM - 5:00 AM"));

    // The guide keeps every program not over yet, by start time.
    CHECK(one->program_count == 3);
    CHECK(!strcmp(one->programs[0].title, "Morning Lanterns") && !strcmp(one->programs[2].title, "Later On"));
    CHECK(g.channels[1].program_count == 0 && g.channels[2].program_count == 0);
    CHECK(liveProgramAt(one, now + 600) == 0 && liveProgramAt(one, now + 1800) == 1);
    CHECK(liveProgramAt(one, now + 5400) == -1 && liveProgramAt(&g.channels[1], now) == -1);

    // The cursor through a channel's programs: 0 is "now".
    CHECK(liveStep(one, 0, now + 600, 1) == now + 1800);
    CHECK(liveStep(one, now + 1800, now + 600, 1) == now + 3600);
    CHECK(liveStep(one, now + 3600, now + 600, 1) == now + 5400);  // Nothing later known: where it ends (read next).
    CHECK(liveStep(one, now + 3600, now + 600, -1) == now + 1800);
    CHECK(liveStep(one, now + 1800, now + 600, -1) == 0);           // Back to what's on now.
    CHECK(liveStep(one, 0, now + 600, -1) == 0);
    CHECK(liveStep(one, now + 300, now + 600, 1) == now + 1800);    // A time gone by counts as now.
    CHECK(liveStep(&g.channels[1], 0, now, 1) == now + 1800);  // No guide: half an hour on.
    CHECK(liveStep(&g.channels[1], now + 1800, now, -1) == 0);
    CHECK(liveStep(&g.channels[1], now + LIVE_GUIDE_DAY_S, now, 1) == now + LIVE_GUIDE_DAY_S);  // A day ahead at most.
    CHECK(liveStep(one, now + 9000, now + 600, -1) == now + 7200);  // Past the known: half an hour back,
    CHECK(liveStep(one, now + 5400, now + 600, -1) == now + 3600);  // then the last one known.
    CHECK(liveStep(one, now + 2400, now + 600, -1) == now + 1800);  // Mid-program (a finger): its start first.
    CHECK(liveShift(0, now, 600) == now + 600 && liveShift(now + 600, now, -1200) == 0);
    CHECK(liveShift(0, now, 2 * LIVE_GUIDE_DAY_S) == now + LIVE_GUIDE_DAY_S);

    // The view: two hours from the half hour of now, moving on to keep the cursor in it.
    CHECK(liveGridStart(now + 600, 0, 0) == now);
    CHECK(liveGridStart(now + 600, now + 2 * 1800 + 60, 0) == now);
    CHECK(liveGridStart(now + 600, now + 3 * 1800, 0) == now + 1800);  // Kept off the last column.
    CHECK(liveGridStart(now + 600, now + 5 * 1800 + 60, 0) == now + 3 * 1800);
    CHECK(liveGridStart(now + 600, 0, 45 * 60) == now - 900);  // 45 minutes ahead of UTC: local half hours.
    int x, w;
    CHECK(liveGridSpan(&one->programs[0], now, 100, 800, &x, &w) && x == 100 && w == 200);
    CHECK(liveGridSpan(&one->programs[2], now, 100, 800, &x, &w) && x == 500 && w == 200);
    LiveProgram long_one = {.start = now - 3600, .end = now + 9 * 3600};
    CHECK(liveGridSpan(&long_one, now, 100, 800, &x, &w) && x == 100 && w == 800);  // Clipped to the view.
    LiveProgram later = {.start = now + LIVE_GRID_SPAN_S, .end = now + LIVE_GRID_SPAN_S + 1800};
    CHECK(!liveGridSpan(&later, now, 100, 800, &x, &w));

    // A channel's list stays in order, without the same program twice, and within its room.
    enum { MANY = LIVE_PROGRAMS_PER_CHANNEL + 3 };
    static char many[MANY * 160 + 256];
    int at = snprintf(many, sizeof(many), "{\"Items\":[");
    for (int i = MANY - 1; i >= 0; i--) {
        char from[32], to[32];
        time_t a_t = (time_t)(now + i * 300), b_t = (time_t)(now + (i + 1) * 300);
        struct tm tm;
        strftime(from, sizeof(from), "%Y-%m-%dT%H:%M:%SZ", gmtime_r(&a_t, &tm));
        strftime(to, sizeof(to), "%Y-%m-%dT%H:%M:%SZ", gmtime_r(&b_t, &tm));
        at += snprintf(many + at, sizeof(many) - (size_t)at,
                       "%s{\"ChannelId\":\"c2\",\"Name\":\"Show %d\",\"StartDate\":\"%s\",\"EndDate\":\"%s\"}",
                       i == MANY - 1 ? "" : ",", i, from, to);
    }
    snprintf(many + at, sizeof(many) - (size_t)at,
             ",{\"ChannelId\":\"c2\",\"Name\":\"Show 0\",\"StartDate\":\"2026-11-19T10:00:00Z\",\"EndDate\":\"2026-11-19T10:05:00Z\"}]}");
    liveGuideSetPrograms(&g, many, now, 0, 0);
    const LiveChannel *two = &g.channels[1];
    CHECK(two->program_count == LIVE_PROGRAMS_PER_CHANNEL);
    CHECK(!strcmp(two->programs[0].title, "Show 0") && !strcmp(two->programs[1].title, "Show 1"));
    bool ordered = true;
    for (int i = 1; i < two->program_count; i++)
        ordered = ordered && two->programs[i].start > two->programs[i - 1].start;
    CHECK(ordered && !strcmp(two->now.title, "Show 0") && !strcmp(two->next.title, "Show 1"));
    CHECK(g.channels[0].program_count == 0);  // A new answer replaces what there was.

    // The guide's hours: only programs in them are kept, but what's on now is known.
    liveGuideSetPrograms(&g, many, now, now + 3600, now + 7200);
    CHECK(g.from == now + 3600 && g.to == now + 7200);
    CHECK(two->program_count == 12 && !strcmp(two->programs[0].title, "Show 12") && !strcmp(two->now.title, "Show 0"));
    // What's on now, read apart: the programs stay.
    LiveGuide n;
    liveGuideInit(&n);
    CHECK(liveGuideSetChannels(&n, CHANNELS) == 3);
    liveGuideSetNow(&n, PROGRAMS, now + 600);
    CHECK(!strcmp(n.channels[0].now.title, "Morning Lanterns") && n.channels[0].program_count == 0);
    liveGuideFree(&n);

    // The hours to read for a view: from 2 hours before it (not before now), 6 hours.
    int64_t from = 0, to = 0;
    LiveGuide hours = {.from = now, .to = now + LIVE_WINDOW_S};
    CHECK(!liveWindowFor(&hours, now, now + 3600, &from, &to));  // In the hours read already.
    CHECK(liveWindowFor(&hours, now, now + 6 * 3600, &from, &to) && from == now + 4 * 3600 && to == now + 10 * 3600);
    CHECK(!liveWindowFor(&hours, now, now - 600, &from, &to));
    hours.to = now + LIVE_GUIDE_DAY_S + 3600;  // The view's end is kept within the day.
    hours.from = now + LIVE_GUIDE_DAY_S - 3600;
    CHECK(!liveWindowFor(&hours, now, now + LIVE_GUIDE_DAY_S - 1800, &from, &to));
    LiveGuide empty;
    liveGuideInit(&empty);
    CHECK(liveWindowFor(&empty, now, now - 600, &from, &to) && from == now && to == now + LIVE_WINDOW_S);

    // Marks and ids from the server: what it records, series.
    static const char MARKED[] =
        "{\"Items\":[{\"Id\":\"p1\",\"ChannelId\":\"c1\",\"Name\":\"A\",\"StartDate\":\"2026-11-19T10:00:00Z\","
        "\"EndDate\":\"2026-11-19T10:30:00Z\",\"TimerId\":\"t1\",\"IsSeries\":true},"
        "{\"Id\":\"p2\",\"ChannelId\":\"c1\",\"Name\":\"B\",\"StartDate\":\"2026-11-19T10:30:00Z\","
        "\"EndDate\":\"2026-11-19T11:00:00Z\",\"SeriesTimerId\":\"s1\"}]}";
    liveGuideSetPrograms(&g, MARKED, now, 0, 0);
    CHECK(!strcmp(one->programs[0].id, "p1") && one->programs[0].marks == (LIVE_TIMER | LIVE_IS_SERIES));
    CHECK(one->programs[1].marks == LIVE_SERIES_TIMER);

    // A guide put together channel by channel (the server's and an IPTV list's).
    LiveGuide built;
    liveGuideInit(&built);
    CHECK(liveGuideReserve(&built, 2, 3));
    CHECK(liveGuideAdd(&built, one, one->programs, one->program_count));
    CHECK(!liveGuideAdd(&built, two, one->programs, 2));  // No room for its programs.
    CHECK(liveGuideAdd(&built, &g.channels[2], NULL, 0) && !liveGuideAdd(&built, one, NULL, 0));
    CHECK(built.count == 2 && built.channels[0].programs == built.pool && !strcmp(built.pool[1].title, "B"));
    CHECK(built.channels[1].program_count == 0);
    liveGuideFree(&built);

    char clock[16];
    liveClock(now + 1800, -6 * 3600, false, clock, sizeof(clock));
    CHECK(!strcmp(clock, "4:30 AM"));

    CHECK(liveGuideSetChannels(&g, "nope") == -1 && g.count == 3);  // An unreadable answer keeps what there was.
    char location[64];
    liveLocation(location, sizeof(location), "c1");
    CHECK(!strcmp(location, "jellyfin:c1/" LIVE_SOURCE) && liveIsLocation(location) && !liveIsLocation("jellyfin:a/b"));
    liveGuideFree(&g);
    CHECK(g.count == 0 && g.channels == NULL);
}
