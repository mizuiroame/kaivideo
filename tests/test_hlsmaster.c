// A Live TV channel's own master list: its other sound tracks and subtitles,
// and the lists KaiVideo hands FFmpeg for them.
#include <stdio.h>
#include <string.h>

#include "check.h"
#include "hlslist.h"
#include "hlsmaster.h"
#include "subs.h"
#include "webvtt.h"

#define BASE "https://tv.example/live/main.m3u8"

static const char MASTER[] =
    "#EXTM3U\n"
    "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"audios\",NAME=\"Castellano\",LANGUAGE=\"spa\",CHANNELS=\"2\",DEFAULT=YES,"
    "AUTOSELECT=YES\n"
    "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"audios\",NAME=\"Original\",LANGUAGE=\"qaa\",DEFAULT=NO,"
    "URI=\"main_a_14.m3u8\"\n"
    "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"other\",NAME=\"Elsewhere\",URI=\"x.m3u8\"\n"
    "#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"subs\",NAME=\"Espa\xC3\xB1ol\",LANGUAGE=\"es\",DEFAULT=NO,"
    "URI=\"https://tv.example/live/main_es.m3u8\"\n"
    "#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"subs\",NAME=\"Ingl\xC3\xA9s\",LANGUAGE=\"en\",URI=\"main_en.m3u8\"\n"
    "#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=3012608,RESOLUTION=1280x720,CODECS=\"avc1.640029,mp4a.40.2\","
    "AUDIO=\"audios\",SUBTITLES=\"subs\"\n"
    "main_720.m3u8\n";

static void testMasterParse(void)
{
    HlsMaster m;
    CHECK(hlsMasterParse(MASTER, BASE, 0, &m));
    CHECK(!strcmp(m.variant_url, "https://tv.example/live/main_720.m3u8") && m.bandwidth == 3012608);
    CHECK(!strcmp(m.codecs, "avc1.640029,mp4a.40.2"));
    // Only the variant's own groups; the sound in the variant itself has no list.
    CHECK(m.count == 4);
    CHECK(m.renditions[0].type == HLS_RENDITION_AUDIO && !strcmp(m.renditions[0].name, "Castellano") &&
          m.renditions[0].list == 0 && m.renditions[0].is_default && !strcmp(m.renditions[0].language, "spa"));
    CHECK(m.renditions[1].list == 1 && !strcmp(m.renditions[1].url, "https://tv.example/live/main_a_14.m3u8"));
    CHECK(m.renditions[2].type == HLS_RENDITION_SUBTITLES && m.renditions[2].list == 2 &&
          !strcmp(m.renditions[2].name, "Espa\xC3\xB1ol") && !strcmp(m.renditions[2].url, "https://tv.example/live/main_es.m3u8"));
    CHECK(m.renditions[3].list == 3 && !strcmp(m.renditions[3].language, "en"));
    CHECK(m.list_count == 4);  // The variant and three lists of its own.
    CHECK(!strcmp(hlsMasterListUrl(&m, 0), m.variant_url) && !strcmp(hlsMasterListUrl(&m, 3), m.renditions[3].url));
    CHECK(hlsMasterListIsSubtitles(&m, 2) && !hlsMasterListIsSubtitles(&m, 1) && !hlsMasterListIsSubtitles(&m, 0));

    // The master list FFmpeg reads: local names for each list.
    char text[4096];
    CHECK(hlsLocalMaster(&m, text, sizeof(text)));
    CHECK(strstr(text, "TYPE=AUDIO,GROUP-ID=\"a\",NAME=\"Castellano\",LANGUAGE=\"spa\",DEFAULT=YES,AUTOSELECT=YES\n"));
    CHECK(strstr(text, "NAME=\"Original\",LANGUAGE=\"qaa\",DEFAULT=NO,AUTOSELECT=YES,URI=\"" HLS_LOCAL_PREFIX "1/index.m3u8\""));
    CHECK(strstr(text, "TYPE=SUBTITLES,GROUP-ID=\"s\",NAME=\"Espa\xC3\xB1ol\",LANGUAGE=\"es\",DEFAULT=NO,AUTOSELECT=NO,"
                       "URI=\"" HLS_LOCAL_PREFIX "2/index.m3u8\""));
    CHECK(strstr(text, "#EXT-X-STREAM-INF:BANDWIDTH=3012608,CODECS=\"avc1.640029,mp4a.40.2\",AUDIO=\"a\",SUBTITLES=\"s\"\n"
                       HLS_LOCAL_PREFIX "0/index.m3u8\n"));
    CHECK(!strstr(text, "tv.example") && !strstr(text, "Elsewhere"));
    CHECK(!hlsLocalMaster(&m, text, 64));  // No room: false, not cut short.
}

