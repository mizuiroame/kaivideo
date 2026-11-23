// IPTV lists (M3U, Xtream) and their XMLTV guides, made-up channels only.
#include <stdlib.h>
#include <string.h>

#include <zlib.h>

#include "check.h"
#include "iptv.h"
#include "xmltv.h"

static void testM3u(void)
{
    const char *m3u = "\xEF\xBB\xBF#EXTM3U url-tvg=\"https://guide.example/epg.xml.gz,https://other.example/x\"\r\n"
                      "#EXTINF:-1 tvg-id=\"one.example\" tvg-name=\"One\" tvg-logo=\"https://logo.example/1.png\" "
                      "tvg-chno=\"7\" group-title=\"News\",Channel One HD\r\n"
                      "#EXTVLCOPT:http-user-agent=Thing\r\n"
                      "https://stream.example/one/index.m3u8\r\n"
                      "#EXTINF:-1 tvg-id=\"two.example\" group-title=\"Sports\",Two, the \"Sports\" one\n"
                      "http://stream.example/two.ts\n"
                      "#EXTINF:-1,No URL that plays\n"
                      "rtmp://stream.example/three\n"
                      "#EXTINF:-1 tvg-logo=\"javascript:x\" group-title=\"News\",Four\n"
                      "https://stream.example/four.m3u8\n"
                      "#EXTINF:-1\n"
                      "#EXTGRP:Music\n"
                      "https://stream.example/five.m3u8\n"
                      "https://stream.example/bare.m3u8\n";
    IptvList list;
    CHECK(iptvParseM3u(m3u, strlen(m3u), &list));
    CHECK(list.count == 5 && list.group_count == 3 && !list.truncated);
    CHECK(!strcmp(list.epg_url, "https://guide.example/epg.xml.gz"));
    if (list.count == 5) {
        const IptvChannel *c = list.channels;
        CHECK(!strcmp(c[0].name, "Channel One HD") && !strcmp(c[0].epg_id, "one.example") &&
              !strcmp(c[0].logo, "https://logo.example/1.png") && !strcmp(c[0].number, "7") &&
              !strcmp(c[0].url, "https://stream.example/one/index.m3u8") && c[0].group == 0);
        CHECK(!strcmp(c[1].name, "Two, the \"Sports\" one") && !strcmp(c[1].url, "http://stream.example/two.ts") &&
              !strcmp(list.groups[c[1].group].name, "Sports"));
        CHECK(!strcmp(c[2].name, "Four") && c[2].logo[0] == '\0' && c[2].group == 0);  // Not a web logo.
        CHECK(!strcmp(c[3].name, "https://stream.example/five.m3u8") &&
              !strcmp(list.groups[c[3].group].name, "Music"));  // No name: its address.
        CHECK(c[4].group == -1 && !strcmp(c[4].name, "https://stream.example/bare.m3u8"));
        CHECK(list.groups[0].count == 2 && !strcmp(list.groups[0].name, "News"));
    }
    iptvListFree(&list);
    CHECK(!iptvParseM3u("<html>Not found</html>", 21, &list) && list.count == 0);
    CHECK(iptvParseM3u("#EXTM3U\n", 8, &list) && list.count == 0);  // An empty list is still one.
    iptvListFree(&list);

    // Headers a channel asks for: #EXTVLCOPT before or after #EXTINF, or Kodi's "|" form; each channel its own.
    const char *with_headers = "#EXTM3U\n"
                               "#EXTVLCOPT:http-referrer=https://site.example/\n"
                               "#EXTINF:-1 group-title=\"News\",Referred\n"
                               "#EXTVLCOPT:http-user-agent=Mozilla/5.0 (Test)\n"
                               "#EXTVLCOPT:http-origin=https://site.example\n"
                               "#EXTVLCOPT:network-caching=1000\n"
                               "https://stream.example/a.m3u8\n"
                               "#EXTINF:-1,Plain\n"
                               "https://stream.example/b.m3u8\n"
                               "#EXTINF:-1,Kodi\n"
                               "https://stream.example/c.m3u8|User-Agent=Agent%20Two&Referer=https%3A%2F%2Fk.example%2F"
                               "&Cookie=x\n"
                               "#EXTINF:-1,Broken\n"
                               "#EXTVLCOPT:http-referrer=https://a.example/\r\n"
                               "https://stream.example/d.ts|Referer=https%3A%2F%2Fa.example%2F%0D%0AX-Evil:%201\n";
    CHECK(iptvParseM3u(with_headers, strlen(with_headers), &list) && list.count == 4);
    if (list.count == 4) {
        const IptvChannel *c = list.channels;
        CHECK(!strcmp(c[0].headers.referer, "https://site.example/") &&
              !strcmp(c[0].headers.user_agent, "Mozilla/5.0 (Test)") &&
              !strcmp(c[0].headers.origin, "https://site.example") && !strcmp(c[0].name, "Referred"));
        CHECK(!streamHeadersAny(&c[1].headers));  // Not carried over to the next channel.
        CHECK(!strcmp(c[2].url, "https://stream.example/c.m3u8") && !strcmp(c[2].headers.user_agent, "Agent Two") &&
              !strcmp(c[2].headers.referer, "https://k.example/") && !c[2].headers.origin[0]);
        // A line break decoded from %0D%0A isn't taken; the #EXTVLCOPT one before stays.
        CHECK(!strcmp(c[3].url, "https://stream.example/d.ts") && !strcmp(c[3].headers.referer, "https://a.example/"));
    }
    iptvListFree(&list);

    // A list longer than kept.
    size_t size = (IPTV_CHANNELS_MAX + 50) * 80 + 16, n = 0;
    char *big = malloc(size);
    if (big) {
        n += (size_t)snprintf(big, size, "#EXTM3U\n");
        for (int i = 0; i < IPTV_CHANNELS_MAX + 50; i++)
            n += (size_t)snprintf(big + n, size - n, "#EXTINF:-1,C%d\nhttps://s.example/%d.m3u8\n", i, i);
        CHECK(iptvParseM3u(big, n, &list) && list.count == IPTV_CHANNELS_MAX && list.truncated);
        iptvListFree(&list);
        free(big);
    }
}

