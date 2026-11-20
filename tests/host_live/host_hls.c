// Opens a server's conversion on a PC through hlsio and FFmpeg, as the player
// does, and says what FFmpeg finds in it.
//   host_hls URL [AUTHORIZATION]
#include <stdio.h>
#include <stdlib.h>

#include <libavformat/avformat.h>

#include "hlsio.h"

unsigned mediaFileInterrupts(void)
{
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "%s URL [AUTHORIZATION]\n", argv[0]);
        return 2;
    }
    char error[256] = "";
    if (!hlsStart(argv[1], argc > 2 ? argv[2] : NULL, 0, NULL, error, sizeof(error))) {
        printf("hlsStart failed: %s\n", error);
        return 1;
    }
    printf("hlsStart ok\n");
    AVFormatContext *fmt = avformat_alloc_context();
    AVIOContext *playlist = NULL;
    av_log_set_level(AV_LOG_WARNING);
    if (!hlsOpenInput(&fmt, &playlist, HLS_LOCATION)) {
        printf("hlsOpenInput failed\n");
        return 1;
    }
    int rc = avformat_find_stream_info(fmt, NULL);
    printf("find_stream_info: %d, %u streams\n", rc, fmt->nb_streams);
    for (unsigned i = 0; i < fmt->nb_streams; i++) {
        const AVCodecParameters *p = fmt->streams[i]->codecpar;
        printf("  %u: %s %s %dx%d\n", i, av_get_media_type_string(p->codec_type), avcodec_get_name(p->codec_id),
               p->width, p->height);
    }
    avformat_close_input(&fmt);
    hlsCloseInput(playlist);
    hlsStop();
    return 0;
}
