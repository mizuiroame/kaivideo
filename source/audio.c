#include "audio.h"

#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <switch.h>

#define IO_BUFFER (64 * 1024)
#define BUFFER_COUNT 4
#define BUFFER_FRAMES 2048           // About 43 ms at 48 kHz: the picture's clock moves in these steps.
#define BUFFER_BYTES (BUFFER_FRAMES * 2 * sizeof(s16))
#define FADE_FRAMES 480              // 10 ms at 48 kHz: in and out of silence without a click.
#define BUFFER_ALIGN 0x1000
#define WAIT_TIMEOUT_NS 10000000ull
#define THREAD_STACK_SIZE 0x40000
#define THREAD_PRIORITY 0x2B

static u32 g_out_rate;
static bool g_ready;

struct Audio {
    FILE *file;
    AVFormatContext *fmt;
    AVIOContext *io;
    AVCodecContext *codec;
    SwrContext *swr;
    int stream;
    AVRational time_base;
    AVPacket *pkt;
    AVFrame *frame;
    char description[64];

    AudioOutBuffer buffers[BUFFER_COUNT];
    bool queued[BUFFER_COUNT];
    s16 *pending;                 // Converted sound not yet in a buffer.
    size_t pending_frames, pending_capacity;
    int64_t pending_pts_ns;       // Time of pending's first frame.

    Thread thread;
    bool thread_started;
    Mutex lock;
    volatile bool stop;
    bool paused;
    bool seek_requested;
    int64_t seek_ns;
    int64_t skip_until_ns;        // After a seek: sound before this is dropped.
    bool read_done, finished;

    // The clock: base_pts is the time of the first frame queued since the
    // last flush, base_played the output's played count when it was queued.
    bool clock_valid;
    int64_t base_pts_ns;
    u64 base_played;
    u64 queued_frames;            // Frames queued since the base.
};

bool audioSystemInit(void)
{
    if (R_FAILED(audoutInitialize()))
        return false;
    if (audoutGetChannelCount() != 2 || audoutGetPcmFormat() != PcmFormat_Int16 || R_FAILED(audoutStartAudioOut())) {
        audoutExit();
        return false;
    }
    g_out_rate = audoutGetSampleRate();
    g_ready = true;
    return true;
}

void audioSystemExit(void)
{
    if (!g_ready)
        return;
    audoutStopAudioOut();
    audoutExit();
    g_ready = false;
}

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

static u64 played(void)
{
    u64 count = 0;
    audoutGetAudioOutPlayedSampleCount(&count);
    return count;
}

// --- Output buffers ---

static void reap(Audio *a)
{
    AudioOutBuffer *released = NULL;
    u32 count = 0;
    audoutWaitPlayFinish(&released, &count, WAIT_TIMEOUT_NS);
    for (int i = 0; i < BUFFER_COUNT; i++) {
        bool contains = true;
        if (a->queued[i] && R_SUCCEEDED(audoutContainsAudioOutBuffer(&a->buffers[i], &contains)) && !contains)
            a->queued[i] = false;
    }
}

static bool anyQueued(const Audio *a)
{
    for (int i = 0; i < BUFFER_COUNT; i++)
        if (a->queued[i])
            return true;
    return false;
}

static void flushOutput(Audio *a)
{
    bool flushed;
    audoutFlushAudioOutBuffers(&flushed);
    for (int i = 0; i < BUFFER_COUNT; i++)
        a->queued[i] = false;
}

// --- Decoding ---

// Makes the resampler match the frame (it can change mid-file).
static bool prepareResampler(Audio *a, const AVFrame *f)
{
    if (a->swr)
        return true;
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    return swr_alloc_set_opts2(&a->swr, &stereo, AV_SAMPLE_FMT_S16, (int)g_out_rate, &f->ch_layout, f->format,
                               f->sample_rate, 0, NULL) >= 0 &&
           swr_init(a->swr) >= 0;
}

