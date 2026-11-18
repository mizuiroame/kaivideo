#include "bench.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>

#define IO_BUFFER (256 * 1024)
#define WARM_UP_FRAMES 30   // Frame threads hand out nothing at first: not measured.
#define MIN_SEEK_DURATION_S 60.0
#define NS_PER_S 1e9

typedef struct {
    FILE *file;
    AVFormatContext *fmt;
    AVIOContext *io;
    AVCodecContext *codec;
    AVPacket *packet;
    AVFrame *frame;
    uint8_t *y, *uv;
    int stream;
} Bench;

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

static void benchClose(Bench *b)
{
    free(b->y);
    free(b->uv);
    av_frame_free(&b->frame);
    av_packet_free(&b->packet);
    avcodec_free_context(&b->codec);
    if (b->fmt)
        avformat_close_input(&b->fmt);
    if (b->io) {
        av_freep(&b->io->buffer);
        avio_context_free(&b->io);
    }
    if (b->file)
        fclose(b->file);
    memset(b, 0, sizeof(*b));
}

static bool benchOpenFile(Bench *b, const char *path, char *error, size_t error_size)
{
    memset(b, 0, sizeof(*b));
    b->file = fopen(path, "rb");
    uint8_t *buffer = b->file ? av_malloc(IO_BUFFER) : NULL;
    b->io = buffer ? avio_alloc_context(buffer, IO_BUFFER, 0, b->file, readFile, NULL, seekFile) : NULL;
    if (buffer && !b->io)
        av_free(buffer);
    b->fmt = b->io ? avformat_alloc_context() : NULL;
    if (!b->fmt) {
        snprintf(error, error_size, "can't open the file");
        benchClose(b);
        return false;
    }
    b->fmt->pb = b->io;
    b->fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
    if (avformat_open_input(&b->fmt, NULL, NULL, NULL) < 0) {  // Frees fmt when it fails.
        snprintf(error, error_size, "FFmpeg can't read it");
        benchClose(b);
        return false;
    }
    if (avformat_find_stream_info(b->fmt, NULL) < 0 ||
        (b->stream = av_find_best_stream(b->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0)) < 0) {
        snprintf(error, error_size, "no video found");
        benchClose(b);
        return false;
    }
    return true;
}

bool benchProbe(const char *path, BenchInfo *info, char *error, size_t error_size)
{
    Bench b;
    memset(info, 0, sizeof(*info));
    if (!benchOpenFile(&b, path, error, error_size))
        return false;
    AVStream *st = b.fmt->streams[b.stream];
    const AVCodecParameters *par = st->codecpar;
    const AVPixFmtDescriptor *pix = av_pix_fmt_desc_get((enum AVPixelFormat)par->format);
    const char *profile = avcodec_profile_name(par->codec_id, par->profile);
    info->is_hevc = par->codec_id == AV_CODEC_ID_HEVC;
    info->width = par->width;
    info->height = par->height;
    info->bits = pix ? pix->comp[0].depth : par->bits_per_raw_sample;
    info->fps = av_q2d(av_guess_frame_rate(b.fmt, st, NULL));
    info->duration_s = b.fmt->duration > 0 ? b.fmt->duration / (double)AV_TIME_BASE : 0;
    snprintf(info->profile, sizeof(info->profile), "%s", profile ? profile : "?");
    benchClose(&b);
    return true;
}

// The frame as KaiVideo would need it: 8-bit Y, then interleaved UV.
static bool convertFrame(Bench *b, const AVFrame *f)
{
    int w = f->width & ~1, h = f->height & ~1;
    bool ten_bit = f->format == AV_PIX_FMT_YUV420P10LE;
    if (!ten_bit && f->format != AV_PIX_FMT_YUV420P && f->format != AV_PIX_FMT_YUVJ420P)
        return false;
    for (int y = 0; y < h; y++) {
        const uint8_t *src = f->data[0] + (ptrdiff_t)y * f->linesize[0];
        uint8_t *dst = b->y + (size_t)y * (size_t)w;
        if (!ten_bit) {
            memcpy(dst, src, (size_t)w);
            continue;
        }
        const uint16_t *wide = (const uint16_t *)src;
        for (int x = 0; x < w; x++)
            dst[x] = (uint8_t)(wide[x] >> 2);
    }
    for (int y = 0; y < h / 2; y++) {
        const uint8_t *u = f->data[1] + (ptrdiff_t)y * f->linesize[1];
        const uint8_t *v = f->data[2] + (ptrdiff_t)y * f->linesize[2];
        uint8_t *dst = b->uv + (size_t)y * (size_t)w;
        for (int x = 0; x < w / 2; x++) {
            dst[x * 2] = ten_bit ? (uint8_t)(((const uint16_t *)u)[x] >> 2) : u[x];
            dst[x * 2 + 1] = ten_bit ? (uint8_t)(((const uint16_t *)v)[x] >> 2) : v[x];
        }
    }
    return true;
}

