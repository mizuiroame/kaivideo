#include "seekbench.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/mem.h>

#define IO_BUFFER (256 * 1024)
#define NS_PER_MS 1e6
#define MAX_SEEK_MS 30000.0  // A seek that takes longer stops counting.
#define INDEX_SEEK_FRACTION 2

// Where the seeks land, as fractions of the video.
static const double SEEK_POINTS[] = {0.13, 0.29, 0.41, 0.57, 0.71, 0.87};
#define SEEK_POINT_COUNT ((int)(sizeof(SEEK_POINTS) / sizeof(SEEK_POINTS[0])))

typedef struct {
    FILE *file;
    AVIOContext *io;
    AVFormatContext *fmt;
    AVCodecContext *codec;
    AVPacket *packet;
    AVFrame *frame;
    int stream;
} Seeker;

static int readFile(void *opaque, uint8_t *buf, int size)
{
    size_t n = fread(buf, 1, (size_t)size, (FILE *)opaque);
    return n > 0 ? (int)n : AVERROR_EOF;
}

static int64_t seekFile(void *opaque, int64_t offset, int whence)
{
    FILE *f = opaque;
    if (whence & AVSEEK_SIZE) {
        long here = ftell(f);
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, here, SEEK_SET);
        return size;
    }
    return fseek(f, (long)offset, whence & ~AVSEEK_FORCE) == 0 ? ftell(f) : -1;
}

static void seekerClose(Seeker *s)
{
    av_frame_free(&s->frame);
    av_packet_free(&s->packet);
    avcodec_free_context(&s->codec);
    if (s->fmt)
        avformat_close_input(&s->fmt);
    if (s->io) {
        av_freep(&s->io->buffer);
        avio_context_free(&s->io);
    }
    if (s->file)
        fclose(s->file);
    memset(s, 0, sizeof(*s));
}

// Ready for avformat_open_input, with its own reading from the file.
static bool seekerPrepare(Seeker *s, const char *path, const char *decoders)
{
    memset(s, 0, sizeof(*s));
    s->file = fopen(path, "rb");
    uint8_t *buffer = s->file ? av_malloc(IO_BUFFER) : NULL;
    s->io = buffer ? avio_alloc_context(buffer, IO_BUFFER, 0, s->file, readFile, NULL, seekFile) : NULL;
    if (buffer && !s->io)
        av_free(buffer);
    s->fmt = s->io ? avformat_alloc_context() : NULL;
    if (!s->fmt)
        return false;
    s->fmt->pb = s->io;
    s->fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
    if (decoders)
        s->fmt->codec_whitelist = av_strdup(decoders);
    return true;
}

static double msSince(BenchClock clock, uint64_t start_ns)
{
    return (double)(clock() - start_ns) / NS_PER_MS;
}

// One opening, timed. Leaves the file closed.
static bool openOnce(const char *path, const char *decoders, BenchClock clock, SeekOpenResult *sum)
{
    Seeker s;
    if (!seekerPrepare(&s, path, decoders)) {
        seekerClose(&s);
        return false;
    }
    uint64_t t = clock();
    bool ok = avformat_open_input(&s.fmt, NULL, NULL, NULL) >= 0;
    sum->open_ms += msSince(clock, t);
    t = clock();
    ok = ok && avformat_find_stream_info(s.fmt, NULL) >= 0;
    sum->info_ms += msSince(clock, t);
    int stream = ok ? av_find_best_stream(s.fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0) : -1;
    ok = ok && stream >= 0;
    if (ok) {
        AVStream *st = s.fmt->streams[stream];
        int64_t middle = av_rescale_q(s.fmt->duration / INDEX_SEEK_FRACTION, AV_TIME_BASE_Q, st->time_base);
        t = clock();
        av_seek_frame(s.fmt, stream, middle, AVSEEK_FLAG_BACKWARD);
        sum->index_ms += msSince(clock, t);
    }
    seekerClose(&s);
    return ok;
}

bool seekbenchOpen(const char *path, const char *decoders, BenchClock clock, SeekOpenResult *result, char *error,
                   size_t error_size)
{
    memset(result, 0, sizeof(*result));
    for (int i = 0; i < SEEKBENCH_ROUNDS; i++) {
        if (!openOnce(path, decoders, clock, result)) {
            snprintf(error, error_size, "can't open it this way");
            return false;
        }
    }
    result->open_ms /= SEEKBENCH_ROUNDS;
    result->info_ms /= SEEKBENCH_ROUNDS;
    result->index_ms /= SEEKBENCH_ROUNDS;
    return true;
}