// Adds a decoded frame to pending, as output sound, dropping what's before skip_until.
static void addFrame(Audio *a, AVFrame *f)
{
    if (!prepareResampler(a, f))
        return;
    int64_t pts = f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts;
    int64_t pts_ns = pts != AV_NOPTS_VALUE ? av_rescale_q(pts, a->time_base, (AVRational){1, 1000000000}) : -1;
    int out = swr_get_out_samples(a->swr, f->nb_samples);
    if (out <= 0)
        return;
    if (a->pending_frames + (size_t)out > a->pending_capacity) {
        size_t capacity = (a->pending_frames + (size_t)out) * 2;
        s16 *grown = realloc(a->pending, capacity * 2 * sizeof(s16));
        if (!grown)
            return;
        a->pending = grown;
        a->pending_capacity = capacity;
    }
    uint8_t *dst = (uint8_t *)(a->pending + a->pending_frames * 2);
    int n = swr_convert(a->swr, &dst, out, (const uint8_t **)f->extended_data, f->nb_samples);
    if (n <= 0)
        return;
    if (a->pending_frames == 0 && pts_ns >= 0)
        a->pending_pts_ns = pts_ns;
    a->pending_frames += (size_t)n;
    // After a seek, drop sound before the target (seeks land on an earlier packet).
    if (a->skip_until_ns > 0 && a->pending_pts_ns >= 0 && a->pending_pts_ns < a->skip_until_ns) {
        size_t drop = (size_t)((a->skip_until_ns - a->pending_pts_ns) * (int64_t)g_out_rate / 1000000000);
        if (drop > a->pending_frames)
            drop = a->pending_frames;
        memmove(a->pending, a->pending + drop * 2, (a->pending_frames - drop) * 2 * sizeof(s16));
        a->pending_frames -= drop;
        a->pending_pts_ns += (int64_t)drop * 1000000000 / g_out_rate;
        if (a->pending_frames > 0)
            a->skip_until_ns = 0;
    }
}

// Decodes until pending holds a buffer's worth, or the file ends. False at the end.
static bool fill(Audio *a)
{
    while (a->pending_frames < BUFFER_FRAMES) {  // Also when stopping: the fade-out needs the sound that comes next.
        int r = avcodec_receive_frame(a->codec, a->frame);
        if (r == 0) {
            addFrame(a, a->frame);
            continue;
        }
        if (r == AVERROR_EOF || a->read_done)
            return a->pending_frames > 0;
        if (av_read_frame(a->fmt, a->pkt) < 0) {
            a->read_done = true;
            avcodec_send_packet(a->codec, NULL);  // Drain what's left.
            continue;
        }
        if (a->pkt->stream_index == a->stream)
            avcodec_send_packet(a->codec, a->pkt);
        av_packet_unref(a->pkt);
    }
    return a->pending_frames > 0;
}

typedef enum {
    FADE_NONE,
    FADE_IN,   // From silence: after starting, a seek or a pause.
    FADE_OUT,  // To silence: before stopping, a seek or a pause, so nothing is cut mid-wave (a click).
} Fade;

// Ramps the first FADE_FRAMES of a buffer up from silence, or down to it
// (then silence for the rest).
static void applyFade(s16 *pcm, Fade fade)
{
    for (size_t f = 0; fade != FADE_NONE && f < BUFFER_FRAMES; f++) {
        float gain = f >= FADE_FRAMES ? 1.0f : (float)f / FADE_FRAMES;
        if (fade == FADE_OUT)
            gain = 1.0f - gain;
        pcm[f * 2] = (s16)(pcm[f * 2] * gain);
        pcm[f * 2 + 1] = (s16)(pcm[f * 2 + 1] * gain);
    }
}

