// Live TV: the server's channels and what's on them now and next.
#include <string.h>

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
    liveGuideSetPrograms(&g, PROGRAMS, now + 600);
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

    CHECK(liveGuideSetChannels(&g, "nope") == -1 && g.count == 3);  // An unreadable answer keeps what there was.
    char location[64];
    liveLocation(location, sizeof(location), "c1");
    CHECK(!strcmp(location, "jellyfin:c1/" LIVE_SOURCE) && liveIsLocation(location) && !liveIsLocation("jellyfin:a/b"));
    liveGuideFree(&g);
    CHECK(g.count == 0 && g.channels == NULL);
}
