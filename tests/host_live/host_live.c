// Reads a Live TV channel's own HLS source on a PC through hlsio, as the
// console does, and says what FFmpeg finds in it: its streams (with the
// sound tracks and subtitles of their own lists) and some packets of each.
//   host_live URL [SECONDS]
// Personal testing only: never put a channel's address in the repository.
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <libavformat/avformat.h>

#include "hlsio.h"
#include "subs.h"
#include "webvtt.h"

#define DEFAULT_SECONDS 20
#define NS_PER_S 1000000000ll

unsigned mediaFileInterrupts(void)
{
    return 0;
}

static int64_t nowNs(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * NS_PER_S + t.tv_nsec;
}

static const char *tag(const AVStream *st, const char *key)
{
    const AVDictionaryEntry *e = av_dict_get(st->metadata, key, NULL, 0);
    return e ? e->value : "-";
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "host_live URL [SECONDS]\n");
        return 2;
    }
    int seconds = argc > 2 ? atoi(argv[2]) : DEFAULT_SECONDS;
    char error[160];
    if (!hlsStartDirect(argv[1], 0, NULL, error, sizeof(error))) {
        printf("FAIL start: %s\n", error);
        return 1;
    }
    AVFormatContext *fmt = avformat_alloc_context();
    AVIOContext *playlist = NULL;
    if (!hlsOpenInput(&fmt, &playlist) || avformat_find_stream_info(fmt, NULL) < 0) {
        printf("FAIL open\n");
        return 1;
    }
    for (unsigned i = 0; i < fmt->nb_streams; i++) {
        const AVStream *st = fmt->streams[i];
        printf("stream %u: %s %s language=%s comment=%s title=%s default=%d\n", i,
               av_get_media_type_string(st->codecpar->codec_type), avcodec_get_name(st->codecpar->codec_id),
               tag(st, "language"), tag(st, "comment"), tag(st, "title"),
               (st->disposition & AV_DISPOSITION_DEFAULT) != 0);
    }
    printf("best sound: stream %d\n", av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0));
    for (int i = 0; i < hlsSubtitleCount(); i++)
        printf("subtitles %d: %s (%s)\n", i, hlsSubtitleName(i), hlsSubtitleLanguage(i));
    hlsSelectSubtitle(0);
    SubTrack subs = {0};
    int64_t shift_ns = fmt->start_time * 1000;  // As the player's clock: 0 at the stream's start.
    int64_t last_video_ns = 0;
    int counts[16] = {0};
    int64_t first_pts[16];
    for (int i = 0; i < 16; i++)
        first_pts[i] = AV_NOPTS_VALUE;
    int64_t until = nowNs() + seconds * NS_PER_S;
    AVPacket *pkt = av_packet_alloc();
    while (nowNs() < until && av_read_frame(fmt, pkt) >= 0) {
        int s = pkt->stream_index < 16 ? pkt->stream_index : 15;
        const AVStream *st = fmt->streams[pkt->stream_index];
        double t = pkt->pts == AV_NOPTS_VALUE ? -1 : pkt->pts * av_q2d(st->time_base);
        if (first_pts[s] == AV_NOPTS_VALUE)
            first_pts[s] = pkt->pts;
        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && pkt->pts != AV_NOPTS_VALUE)
            last_video_ns = (int64_t)(t * 1e9) - shift_ns;
        char *data;
        size_t size;
        while (hlsTakeSubtitle(&data, &size)) {
            printf("  subtitle segment: %d lines\n", webvttAdd(&subs, data, size, shift_ns));
            free(data);
        }
        if (st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE && counts[s] < 6)
            printf("  subtitle %d at %.2f s: %.*s\n", s, t, pkt->size > 60 ? 60 : pkt->size, (const char *)pkt->data);
        counts[s]++;
        av_packet_unref(pkt);
    }
    for (unsigned i = 0; i < fmt->nb_streams && i < 16; i++) {
        const AVStream *st = fmt->streams[i];
        printf("stream %u: %d packets, first at %.2f s\n", i, counts[i],
               first_pts[i] == AV_NOPTS_VALUE ? -1.0 : first_pts[i] * av_q2d(st->time_base));
    }
    printf("picture at %.2f s on the player's clock; %zu subtitle lines\n", last_video_ns / 1e9, subs.count);
    for (size_t i = 0; i < subs.count && i < 4; i++)
        printf("  %.2f-%.2f s: %s\n", subs.cues[i].start_ns / 1e9, subs.cues[i].end_ns / 1e9, subs.cues[i].text);
    subsFree(&subs);
    av_packet_free(&pkt);
    avformat_close_input(&fmt);
    hlsCloseInput(playlist);
    hlsStop();
    return 0;
}