// Queues a buffer from pending (padded with silence at the very end).
static void queueBuffer(Audio *a, int i, Fade fade)
{
    AudioOutBuffer *b = &a->buffers[i];
    size_t n = a->pending_frames < BUFFER_FRAMES ? a->pending_frames : BUFFER_FRAMES;
    memcpy(b->buffer, a->pending, n * 2 * sizeof(s16));
    if (n < BUFFER_FRAMES)
        memset((s16 *)b->buffer + n * 2, 0, (BUFFER_FRAMES - n) * 2 * sizeof(s16));
    applyFade(b->buffer, fade);
    int64_t pts = a->pending_pts_ns;
    memmove(a->pending, a->pending + n * 2, (a->pending_frames - n) * 2 * sizeof(s16));
    a->pending_frames -= n;
    a->pending_pts_ns += (int64_t)n * 1000000000 / g_out_rate;
    b->data_size = BUFFER_BYTES;
    b->data_offset = 0;
    if (R_FAILED(audoutAppendAudioOutBuffer(b)))
        return;
    a->queued[i] = true;
    mutexLock(&a->lock);
    if (!a->clock_valid) {
        a->clock_valid = true;
        a->base_pts_ns = pts;
        a->base_played = played();
        a->queued_frames = 0;
    }
    a->queued_frames += BUFFER_FRAMES;
    mutexUnlock(&a->lock);
}

// A buffer the output is done with, waiting for one if needed; -1 if none frees up.
static int freeBuffer(Audio *a)
{
    for (int tries = 0; tries < 50; tries++) {
        for (int i = 0; i < BUFFER_COUNT; i++)
            if (!a->queued[i])
                return i;
        reap(a);
    }
    return -1;
}

// Ends the sound softly: one more buffer fading to silence, then lets what's
// queued play out (a flush would cut a wave in the middle: a click).
static void fadeOut(Audio *a)
{
    int i = freeBuffer(a);
    if (i >= 0) {
        fill(a);
        queueBuffer(a, i, FADE_OUT);
    }
    for (int tries = 0; tries < 50 && anyQueued(a); tries++)
        reap(a);
}

