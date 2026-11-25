// Tries every channel of an M3U list on a PC the way KaiVideo opens an IPTV
// channel (hlsStartDirect, then FFmpeg through hlsio), and says which open
// and what's in them.
//   host_iptv LIST.m3u [FIRST [COUNT]]
// Built in the kaivideo-host image (FFmpeg, curl and jansson for the PC):
//   docker run --rm -v "$PWD:/w" -w /w kaivideo-host sh -c "gcc -O1 -DKAIVIDEO_VERSION='\\"test\\"' -Isource
//     -Ithird_party tests/host_live/host_iptv.c source/hlsio.c source/tsstream.c source/iptv.c source/net.c
//     source/hlslist.c source/hlsmaster.c source/certtrust.c source/lang.c source/streamheaders.c source/util.c
//     -o /tmp/host_iptv -lavformat -lavcodec -lavutil -lcurl -ljansson -lpthread -lm && /tmp/host_iptv LIST.m3u"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavformat/avformat.h>

#include "hlsio.h"
#include "iptv.h"
#include "tsstream.h"

unsigned mediaFileInterrupts(void)
{
    return 0;
}

// As main.c's openIptv: HLS first for an .m3u8 address, a stream sent as it is first for the rest,
// with the headers the list asks for (#EXTVLCOPT).
static const char *startChannel(const IptvChannel *c, char *error, size_t size)
{
    bool hls_first = strstr(c->url, ".m3u8") != NULL;
    if (!hls_first) {
        TsStart ts = tsStreamStart(c->url, false, &c->headers, NULL, error, size);
        if (ts == TS_STARTED)
            return TS_LOCATION;
        if (ts == TS_FAILED)
            return NULL;
    }
    if (hlsStartDirectAny(c->url, 0, false, &c->headers, NULL, error, size))
        return HLS_LOCATION;
    if (hls_first && hlsDirectWasNotHls() && tsStreamStart(c->url, false, &c->headers, NULL, error, size) == TS_STARTED)
        return TS_LOCATION;
    return NULL;
}

static bool tryChannel(const IptvChannel *c, char *what, size_t size)
{
    char error[256] = "";
    const char *location = startChannel(c, error, sizeof(error));
    if (!location) {
        snprintf(what, size, "start: %s", error);
        return false;
    }
    bool stream = !strcmp(location, TS_LOCATION);
    AVFormatContext *fmt = avformat_alloc_context();
    AVIOContext *playlist = NULL;
    bool ok = stream ? tsStreamOpenInput(&fmt, &playlist) : hlsOpenInput(&fmt, &playlist, HLS_LOCATION);
    if (!ok) {
        snprintf(what, size, "open failed");
    } else {
        int rc = avformat_find_stream_info(fmt, NULL);
        size_t n = (size_t)snprintf(what, size, "info %d:", rc);
        for (unsigned i = 0; i < fmt->nb_streams && n < size; i++) {
            const AVCodecParameters *p = fmt->streams[i]->codecpar;
            n += (size_t)snprintf(what + n, size - n, " %s/%s%s", av_get_media_type_string(p->codec_type),
                                  avcodec_get_name(p->codec_id), p->width ? "" : "");
            if (p->width && n < size)
                n += (size_t)snprintf(what + n, size - n, " %dx%d", p->width, p->height);
        }
        ok = rc >= 0 && fmt->nb_streams > 0;
        if (stream && n < size)
            snprintf(what + n, size - n, " (MPEG-TS sent as it is)");
        avformat_close_input(&fmt);
        if (stream)
            tsStreamCloseInput(playlist);
        else
            hlsCloseInput(playlist);
    }
    hlsStop();  // The stream too.
    return ok;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "%s LIST.m3u [FIRST [COUNT]]  or  %s URL (one channel, FFmpeg's log on)\n", argv[0], argv[0]);
        return 2;
    }
    if (!strncmp(argv[1], "http", 4)) {
        IptvChannel one;
        memset(&one, 0, sizeof(one));
        snprintf(one.url, sizeof(one.url), "%s", argv[1]);
        av_log_set_level(AV_LOG_VERBOSE);
        char what[512];
        bool good = tryChannel(&one, what, sizeof(what));
        printf("%s %s\n", good ? "OK" : "FAIL", what);
        return good ? 0 : 1;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f)
        return 1;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *text = malloc((size_t)size + 1);
    fread(text, 1, (size_t)size, f);
    fclose(f);
    IptvList list;
    if (!iptvParseM3u(text, (size_t)size, &list))
        return 1;
    int first = argc > 2 ? atoi(argv[2]) : 0, count = argc > 3 ? atoi(argv[3]) : list.count;
    av_log_set_level(AV_LOG_ERROR);
    int ok = 0, tried = 0;
    for (int i = first; i < list.count && i < first + count; i++) {
        char what[512];
        bool good = tryChannel(&list.channels[i], what, sizeof(what));
        ok += good;
        tried++;
        printf("%s %3d %-34.34s %s | %s\n", good ? "OK  " : "FAIL", i, list.channels[i].name, what,
               list.channels[i].url);
        fflush(stdout);
    }
    printf("%d of %d open\n", ok, tried);
    return 0;
}