static void testXtream(void)
{
    char out[IPTV_URL_MAX];
    CHECK(xtreamNormalizeServer(" tv.example:8080/ ", out, sizeof(out)) && !strcmp(out, "http://tv.example:8080"));
    CHECK(xtreamNormalizeServer("https://tv.example/player_api.php?x=1", out, sizeof(out)) &&
          !strcmp(out, "https://tv.example"));
    CHECK(!xtreamNormalizeServer("ftp://tv.example", out, sizeof(out)) &&
          !xtreamNormalizeServer("http://me@tv.example", out, sizeof(out)) &&
          !xtreamNormalizeServer("", out, sizeof(out)));

    XtreamAccount a = {"http://tv.example:8080", "kai user", "p&ss/word"};
    CHECK(xtreamApiUrl(&a, "get_live_streams", NULL, out, sizeof(out)) &&
          !strcmp(out, "http://tv.example:8080/player_api.php?username=kai%20user&password=p%26ss%2Fword"
                       "&action=get_live_streams"));
    CHECK(xtreamApiUrl(&a, "", NULL, out, sizeof(out)) &&
          !strcmp(out, "http://tv.example:8080/player_api.php?username=kai%20user&password=p%26ss%2Fword"));
    CHECK(xtreamStreamUrl(&a, "live", 42, "m3u8", out, sizeof(out)) &&
          !strcmp(out, "http://tv.example:8080/live/kai%20user/p%26ss%2Fword/42.m3u8"));
    CHECK(!xtreamStreamUrl(&a, "movie", 42, "../x", out, sizeof(out)) && !xtreamStreamUrl(&a, "live", 0, "ts", out,
                                                                                            sizeof(out)));
    CHECK(xtreamGuideUrl(&a, out, sizeof(out)) &&
          !strcmp(out, "http://tv.example:8080/xmltv.php?username=kai%20user&password=p%26ss%2Fword"));

    XtreamAuth auth;
    CHECK(xtreamParseAuth("{\"user_info\":{\"auth\":1,\"status\":\"Active\",\"exp_date\":\"1795132800\","
                          "\"max_connections\":\"2\"},\"server_info\":{}}", &auth) &&
          auth.active && auth.expires == 1795132800 && auth.max_connections == 2);
    CHECK(xtreamParseAuth("{\"user_info\":{\"auth\":1,\"status\":\"Expired\"}}", &auth) && !auth.active &&
          !strcmp(auth.status, "Expired"));
    CHECK(!xtreamParseAuth("{\"user_info\":[]}", &auth) && !xtreamParseAuth("<html>", &auth));

    IptvList list;
    memset(&list, 0, sizeof(list));
    CHECK(xtreamParseCategories("[{\"category_id\":\"3\",\"category_name\":\"News\"},"
                                "{\"category_id\":7,\"category_name\":\"Kids\"},{\"category_id\":\"9\"}]", &list) &&
          list.group_count == 2 && !strcmp(list.groups[1].id, "7"));
    CHECK(xtreamParseLive("[{\"num\":1,\"name\":\"One\",\"stream_id\":101,\"stream_icon\":\"https://l.example/1.png\","
                          "\"epg_channel_id\":\"one.example\",\"category_id\":\"3\"},"
                          "{\"num\":\"2\",\"name\":\"Two\",\"stream_id\":\"102\",\"category_id\":\"7\"},"
                          "{\"name\":\"No id\"},{\"stream_id\":5}]", &a, &list));
    CHECK(list.count == 2 && list.channels[0].group == 0 && list.channels[1].group == 1 &&
          list.groups[0].count == 1 && !strcmp(list.channels[0].epg_id, "one.example") &&
          !strcmp(list.channels[1].number, "2") &&
          !strcmp(list.channels[1].url, "http://tv.example:8080/live/kai%20user/p%26ss%2Fword/102.m3u8"));
    CHECK(!xtreamParseLive("{\"user_info\":{}}", &a, &list));
    iptvListFree(&list);
}