static void doSeek(Audio *a, int64_t target_ns)
{
    flushOutput(a);  // Already faded out and played out: nothing is cut.
    int64_t ts = av_rescale_q(target_ns, (AVRational){1, 1000000000}, a->time_base);
    av_seek_frame(a->fmt, a->stream, ts, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(a->codec);
    a->pending_frames = 0;
    a->read_done = false;
    a->skip_until_ns = target_ns > 0 ? target_ns : 0;
    mutexLock(&a->lock);
    a->clock_valid = false;
    a->finished = false;
    mutexUnlock(&a->lock);
}

static void audioThread(void *arg)
{
    Audio *a = arg;
    bool ended = false, silent = true;  // silent: nothing sounding, the next buffer fades in.
    while (true) {
        mutexLock(&a->lock);
        bool stop = a->stop, paused = a->paused, seek = a->seek_requested;
        int64_t target = a->seek_ns;
        a->seek_requested = false;
        mutexUnlock(&a->lock);
        if ((stop || paused || seek) && !silent && !ended) {
            fadeOut(a);
            silent = true;
        }
        if (stop)
            break;
        if (seek) {
            doSeek(a, target);
            ended = false;
        }
        if (paused || ended) {
            if (ended && !anyQueued(a)) {
                mutexLock(&a->lock);
                a->finished = true;
                mutexUnlock(&a->lock);
            }
            reap(a);
            svcSleepThread(WAIT_TIMEOUT_NS);
            continue;
        }
        for (int i = 0; i < BUFFER_COUNT && !ended; i++) {
            if (a->queued[i])
                continue;
            if (!fill(a)) {
                ended = true;
            } else {
                queueBuffer(a, i, silent ? FADE_IN : FADE_NONE);
                silent = false;
            }
        }
        reap(a);
    }
}

// --- Opening ---

Audio *audioOpen(const char *path, char *error, size_t error_size)
{
    if (error_size)
        error[0] = '\0';
    if (!g_ready) {
        snprintf(error, error_size, "The audio output isn't available.");
        return NULL;
    }
    Audio *a = calloc(1, sizeof(*a));
    if (!a)
        return NULL;
    mutexInit(&a->lock);
    a->file = fopen(path, "rb");
    uint8_t *buffer = av_malloc(IO_BUFFER);
    a->io = a->file && buffer ? avio_alloc_context(buffer, IO_BUFFER, 0, a->file, readFile, NULL, seekFile) : NULL;
    a->fmt = avformat_alloc_context();
    a->pkt = av_packet_alloc();
    a->frame = av_frame_alloc();
    if (!a->io || !a->fmt || !a->pkt || !a->frame) {
        if (!a->io)
            av_free(buffer);
        audioClose(a);
        return NULL;
    }
    a->fmt->pb = a->io;
    a->fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
    const AVCodec *decoder = NULL;
    if (avformat_open_input(&a->fmt, NULL, NULL, NULL) < 0 || avformat_find_stream_info(a->fmt, NULL) < 0 ||
        (a->stream = av_find_best_stream(a->fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0)) < 0) {
        audioClose(a);
        return NULL;  // No sound: the video plays silent.
    }
    AVStream *st = a->fmt->streams[a->stream];
    if (!decoder) {
        snprintf(error, error_size, "This video's sound (%s) can't be played.", avcodec_get_name(st->codecpar->codec_id));
        audioClose(a);
        return NULL;
    }
    a->codec = avcodec_alloc_context3(decoder);
    if (!a->codec || avcodec_parameters_to_context(a->codec, st->codecpar) < 0 || avcodec_open2(a->codec, decoder, NULL) < 0) {
        snprintf(error, error_size, "This video's sound can't be played.");
        audioClose(a);
        return NULL;
    }
    a->time_base = st->time_base;
    snprintf(a->description, sizeof(a->description), "%s, %d kHz, %d channel%s", decoder->long_name ? decoder->name : "",
             st->codecpar->sample_rate / 1000, st->codecpar->ch_layout.nb_channels,
             st->codecpar->ch_layout.nb_channels == 1 ? "" : "s");
    for (int i = 0; i < BUFFER_COUNT; i++) {
        a->buffers[i].buffer = memalign(BUFFER_ALIGN, BUFFER_BYTES);
        a->buffers[i].buffer_size = BUFFER_BYTES;
        if (!a->buffers[i].buffer) {
            audioClose(a);
            return NULL;
        }
    }
    a->paused = true;  // Until the player says the picture is ready.
    if (R_FAILED(threadCreate(&a->thread, audioThread, a, NULL, THREAD_STACK_SIZE, THREAD_PRIORITY, -2)) ||
        R_FAILED(threadStart(&a->thread))) {
        snprintf(error, error_size, "The sound thread can't start.");
        audioClose(a);
        return NULL;
    }
    a->thread_started = true;
    return a;
}

void audioClose(Audio *a)
{
    if (!a)
        return;
    if (a->thread_started) {
        a->stop = true;
        threadWaitForExit(&a->thread);
        threadClose(&a->thread);
    }
    if (g_ready)
        flushOutput(a);
    for (int i = 0; i < BUFFER_COUNT; i++)
        free(a->buffers[i].buffer);
    free(a->pending);
    swr_free(&a->swr);
    avcodec_free_context(&a->codec);
    av_packet_free(&a->pkt);
    av_frame_free(&a->frame);
    if (a->fmt)
        avformat_close_input(&a->fmt);
    if (a->io) {
        av_freep(&a->io->buffer);
        avio_context_free(&a->io);
    }
    if (a->file)
        fclose(a->file);
    free(a);
}

const char *audioDescription(const Audio *a)
{
    return a->description;
}

bool audioClock(Audio *a, int64_t *pts_ns)
{
    mutexLock(&a->lock);
    bool valid = a->clock_valid;
    if (valid) {
        u64 heard = played() - a->base_played;
        if (heard > a->queued_frames)
            heard = a->queued_frames;  // Not past what was queued.
        *pts_ns = a->base_pts_ns + (int64_t)(heard * 1000000000ull / g_out_rate);
    }
    mutexUnlock(&a->lock);
    return valid;
}

void audioSetPaused(Audio *a, bool paused)
{
    mutexLock(&a->lock);
    a->paused = paused;
    mutexUnlock(&a->lock);
}

void audioSeek(Audio *a, int64_t pts_ns)
{
    mutexLock(&a->lock);
    a->seek_requested = true;
    a->seek_ns = pts_ns;
    a->clock_valid = false;
    mutexUnlock(&a->lock);
}

bool audioFinished(Audio *a)
{
    mutexLock(&a->lock);
    bool finished = a->finished;
    mutexUnlock(&a->lock);
    return finished;
}