static bool openDecoder(Bench *b, int threads, char *error, size_t error_size)
{
    AVStream *st = b->fmt->streams[b->stream];
    const AVCodec *decoder = avcodec_find_decoder(st->codecpar->codec_id);
    b->codec = decoder ? avcodec_alloc_context3(decoder) : NULL;
    b->packet = av_packet_alloc();
    b->frame = av_frame_alloc();
    if (!b->codec || !b->packet || !b->frame || avcodec_parameters_to_context(b->codec, st->codecpar) < 0) {
        snprintf(error, error_size, "no decoder for this video");
        return false;
    }
    b->codec->thread_count = threads;
    b->codec->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    b->codec->pkt_timebase = st->time_base;
    if (avcodec_open2(b->codec, decoder, NULL) < 0) {
        snprintf(error, error_size, "the decoder didn't open");
        return false;
    }
    size_t pixels = (size_t)(st->codecpar->width & ~1) * (size_t)(st->codecpar->height & ~1);
    b->y = malloc(pixels);
    b->uv = malloc(pixels / 2);
    if (!b->y || !b->uv) {
        snprintf(error, error_size, "not enough memory");
        return false;
    }
    return true;
}

typedef struct {
    BenchClock clock;
    BenchResult *result;
    int seen;
    uint64_t start_ns, stretch_ns, convert_ns;
} Meter;

// A frame came out: converts it and counts it, once past the warm-up.
static bool countFrame(Bench *b, Meter *m, char *error, size_t error_size)
{
    uint64_t before = m->clock();
    if (!convertFrame(b, b->frame)) {
        snprintf(error, error_size, "unexpected pixel format %s", av_get_pix_fmt_name((enum AVPixelFormat)b->frame->format));
        return false;
    }
    uint64_t now = m->clock();
    av_frame_unref(b->frame);
    if (++m->seen == WARM_UP_FRAMES) {
        m->start_ns = m->stretch_ns = now;
        return true;
    }
    if (m->seen < WARM_UP_FRAMES)
        return true;
    BenchResult *r = m->result;
    r->frames++;
    m->convert_ns += now - before;
    r->seconds = (double)(now - m->start_ns) / NS_PER_S;
    if (r->frames % BENCH_STRETCH_FRAMES == 0) {
        double fps = BENCH_STRETCH_FRAMES * NS_PER_S / (double)(now - m->stretch_ns);
        if (r->slowest_fps == 0 || fps < r->slowest_fps)
            r->slowest_fps = fps;
        m->stretch_ns = now;
    }
    return true;
}

static bool decodeLoop(Bench *b, Meter *m, double max_seconds, int max_frames, BenchCancelled cancelled,
                       void *cancel_data, char *error, size_t error_size)
{
    BenchResult *r = m->result;
    bool draining = false;
    while (r->frames < max_frames && r->seconds < max_seconds) {
        int got = avcodec_receive_frame(b->codec, b->frame);
        if (got == 0) {
            if (!countFrame(b, m, error, error_size))
                return false;
            continue;
        }
        if (got == AVERROR_EOF)
            return true;
        if (got != AVERROR(EAGAIN)) {
            snprintf(error, error_size, "decoding failed");
            return false;
        }
        if (cancelled && cancelled(cancel_data)) {
            r->cancelled = true;
            return true;
        }
        if (draining)
            continue;
        int read = av_read_frame(b->fmt, b->packet);
        if (read < 0) {
            draining = true;
            avcodec_send_packet(b->codec, NULL);
            continue;
        }
        if (b->packet->stream_index == b->stream)
            avcodec_send_packet(b->codec, b->packet);  // A damaged packet is skipped, as a player would.
        av_packet_unref(b->packet);
    }
    return true;
}

bool benchRun(const char *path, int threads, double max_seconds, int max_frames, BenchClock clock,
              BenchCancelled cancelled, void *cancel_data, BenchResult *result, char *error, size_t error_size)
{
    Bench b;
    memset(result, 0, sizeof(*result));
    if (!benchOpenFile(&b, path, error, error_size))
        return false;
    bool ok = openDecoder(&b, threads, error, error_size);
    if (ok) {
        result->thread_count = b.codec->thread_count;
        result->frame_threads = b.codec->active_thread_type == FF_THREAD_FRAME;
        if (b.fmt->duration / (double)AV_TIME_BASE > MIN_SEEK_DURATION_S)
            av_seek_frame(b.fmt, -1, b.fmt->duration / 4, AVSEEK_FLAG_BACKWARD);
        Meter meter = {.clock = clock, .result = result};
        ok = decodeLoop(&b, &meter, max_seconds, max_frames, cancelled, cancel_data, error, error_size);
        if (result->frames > 0) {
            result->convert_ms = (double)meter.convert_ns / result->frames / 1e6;
            result->average_fps = result->seconds > 0 ? result->frames / result->seconds : 0;
        }
    }
    benchClose(&b);
    return ok;
}
