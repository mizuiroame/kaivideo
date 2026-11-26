// What host_video needs besides video.c, for files on the PC only: no
// server, HLS or network folder, and a FILE* reader (FFmpeg in the image has
// no protocols). Built in the kaivideo-ass-host image (FFmpeg 7.1 and libass):
//   gcc -O2 -Itests/host_video -Isource -Ithird_party -I/opt/ff/include tests/host_video/host_video.c //       tests/host_video/host_stubs.c source/video.c source/swframe.c source/hdr.c source/asssubs.c //       source/asscompose.c source/lang.c source/picsubs.c source/picsubsff.c source/vobsub.c source/playback.c //       source/subs.c source/util.c -L/opt/ff/lib -lavformat -lavcodec -lavutil -lass -lm -lpthread -lz //       -o build/host_video
#include <libavformat/avformat.h>
#include <stdio.h>
#include <string.h>
#include "mediafile.h"
bool hlsIsLocation(const char *p) { (void)p; return false; }
bool jfIsRemoteLocation(const char *p) { (void)p; return false; }
bool mediaFileFailed(const MediaFile *f) { (void)f; return false; }
static int readFile(void *o, uint8_t *b, int n) { size_t r = fread(b, 1, (size_t)n, o); return r ? (int)r : AVERROR_EOF; }
static int64_t seekFile(void *o, int64_t off, int whence)
{
    FILE *f = o;
    if (whence == AVSEEK_SIZE) { long at = ftell(f); fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, at, SEEK_SET); return size; }
    return fseek(f, (long)off, whence & 3) == 0 ? ftell(f) : -1;
}
bool mediaInputOpen(MediaInput *in, AVFormatContext **fmt, const char *location, int buffer_size)
{
    memset(in, 0, sizeof(*in));
    FILE *f = fopen(location, "rb");
    if (!f) return false;
    AVIOContext *io = avio_alloc_context(av_malloc(buffer_size), buffer_size, 0, f, readFile, NULL, seekFile);
    (*fmt)->pb = io;
    return avformat_open_input(fmt, NULL, NULL, NULL) == 0 && avformat_find_stream_info(*fmt, NULL) >= 0;
}
void mediaInputClose(MediaInput *in) { (void)in; }
