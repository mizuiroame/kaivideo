// Changing sound track on a converted video, on a PC: the server's conversion
// of one track alone (hlsio's sound session), opened and sought as audio.c
// does when it joins the picture, and what packets come.
//   host_swap URL AUTHORIZATION START_S
#include <stdio.h>
#include <stdlib.h>

#include <libavformat/avformat.h>

#include "hlsio.h"
#include "net.h"

#define NS_PER_S 1000000000ll
#define JOIN_AHEAD_S 2
#define PACKETS 40

unsigned mediaFileInterrupts(void)
{
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "host_swap URL AUTHORIZATION START_S\n");
        return 2;
    }
    int64_t start_ns = atoll(argv[3]) * NS_PER_S;
    char error[160];
    if (!hlsStartAudio(argv[1], argv[2], start_ns, NULL, error, sizeof(error))) {
        printf("FAIL start: %s\n", error);
        NetClient *net = netClientOpen();
        NetRequest request = {.url = argv[1], .authorization = argv[2], .timeout_s = 30};
        NetResponse r;
        bool ok = netFetchText(net, &request, 8u << 20, &r);
        printf("  the list alone: %s, status %ld (%s), %zu bytes\n", ok ? "read" : "not read", (long)r.status,
               r.error, ok ? r.size : 0);
        if (ok)
            printf("%.300s\n", r.data);
        return 1;
    }
    AVFormatContext *fmt = avformat_alloc_context();
    fmt->codec_whitelist = av_strdup(getenv("WHITELIST") ? getenv("WHITELIST") : "");  // As audio.c's, if given.
    if (!getenv("WHITELIST"))
        av_freep(&fmt->codec_whitelist);
    AVIOContext *playlist = NULL;
    if (!hlsOpenInput(&fmt, &playlist, HLS_AUDIO_LOCATION) || avformat_find_stream_info(fmt, NULL) < 0) {
        printf("FAIL open\n");
        return 1;
    }
    int video = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    int audio = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    int64_t skipped_us = hlsLocationBaseNs(HLS_AUDIO_LOCATION) / 1000;
    const AVStream *vs = video >= 0 ? fmt->streams[video] : NULL;
    int64_t first_us = vs && vs->start_time != AV_NOPTS_VALUE ? av_rescale_q(vs->start_time, vs->time_base, AV_TIME_BASE_Q)
                                                                : fmt->start_time;
    int64_t start_us = first_us - skipped_us;
    printf("video %d audio %d; list from %.2f s; stream starts %.2f s (picture), %.2f s (earliest)\n", video, audio,
           skipped_us / 1e6, first_us / 1e6, fmt->start_time / 1e6);
    // As audio.c: seek on the picture's stream to a little ahead.
    int64_t target_ns = start_ns + JOIN_AHEAD_S * NS_PER_S;
    int seek_stream = video >= 0 ? video : audio;
    AVRational tb = fmt->streams[seek_stream]->time_base;
    int64_t ts = av_rescale_q(target_ns, (AVRational){1, 1000000000}, tb) + av_rescale_q(start_us, AV_TIME_BASE_Q, tb);
    int r = av_seek_frame(fmt, seek_stream, ts, AVSEEK_FLAG_BACKWARD);
    printf("seek to %.2f s on the player's clock: %d\n", target_ns / 1e9, r);
    AVPacket *pkt = av_packet_alloc();
    int audio_packets = 0;
    fprintf(stderr, "reading\n");
    for (int n = 0; n < PACKETS * 4 && audio_packets < PACKETS && av_read_frame(fmt, pkt) >= 0; n++) {
        fprintf(stderr, "  packet %d of stream %d\n", n, pkt->stream_index);
        if (pkt->stream_index == audio) {
            double t = (pkt->pts - av_rescale_q(start_us, AV_TIME_BASE_Q, fmt->streams[audio]->time_base)) *
                       av_q2d(fmt->streams[audio]->time_base);
            if (audio_packets < 3 || audio_packets == PACKETS - 1)
                printf("  sound packet at %.2f s on the player's clock\n", t);
            audio_packets++;
        }
        av_packet_unref(pkt);
    }
    printf("%d sound packets\n", audio_packets);
    av_packet_free(&pkt);
    avformat_close_input(&fmt);
    hlsCloseInput(playlist);
    hlsStopAudio();
    return 0;
}