static void testMasterPlain(void)
{
    HlsMaster m;
    // No groups: the variant alone, with no master list of its own needed.
    CHECK(hlsMasterParse("#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=4164200,RESOLUTION=1920x1080\nchunklist.m3u8\n",
                         "https://h/live/playlist.m3u8", 0, &m));
    CHECK(m.count == 0 && m.list_count == 1 && !strcmp(m.variant_url, "https://h/live/chunklist.m3u8"));
    CHECK(!hlsMasterParse("#EXTM3U\n#EXTINF:6.0,\na.ts\n", "https://h/a.m3u8", 0, &m));  // A media list.
    // A name with a quote can't break the line it goes in.
    CHECK(hlsMasterParse("#EXTM3U\n#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"g\",NAME=\"A, \\\"B\\\"\",URI=\"b.m3u8\"\n"
                         "#EXT-X-STREAM-INF:BANDWIDTH=1,AUDIO=\"g\"\nv.m3u8\n", "https://h/m.m3u8", 0, &m));
    CHECK(m.count == 1 && !strchr(m.renditions[0].name, '"'));
}

static void testListNames(void)
{
    static const char LIST[] = "#EXTM3U\n#EXT-X-MEDIA-SEQUENCE:7\n#EXTINF:5.0,\nhttps://s/a.vtt\n#EXTINF:5.0,\nb.vtt\n";
    HlsPlaylist p;
    CHECK(hlsPlaylistParseList(LIST, "https://s/subs.m3u8", 0, 2, true, &p));
    CHECK(strstr(p.text, HLS_LOCAL_PREFIX "2/0.vtt\n") && strstr(p.text, HLS_LOCAL_PREFIX "2/1.vtt\n"));
    CHECK(hlsPlaylistAppend(&p, "#EXTM3U\n#EXT-X-MEDIA-SEQUENCE:8\n#EXTINF:5.0,\nb.vtt\n#EXTINF:5.0,\nc.vtt\n",
                            "https://s/subs.m3u8") == 1);
    CHECK(strstr(p.text, HLS_LOCAL_PREFIX "2/2.vtt\n") && !strcmp(p.urls[2], "https://s/c.vtt"));
    hlsPlaylistFree(&p);

    int list = -1, index = -1;
    CHECK(hlsLocalName(HLS_LOCAL_PREFIX "2/5.vtt", &list, &index) && list == 2 && index == 5);
    CHECK(hlsLocalName(HLS_LOCAL_PREFIX "0/12.ts", &list, &index) && list == 0 && index == 12);
    CHECK(hlsLocalName(HLS_LOCAL_PREFIX "12.ts", &list, &index) && list == 0 && index == 12);  // One list alone.
    CHECK(hlsLocalName(HLS_LOCAL_PREFIX "3/index.m3u8", &list, &index) && list == 3 && index == -1);
    CHECK(!hlsLocalName(HLS_LOCAL_PREFIX "x.ts", &list, &index) && !hlsLocalName("https://h/1.ts", &list, &index));
    CHECK(!hlsLocalName(HLS_LOCAL_PREFIX "1/2.mp4", &list, &index));
    // fMP4: the initialization section's local name.
    CHECK(hlsLocalName(HLS_LOCAL_PREFIX "init.mp4", &list, &index) && list == 0 && index == HLS_INIT_INDEX);
    CHECK(hlsLocalName(HLS_LOCAL_PREFIX "1/init.mp4", &list, &index) && list == 1 && index == HLS_INIT_INDEX);
}

