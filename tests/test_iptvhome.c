// The IPTV view's Home: recents, favorites and the rows, made-up channels only.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "iptvhome.h"

static const char *const M3U = "#EXTM3U\n"
                               "#EXTINF:-1 group-title=\"News\",Alpha\nhttps://s.example/a.m3u8\n"
                               "#EXTINF:-1 group-title=\"Sports\",Bravo\nhttps://s.example/b.m3u8\n"
                               "#EXTINF:-1 group-title=\"News\",Charlie\nhttps://s.example/c.ts\n"
                               "#EXTINF:-1 group-title=\"Empty\",Delta\nhttps://s.example/d.m3u8\n";

static void testMarks(void)
{
    IptvMarks *m = malloc(sizeof(*m));
    iptvMarksInit(m);
    iptvMarksWatched(m, "Alpha");
    iptvMarksWatched(m, "Bravo");
    iptvMarksWatched(m, "Alpha");  // Again: first, not twice.
    CHECK(m->recent_count == 2 && !strcmp(m->recent[0], "Alpha") && !strcmp(m->recent[1], "Bravo"));
    for (int i = 0; i < IPTVHOME_RECENT_MAX + 5; i++) {
        char name[16];
        snprintf(name, sizeof(name), "Ch %d", i);
        iptvMarksWatched(m, name);
    }
    CHECK(m->recent_count == IPTVHOME_RECENT_MAX);
    CHECK(!strcmp(m->recent[0], "Ch 24") && !strcmp(m->recent[IPTVHOME_RECENT_MAX - 1], "Ch 5"));

    CHECK(iptvMarksToggleFavorite(m, "Bravo"));
    CHECK(iptvMarksToggleFavorite(m, "Tab\there"));
    CHECK(iptvMarksIsFavorite(m, "Bravo") && iptvMarksIsFavorite(m, "Tab\there"));
    CHECK(!iptvMarksToggleFavorite(m, "Bravo") && !iptvMarksIsFavorite(m, "Bravo"));
    CHECK(!iptvMarksToggleFavorite(m, ""));

    char text[8192];
    CHECK(iptvMarksWrite(m, text, sizeof(text)));
    CHECK(strstr(text, "recent\tCh 24\n") == text);
    CHECK(strstr(text, "favorite\tTab here\n"));
    IptvMarks *back = malloc(sizeof(*back));
    iptvMarksRead(back, text);
    CHECK(back->recent_count == m->recent_count && back->favorite_count == 1);
    CHECK(!strcmp(back->recent[3], m->recent[3]) && !strcmp(back->favorites[0], "Tab here"));
    char small[16];
    CHECK(!iptvMarksWrite(m, small, sizeof(small)));

    iptvMarksRead(back, "recent\tOne\r\nfavorite\tTwo\nother\tThree\nno tab\nrecent\tOne\nrecent\t\n");
    CHECK(back->recent_count == 1 && back->favorite_count == 1 && !strcmp(back->recent[0], "One"));
    CHECK(!strcmp(back->favorites[0], "Two"));
    free(back);
    free(m);
}

static void testRows(void)
{
    IptvList list;
    CHECK(iptvParseM3u(M3U, strlen(M3U), &list));
    IptvMarks *m = malloc(sizeof(*m));
    iptvMarksInit(m);
    IptvHome h = {0};
    iptvHomeBuild(&h, &list, m);
    CHECK(h.count == 3);  // News, Sports, Empty: no recents nor favorites yet.
    CHECK(h.rows[0].kind == IPTVROW_GROUP && h.rows[0].count == 2);
    CHECK(iptvHomeChannel(&h, 0, 1) == 2 && iptvHomeChannel(&h, 0, 2) == -1 && iptvHomeChannel(&h, 9, 0) == -1);

    iptvMarksWatched(m, "Charlie");
    iptvMarksWatched(m, "Gone");  // Not in this list: left out.
    iptvMarksWatched(m, "Bravo");
    iptvMarksToggleFavorite(m, "Delta");
    iptvHomeBuild(&h, &list, m);
    CHECK(h.count == 5);
    CHECK(h.rows[0].kind == IPTVROW_RECENT && h.rows[0].count == 2);
    CHECK(iptvHomeChannel(&h, 0, 0) == 1 && iptvHomeChannel(&h, 0, 1) == 2);
    CHECK(h.rows[1].kind == IPTVROW_FAVORITES && iptvHomeChannel(&h, 1, 0) == 3);

    IptvHomeView *v = malloc(sizeof(*v));
    iptvHomeViewReset(v);
    iptvHomeMove(v, &h, 5, 0, 3, 5);
    CHECK(v->rows.cursor == 0 && v->cols[0].cursor == 1);
    iptvHomeMove(v, &h, 0, 9, 3, 5);
    CHECK(v->rows.cursor == 4 && v->rows.first == 2);
    iptvHomeMove(v, &h, 0, -1, 3, 5);
    CHECK(v->rows.cursor == 3 && v->cols[0].cursor == 1);  // Each row keeps its place.
    iptvHomeFree(&h);
    iptvHomeBuild(&h, &list, m);
    iptvHomeFree(&h);
    CHECK(h.count == 0 && !h.pool);

    // Without groups: one row of them all.
    iptvListFree(&list);
    const char *plain = "#EXTM3U\n#EXTINF:-1,One\nhttps://s.example/1\n#EXTINF:-1,Two\nhttps://s.example/2\n";
    CHECK(iptvParseM3u(plain, strlen(plain), &list));
    iptvMarksInit(m);
    iptvHomeBuild(&h, &list, m);
    CHECK(h.count == 1 && h.rows[0].group == -1 && h.rows[0].count == 2);
    iptvHomeFree(&h);
    iptvListFree(&list);
    free(v);
    free(m);
}

void testIptvHome(void)
{
    testMarks();
    testRows();
}