static const char GUIDE[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE tv SYSTEM \"xmltv.dtd\">\n"
    "<tv generator-info-name=\"made-up\">\n"
    "<channel id=\"one.example\"><display-name>One</display-name></channel>\n"
    "<programme start=\"20261122090000 +0000\" stop=\"20261122100000 +0000\" channel=\"one.example\">"
    "<title lang=\"en\">Morning &amp; News</title><title lang=\"es\">Mañana</title></programme>\n"
    "<programme start=\"20261122110000 +0100\" stop=\"20261122120000 +0100\" channel=\"one.example\">"
    "<title>Cooking</title><desc>Long description...</desc></programme>\n"
    "<programme start=\"20261121090000 +0000\" stop=\"20261121100000 +0000\" channel=\"one.example\">"
    "<title>Yesterday</title></programme>\n"
    "<programme start=\"20261122090000 +0000\" stop=\"20261122093000 +0000\" channel=\"elsewhere.example\">"
    "<title>Not in the list</title></programme>\n"
    "<programme start=\"20261122093000\" stop=\"20261122110000\" channel=\"two.example\">"
    "<title><![CDATA[Match <live>]]></title></programme>\n"
    "</tv>\n";

static const char *const IDS[] = {"one.example", "two.example", "", "one.example"};
#define NOV_22_0800 1795334400LL  // 2026-11-22 08:00 UTC.

static void checkGuide(XmltvReader *r)
{
    int count;
    const LiveProgram *p = xmltvPrograms(r, 0, &count);
    CHECK(count == 2 && p && !strcmp(p[0].title, "Morning & News") && p[0].start == NOV_22_0800 + 3600 &&
          !strcmp(p[1].title, "Cooking") && p[1].start == NOV_22_0800 + 2 * 3600);  // 11:00 +0100 is 10:00 UTC.
    p = xmltvPrograms(r, 1, &count);
    CHECK(count == 1 && !strcmp(p[0].title, "Match <live>"));
    CHECK(xmltvPrograms(r, 2, &count) == NULL && count == 0);
    p = xmltvPrograms(r, 3, &count);  // Shares the first channel's id.
    CHECK(count == 2);
    CHECK(xmltvMatched(r) == 3);
}

static void testGuide(void)
{
    CHECK(xmltvParseTime("20261122090000 +0000") == NOV_22_0800 + 3600);
    CHECK(xmltvParseTime("20261122100000 +0100") == NOV_22_0800 + 3600);
    CHECK(xmltvParseTime("20261122043000 -0430") == NOV_22_0800 + 3600);
    CHECK(xmltvParseTime("202611220900") == NOV_22_0800 + 3600);
    CHECK(xmltvParseTime("2026112") == 0 && xmltvParseTime("20261322090000") == 0 && xmltvParseTime(NULL) == 0);

    // Plain, fed a few bytes at a time (cuts land inside tags).
    XmltvReader *r = xmltvBegin(IDS, 4, NOV_22_0800, NOV_22_0800 + 24 * 3600);
    size_t length = strlen(GUIDE);
    for (size_t at = 0; at < length; at += 7)
        CHECK(xmltvFeed(r, GUIDE + at, length - at < 7 ? length - at : 7));
    checkGuide(r);
    xmltvFree(r);

    // Gzipped, in two members, fed in uneven pieces.
    unsigned char packed[4096];
    size_t packed_length = 0;
    size_t half = length / 2;
    for (int part = 0; part < 2; part++) {
        z_stream z;
        memset(&z, 0, sizeof(z));
        deflateInit2(&z, 6, Z_DEFLATED, 16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
        z.next_in = (Bytef *)(GUIDE + (part ? half : 0));
        z.avail_in = (uInt)(part ? length - half : half);
        z.next_out = packed + packed_length;
        z.avail_out = (uInt)(sizeof(packed) - packed_length);
        deflate(&z, Z_FINISH);
        packed_length = sizeof(packed) - z.avail_out;
        deflateEnd(&z);
    }
    r = xmltvBegin(IDS, 4, NOV_22_0800, NOV_22_0800 + 24 * 3600);
    for (size_t at = 0; at < packed_length; at += 13)
        CHECK(xmltvFeed(r, packed + at, packed_length - at < 13 ? packed_length - at : 13));
    checkGuide(r);
    xmltvFree(r);

    // A window that leaves the morning out; a channel with more programs than kept.
    r = xmltvBegin(IDS, 4, NOV_22_0800 + 2 * 3600 + 1, NOV_22_0800 + 24 * 3600);
    xmltvFeed(r, GUIDE, length);
    int count;
    const LiveProgram *p = xmltvPrograms(r, 0, &count);
    CHECK(count == 1 && !strcmp(p[0].title, "Cooking"));
    xmltvFree(r);
    char many[64 * 1024];
    size_t n = (size_t)snprintf(many, sizeof(many), "<tv>");
    for (int i = XMLTV_PER_CHANNEL + 5; i > 0; i--)  // Last first: the earliest are kept.
        n += (size_t)snprintf(many + n, sizeof(many) - n,
                              "<programme start=\"2026112210%02d00 +0000\" stop=\"2026112210%02d00 +0000\" "
                              "channel=\"one.example\"><title>P%d</title></programme>",
                              i * 2, i * 2 + 1, i);
    r = xmltvBegin(IDS, 4, NOV_22_0800, NOV_22_0800 + 24 * 3600);
    xmltvFeed(r, many, n);
    p = xmltvPrograms(r, 0, &count);
    CHECK(count == XMLTV_PER_CHANNEL && !strcmp(p[0].title, "P1") && p[1].start > p[0].start);
    xmltvFree(r);

    // A broken gzip stream says so; damaged text never reads past what it got.
    r = xmltvBegin(IDS, 4, 0, NOV_22_0800 * 2);
    unsigned char bad[64] = {0x1F, 0x8B, 8, 0, 0, 0, 0, 0, 0, 3, 0xFF, 0xFF, 0xFF};
    CHECK(!xmltvFeed(r, bad, sizeof(bad)) || !xmltvFeed(r, bad, sizeof(bad)));
    xmltvFree(r);
    char *copy = malloc(length);
    srand(11);
    for (int round = 0; copy && round < 2000; round++) {
        memcpy(copy, GUIDE, length);
        for (int k = 0; k < 6; k++)
            copy[rand() % length] = "<>/\"&;= x"[rand() % 9];
        r = xmltvBegin(IDS, 4, 0, NOV_22_0800 * 2);
        xmltvFeed(r, copy, (size_t)(rand() % (int)length) + 1);
        for (int c = 0; c < 4; c++) {
            xmltvPrograms(r, c, &count);
            CHECK(count >= 0 && count <= XMLTV_PER_CHANNEL);
        }
        xmltvFree(r);
    }
    free(copy);
}

static void testAccount(void)
{
    IptvAccount a = {IPTV_XTREAM, "http://tv.example:8080", "kai", "s3cr=t+/x", "", false};
    char text[2048];
    CHECK(iptvAccountWrite(&a, text, sizeof(text)) && !strstr(text, "s3cr=t") && strstr(text, "password=b64:"));
    IptvAccount back;
    CHECK(iptvAccountRead(&back, text) && back.kind == IPTV_XTREAM && !strcmp(back.password, "s3cr=t+/x") &&
          !strcmp(back.address, "http://tv.example:8080") && !strcmp(back.user, "kai") && !back.any_certificate);
    a.any_certificate = true;  // Certificate check off: kept.
    CHECK(iptvAccountWrite(&a, text, sizeof(text)) && strstr(text, "certificates=off") &&
          iptvAccountRead(&back, text) && back.any_certificate);
    a.any_certificate = false;
    char guide[IPTV_URL_MAX];
    iptvGuideUrl(&back, "https://ignored.example/epg.xml", guide, sizeof(guide));
    CHECK(!strcmp(guide, "http://tv.example:8080/xmltv.php?username=kai&password=s3cr%3Dt%2B%2Fx"));
    IptvAccount m3u = {IPTV_M3U, "https://lists.example/tv.m3u", "", "", "", false};
    iptvGuideUrl(&m3u, "https://guide.example/epg.xml.gz", guide, sizeof(guide));
    CHECK(!strcmp(guide, "https://guide.example/epg.xml.gz"));
    snprintf(m3u.guide, sizeof(m3u.guide), "%s", "https://mine.example/g.xml");
    iptvGuideUrl(&m3u, "https://guide.example/epg.xml.gz", guide, sizeof(guide));
    CHECK(!strcmp(guide, "https://mine.example/g.xml"));
    CHECK(!iptvAccountRead(&back, "kind=m3u\naddress=file:///etc/x\n") && back.kind == IPTV_NONE);
    CHECK(!iptvAccountRead(&back, "nothing") && !iptvAccountRead(&back, "kind=other\naddress=https://x.example\n"));
    IptvAccount bad = {IPTV_M3U, "https://x.example/a\nkind=xtream", "", "", "", false};
    CHECK(!iptvAccountWrite(&bad, text, sizeof(text)));
}

static void testStreamHeaders(void)
{
    StreamHeaders h;
    memset(&h, 0, sizeof(h));
    CHECK(!streamHeadersAny(&h) && !streamHeadersAny(NULL));
    CHECK(streamHeadersSet(&h, "REFERRER", "https://a.example/") && !strcmp(h.referer, "https://a.example/"));
    CHECK(!streamHeadersSet(&h, "Cookie", "x=1") && !streamHeadersSet(&h, "Authorization", "Bearer x"));
    CHECK(!streamHeadersSet(&h, "User-Agent", "") && !streamHeadersSet(&h, "User-Agent", NULL));
    CHECK(!streamHeadersSet(&h, "User-Agent", "a\nb") && !streamHeadersSet(&h, "Origin", "a\rb"));
    CHECK(!streamHeadersSet(&h, "Origin", "caf\xC3\xA9") && !h.origin[0]);  // Not ASCII.
    char longest[STREAM_HEADER_MAX + 1];
    memset(longest, 'a', sizeof(longest) - 1);
    longest[sizeof(longest) - 1] = '\0';
    CHECK(!streamHeadersSet(&h, "User-Agent", longest));
    longest[STREAM_HEADER_MAX - 1] = '\0';
    CHECK(streamHeadersSet(&h, "User-Agent", longest));

    char names[64];
    memset(&h, 0, sizeof(h));
    streamHeadersNames(&h, names, sizeof(names));
    CHECK(!strcmp(names, ""));
    CHECK(streamHeadersSet(&h, "Origin", "https://o.example"));
    streamHeadersNames(&h, names, sizeof(names));
    CHECK(!strcmp(names, "Origin"));
    CHECK(streamHeadersSet(&h, "Referer", "https://r.example/"));
    streamHeadersNames(&h, names, sizeof(names));
    CHECK(!strcmp(names, "Referer, Origin"));
    CHECK(streamHeadersSet(&h, "user-agent", "UA+1"));
    streamHeadersNames(&h, names, sizeof(names));
    CHECK(!strcmp(names, "Referer, User-Agent, Origin") && !strstr(names, "r.example"));

    memset(&h, 0, sizeof(h));
    const char *opt = "#extvlcopt: http-user-agent = Thing/1.0 ";
    CHECK(streamHeadersFromVlcOpt(&h, opt, strlen(opt)) && !strcmp(h.user_agent, "Thing/1.0"));
    opt = "#EXTVLCOPT:http-referer=https://r.example/";  // One "r", as VLC also takes.
    CHECK(streamHeadersFromVlcOpt(&h, opt, strlen(opt)) && !strcmp(h.referer, "https://r.example/"));
    opt = "#EXTVLCOPT:network-caching=1000";
    CHECK(!streamHeadersFromVlcOpt(&h, opt, strlen(opt)));
    CHECK(!streamHeadersFromVlcOpt(&h, "#EXTVLCOPT:", 11) && !streamHeadersFromVlcOpt(&h, "#EXTINF:-1", 10));

    memset(&h, 0, sizeof(h));
    char url[256] = "https://s.example/x.m3u8";
    CHECK(!streamHeadersFromPipe(&h, url) && !strcmp(url, "https://s.example/x.m3u8"));
    snprintf(url, sizeof(url), "https://s.example/x.m3u8 |User-Agent=A+B%%2FC&&=x&Origin&Referer=");
    CHECK(streamHeadersFromPipe(&h, url) && !strcmp(url, "https://s.example/x.m3u8"));
    CHECK(!strcmp(h.user_agent, "A+B/C") && !h.origin[0] && !h.referer[0]);
    snprintf(url, sizeof(url), "https://s.example/y|Referer=%%zz%%4");  // Bad escapes stay as they are.
    CHECK(streamHeadersFromPipe(&h, url) && !strcmp(h.referer, "%zz%4"));
}

void testIptv(void)
{
    testStreamHeaders();
    testAccount();
    testM3u();
    testXtream();
    testGuide();
}