static bool seekerOpenDecoder(Seeker *s, const char *path, int threads)
{
    if (!seekerPrepare(s, path, NULL) || avformat_open_input(&s->fmt, NULL, NULL, NULL) < 0 ||
        avformat_find_stream_info(s->fmt, NULL) < 0)
        return false;
    s->stream = av_find_best_stream(s->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (s->stream < 0)
        return false;
    AVStream *st = s->fmt->streams[s->stream];
    const AVCodec *decoder = avcodec_find_decoder(st->codecpar->codec_id);
    s->codec = decoder ? avcodec_alloc_context3(decoder) : NULL;
    s->packet = av_packet_alloc();
    s->frame = av_frame_alloc();
    if (!s->codec || !s->packet || !s->frame || avcodec_parameters_to_context(s->codec, st->codecpar) < 0)
        return false;
    s->codec->thread_count = threads;
    s->codec->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    s->codec->pkt_timebase = st->time_base;
    return avcodec_open2(s->codec, decoder, NULL) >= 0;
}

// Frames ready now: notes the first, and true once one reaches the target.
static bool takeFrames(Seeker *s, int64_t target, BenchClock clock, uint64_t start_ns, double *first_ms)
{
    while (avcodec_receive_frame(s->codec, s->frame) == 0) {
        int64_t pts = s->frame->best_effort_timestamp;
        av_frame_unref(s->frame);
        if (*first_ms < 0)
            *first_ms = msSince(clock, start_ns);
        if (pts != AV_NOPTS_VALUE && pts >= target)
            return true;
    }
    return false;
}

// One seek, as KaiVideo does it: frames nothing depends on are skipped on the way.
static bool seekOnce(Seeker *s, int64_t target, BenchClock clock, double *first_ms, double *target_ms, int *packets)
{
    uint64_t start = clock();
    *first_ms = -1;
    *packets = 0;
    if (av_seek_frame(s->fmt, s->stream, target, AVSEEK_FLAG_BACKWARD) < 0)
        return false;
    avcodec_flush_buffers(s->codec);
    while (msSince(clock, start) < MAX_SEEK_MS) {
        if (takeFrames(s, target, clock, start, first_ms)) {
            *target_ms = msSince(clock, start);
            return true;
        }
        if (av_read_frame(s->fmt, s->packet) < 0)
            return false;
        if (s->packet->stream_index == s->stream) {
            bool before = s->packet->pts != AV_NOPTS_VALUE && s->packet->pts < target;
            s->codec->skip_frame = before ? AVDISCARD_NONREF : AVDISCARD_DEFAULT;
            avcodec_send_packet(s->codec, s->packet);
            (*packets)++;
        }
        av_packet_unref(s->packet);
    }
    return false;
}

bool seekbenchRun(const char *path, int threads, BenchClock clock, BenchCancelled cancelled, void *cancel_data,
                  SeekRunResult *result, char *error, size_t error_size)
{
    Seeker s;
    memset(result, 0, sizeof(*result));
    if (!seekerOpenDecoder(&s, path, threads)) {
        snprintf(error, error_size, "can't decode it");
        seekerClose(&s);
        return false;
    }
    AVStream *st = s.fmt->streams[s.stream];
    int64_t duration = av_rescale_q(s.fmt->duration, AV_TIME_BASE_Q, st->time_base);
    for (int i = 0; i < SEEK_POINT_COUNT; i++) {
        if (cancelled && cancelled(cancel_data)) {
            result->cancelled = true;
            break;
        }
        double first_ms, target_ms;
        int packets;
        if (!seekOnce(&s, (int64_t)((double)duration * SEEK_POINTS[i]), clock, &first_ms, &target_ms, &packets))
            continue;
        result->seeks++;
        result->first_ms += first_ms;
        result->target_ms += target_ms;
        result->packets += packets;
        if (target_ms > result->worst_ms)
            result->worst_ms = target_ms;
    }
    seekerClose(&s);
    if (result->seeks) {
        result->first_ms /= result->seeks;
        result->target_ms /= result->seeks;
        result->packets /= result->seeks;
    } else if (!result->cancelled) {
        snprintf(error, error_size, "no seek worked");
        return false;
    }
    return true;
}