// An fMP4 list (CMAF): its #EXT-X-MAP becomes a name KaiVideo serves, its address kept.
static void testFmp4(void)
{
    static const char LIST[] = "#EXTM3U\n#EXT-X-VERSION:6\n#EXT-X-MEDIA-SEQUENCE:46311\n"
                               "#EXT-X-MAP:URI=\"header_vo.cmfv\"\n#EXTINF:6.006,\nmedia_46311.cmfv\n"
                               "#EXTINF:6.006,\nmedia_46312.cmfv\n";
    HlsPlaylist p;
    CHECK(hlsPlaylistParse(LIST, "https://s.example/ontv/chunklist_vo.m3u8", 0, &p));
    CHECK(p.map_url && !strcmp(p.map_url, "https://s.example/ontv/header_vo.cmfv"));
    CHECK(strstr(p.text, "#EXT-X-MAP:URI=\"" HLS_LOCAL_PREFIX "init.mp4\"\n") && !strstr(p.text, "header_vo"));
    CHECK(p.count == 2 && !strcmp(p.urls[1], "https://s.example/ontv/media_46312.cmfv"));
    // Read again while live: the rebuilt text keeps it.
    CHECK(hlsPlaylistAppend(&p, "#EXTM3U\n#EXT-X-MEDIA-SEQUENCE:46312\n#EXT-X-MAP:URI=\"header_vo.cmfv\"\n"
                                "#EXTINF:6.006,\nmedia_46312.cmfv\n#EXTINF:6.006,\nmedia_46313.cmfv\n",
                            "https://s.example/ontv/chunklist_vo.m3u8") == 1);
    CHECK(strstr(p.text, "#EXT-X-MAP:URI=\"" HLS_LOCAL_PREFIX "init.mp4\"\n") != NULL);
    hlsPlaylistFree(&p);
    CHECK(hlsPlaylistParseList(LIST, "https://s.example/a.m3u8", 0, 1, false, &p) &&
          strstr(p.text, "#EXT-X-MAP:URI=\"" HLS_LOCAL_PREFIX "1/init.mp4\"\n"));
    hlsPlaylistFree(&p);
    static const char PLAIN[] = "#EXTM3U\n#EXTINF:5.0,\na.ts\n";
    CHECK(hlsPlaylistParse(PLAIN, "https://s.example/a.m3u8", 0, &p) && p.map_url == NULL &&
          !strstr(p.text, "EXT-X-MAP"));
    hlsPlaylistFree(&p);
}

static void testWebVtt(void)
{
    // As a channel sends them: cue times on the MPEG-TS clock (here MPEGTS:0).
    static const char SEGMENT[] =
        "WEBVTT\nX-TIMESTAMP-MAP=LOCAL:00:00:00.000,MPEGTS:0\n\n"
        "14:00:31.883 --> 14:00:35.347\nse\xC3\xB1" "ales de alerta\ndif\xC3\xAD" "ciles de detectar.\n\n"
        "cue-2\n14:00:35.400 --> 14:00:37.000 line:90%\n<i>Otra</i> l\xC3\xADnea\n";
    const int64_t at = (14 * 3600 + 31) * 1000000000ll;  // 14:00:31 is 50431 s.
    SubTrack t = {0};
    CHECK(webvttAdd(&t, SEGMENT, sizeof(SEGMENT) - 1, 50000 * 1000000000ll) == 2);
    char text[256];
    // Moved back by shift: 431.883 s on the player's clock.
    CHECK(subsAt(&t, at + 883000000ll + 1 - 50000 * 1000000000ll, text, sizeof(text)) &&
          !strcmp(text, "se\xC3\xB1" "ales de alerta\ndif\xC3\xAD" "ciles de detectar."));
    CHECK(subsAt(&t, (14 * 3600 + 36) * 1000000000ll - 50000 * 1000000000ll, text, sizeof(text)) &&
          !strcmp(text, "Otra l\xC3\xAD" "nea"));  // Tags out, settings after the time ignored.
    // The same segment again (the list was read again) adds nothing twice.
    webvttAdd(&t, SEGMENT, sizeof(SEGMENT) - 1, 50000 * 1000000000ll);
    CHECK(t.count == 2);
    subsFree(&t);

    // A map onto a stream that starts later, minutes without hours, CRLF line breaks.
    static const char MAPPED[] = "WEBVTT\r\nX-TIMESTAMP-MAP=MPEGTS:900000,LOCAL:00:00:02.000\r\n\r\n"
                                 "00:03.000 --> 00:04.500\r\nHola\r\n";
    CHECK(webvttAdd(&t, MAPPED, sizeof(MAPPED) - 1, 0) == 1);
    CHECK(t.cues[0].start_ns == 11000000000ll && t.cues[0].end_ns == 12500000000ll);  // 10 s - 2 s + 3 s.
    subsFree(&t);
    CHECK(webvttAdd(&t, "1\n00:00:01,000 --> 00:00:02,000\nSRT\n", 36, 0) == -1);  // Not WebVTT.
    CHECK(webvttAdd(&t, "WEBVTT\n\n", 8, 0) == 0);
}

void testHlsMaster(void)
{
    testMasterParse();
    testMasterPlain();
    testListNames();
    testFmp4();
    testWebVtt();
}
