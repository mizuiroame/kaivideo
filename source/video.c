#include "video.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
#include <switch.h>

#include "hlsio.h"
#include "lang.h"
#include "mediafile.h"
#include "subs.h"
#include "swframe.h"

#define IO_BUFFER (256 * 1024)
#define SUBMIT_TIMEOUT_NS 2000000ull
#define SUBMIT_TRIES 2500             // About 5 seconds of retries before giving up.
#define TAIL_TIMEOUT_NS 300000000ull  // After the last sample: how long to wait for frames.

#define SUB_TEXT_MAX 1024
#define SUB_DEFAULT_NS 4000000000ll  // A line without an end shows this long.
#define DEFAULT_FRAME_NS 41666667ll  // Between frames, when a file doesn't say.
#define MAX_RECEIVE_ERRORS 64        // In a row: decoding has failed for good.
#define SUB_MAX_FONTS 64             // Fonts for styled subtitles, the file's included.
#define SUB_MAX_FONT_BYTES (48 * 1024 * 1024)
#define NO_STYLED_TRACK (-1)

typedef struct {
    uint8_t *y, *uv;   // NV12, width bytes a row.
    int64_t pts_ns;
} Slot;

// A text subtitle track inside the file, decoded as it is read.
typedef struct {
    int stream;
    AVCodecContext *ctx;   // NULL for a styled track, which libass reads as it is.
    int styled_track;      // Its number in the Video's AssSubs, or NO_STYLED_TRACK.
    SubTrack track;    // Under the Video's lock.
    char label[64];
    bool preferred;    // Marked default or forced in the file.
} SubStream;

struct Video {
    MediaInput input;
    int64_t time_shift;            // Taken from the stream's times (converted video).
    AVFormatContext *fmt;
    AVBSFContext *bsf;
    int stream;
    AVRational time_base;
    int64_t duration_ns;
    VideoDecoder decoder;
    bool decoder_open;
    // Decoding on the CPU instead (only the decoding thread uses these once open).
    AVCodecContext *software;
    AVFrame *software_frame;
    int max_threads;               // 0: as many as suit the video.
    int64_t software_last_ns;      // The last frame's time: for a frame without one.
    int64_t frame_ns;
    int64_t clock_ns;              // Where playback last was (SWFRAME_NO_CLOCK after a seek).
    char description[96];
    int width, height;
    double aspect;
    bool bt709, full_range;

    pthread_t thread;
    bool thread_started;
    pthread_mutex_t lock;
    pthread_cond_t wake;           // A slot came free, a seek came, or stop.
    Slot slots[VIDEO_QUEUE];       // Decoded frames, oldest at head.
    int head, count;
    Slot shown;                    // The frame being shown: its buffers swap with a slot's.
    GfxVideoFrame frame;
    volatile bool stop;
    bool seek_requested;
    int64_t seek_ns;
    int64_t skip_until_ns;         // After a seek: frames before this aren't kept.
    // On the CPU, a seek's first picture, shown while the target is decoded.
    Slot preview;                  // The decoding thread's until preview_ready; then the window's.
    bool preview_wanted;           // Only the decoding thread uses it.
    bool preview_ready;
    // The latest time of the video sent to the decoder since the last seek:
    // a frame later than it was decoded before the seek (the decoder hands a
    // few of those out after its flush) and isn't kept. Seeking back, they
    // would wait in the queue for a time that never comes.
    int64_t submitted_max_ns;
    bool done;                     // The thread has decoded all there is.
    bool failed;
    char error[128];

    unsigned frame_id;
    VideoStats stats;
    double copy_total_ms;

    SubStream subs[VIDEO_SUB_TRACKS];
    int sub_count;
    AssSubs *styled;               // Only if the file has ASS tracks.
    char audio_names[VIDEO_AUDIO_TRACKS][64];
    int audio_count;
    PlaybackChapter chapters[VIDEO_CHAPTERS];
    int chapter_count;
};

// --- The decoding thread ---

static void fail(Video *v, const char *message)
{
    pthread_mutex_lock(&v->lock);
    if (!v->failed) {
        v->failed = true;
        snprintf(v->error, sizeof(v->error), "%s", message);
    }
    pthread_mutex_unlock(&v->lock);
}

// True when the thread should drop what it's doing: stop, or a seek.
static bool interrupted(Video *v)
{
    pthread_mutex_lock(&v->lock);
    bool yes = v->stop || v->seek_requested;
    pthread_mutex_unlock(&v->lock);
    return yes;
}

// For FFmpeg: a jump doesn't wait for a segment of the place being left.
static int readInterrupted(void *opaque)
{
    return interrupted(opaque);
}

// The queue's next free slot, once there is one; NULL when interrupted.
static Slot *waitForSlot(Video *v)
{
    pthread_mutex_lock(&v->lock);
    while (v->count == VIDEO_QUEUE && !v->stop && !v->seek_requested)
        pthread_cond_wait(&v->wake, &v->lock);
    bool stop = v->stop || v->seek_requested;
    Slot *s = &v->slots[(v->head + v->count) % VIDEO_QUEUE];
    pthread_mutex_unlock(&v->lock);
    return stop ? NULL : s;
}

// The slot from waitForSlot is filled: into the queue. False if a seek came
// meanwhile (the frame is from before it).
static bool commitSlot(Video *v, double copy_ms)
{
    pthread_mutex_lock(&v->lock);
    bool seeking = v->seek_requested;
    if (!seeking) {
        v->count++;
        v->stats.decoded++;
        v->copy_total_ms += copy_ms;
    }
    pthread_mutex_unlock(&v->lock);
    return !seeking;
}

// --- Decoding on the CPU ---

static int softwareBits(int format)
{
    switch (format) {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUVJ420P: return 8;
    case AV_PIX_FMT_YUV420P10LE: return 10;
    default: return 0;
    }
}

// A decoded frame into a slot, as NV12. The time it took in copy_ms.
static void convertSoftwareFrame(const Video *v, const AVFrame *f, int bits, Slot *s, int64_t pts_ns, double *copy_ms)
{
    int width = f->width < v->width ? f->width & ~1 : v->width;
    int height = f->height < v->height ? f->height & ~1 : v->height;
    const uint8_t *const planes[3] = {f->data[0], f->data[1], f->data[2]};
    const ptrdiff_t lines[3] = {f->linesize[0], f->linesize[1], f->linesize[2]};
    u64 t0 = armGetSystemTick();
    swframeToNv12(planes, lines, width, height, bits, s->y, s->uv, v->width);
    s->pts_ns = pts_ns;
    *copy_ms = armTicksToNs(armGetSystemTick() - t0) / 1e6;
}

// A decoded frame into the queue. False when interrupted or failed.
static bool queueSoftwareFrame(Video *v, const AVFrame *f, int bits, int64_t pts_ns)
{
    Slot *s = waitForSlot(v);
    if (!s)
        return false;
    double copy_ms;
    convertSoftwareFrame(v, f, bits, s, pts_ns, &copy_ms);
    return commitSlot(v, copy_ms);
}

// The seek's first picture: for showing until the target is ready.
static void keepPreview(Video *v, const AVFrame *f, int bits, int64_t pts_ns)
{
    double copy_ms;
    v->preview_wanted = false;
    convertSoftwareFrame(v, f, bits, &v->preview, pts_ns, &copy_ms);
    pthread_mutex_lock(&v->lock);
    v->preview_ready = !v->seek_requested;
    pthread_mutex_unlock(&v->lock);
}

// A frame out of FFmpeg: queued, or it's from before a seek's target. False
// when interrupted or failed.
static bool handleSoftwareFrame(Video *v, const AVFrame *f, int64_t pts_ns)
{
    int bits = softwareBits(f->format);
    if (!bits) {
        fail(v, T("This video's picture format can't be shown."));
        return false;
    }
    if (pts_ns >= v->skip_until_ns) {
        v->preview_wanted = false;
        return queueSoftwareFrame(v, f, bits, pts_ns);
    }
    if (v->preview_wanted)
        keepPreview(v, f, bits, pts_ns);
    return true;
}

// Takes every frame FFmpeg has ready. False when interrupted or failed.
static bool takeSoftwareFrames(Video *v)
{
    AVFrame *f = v->software_frame;
    int errors = 0;
    while (!interrupted(v)) {
        int got = avcodec_receive_frame(v->software, f);
        if (got == AVERROR(EAGAIN) || got == AVERROR_EOF)
            return true;
        if (got < 0) {  // A damaged frame: the next ones may be fine.
            if (++errors < MAX_RECEIVE_ERRORS)
                continue;
            fail(v, T("The video can't be decoded."));
            return false;
        }
        errors = 0;
        int64_t pts = f->best_effort_timestamp;
        int64_t pts_ns = pts != AV_NOPTS_VALUE ? av_rescale_q(pts, v->time_base, (AVRational){1, 1000000000})
                                               : v->software_last_ns + v->frame_ns;
        v->software_last_ns = pts_ns;
        bool ok = handleSoftwareFrame(v, f, pts_ns);
        av_frame_unref(f);
        if (!ok)
            return false;
    }
    return false;
}

static bool submitSoftware(Video *v, const AVPacket *p, bool last)
{
    pthread_mutex_lock(&v->lock);
    int64_t clock_ns = v->clock_ns;
    pthread_mutex_unlock(&v->lock);
    bool skippable = p->pts != AV_NOPTS_VALUE &&
                     swframeCanSkip(av_rescale_q(p->pts, v->time_base, (AVRational){1, 1000000000}), v->skip_until_ns,
                                    clock_ns);
    v->software->skip_frame = skippable ? AVDISCARD_NONREF : AVDISCARD_DEFAULT;
    int sent;
    while ((sent = avcodec_send_packet(v->software, p)) == AVERROR(EAGAIN))
        if (!takeSoftwareFrames(v))
            return false;
    if (sent < 0 && sent != AVERROR_INVALIDDATA) {
        fail(v, T("The video can't be decoded."));
        return false;
    }
    if (last)
        avcodec_send_packet(v->software, NULL);  // The frames it still holds come out.
    return takeSoftwareFrames(v);
}

// --- Decoding with the console's decoder ---

// Moves every frame the decoder has ready into the queue, waiting up to
// timeout_ns for the first; waits for room when the queue is full. False when interrupted.
static bool takeFrames(Video *v, u64 timeout_ns)
{
    if (v->software)
        return takeSoftwareFrames(v);
    VideoFrame f;
    while (R_SUCCEEDED(videoDecoderGetFrame(&v->decoder, &f, timeout_ns))) {
        timeout_ns = 0;
        if (f.timestamp_ns < v->skip_until_ns || f.timestamp_ns > v->submitted_max_ns) {
            // Before a seek's target (decoded only to get there), or from before the seek.
            videoDecoderReleaseFrame(&v->decoder, &f);
            continue;
        }
        Slot *s = waitForSlot(v);
        if (!s) {
            videoDecoderReleaseFrame(&v->decoder, &f);
            return false;
        }
        u64 t0 = armGetSystemTick();
        Result rc = videoDecoderCopyFrame(&v->decoder, &f, s->y, (size_t)v->width, s->uv, (size_t)v->width);
        double ms = armTicksToNs(armGetSystemTick() - t0) / 1e6;
        s->pts_ns = f.timestamp_ns;
        videoDecoderReleaseFrame(&v->decoder, &f);
        if (R_FAILED(rc))
            continue;
        if (!commitSlot(v, ms))
            return false;
    }
    return !interrupted(v);
}

static bool submit(Video *v, const AVPacket *p, bool last)
{
    if (v->software)
        return submitSoftware(v, p, last);
    int64_t ts = p->pts != AV_NOPTS_VALUE ? p->pts : p->dts != AV_NOPTS_VALUE ? p->dts : 0;
    s64 ns = av_rescale_q(ts, v->time_base, (AVRational){1, 1000000000});
    if (ns > v->submitted_max_ns)
        v->submitted_max_ns = ns;
    for (int tries = 0; tries < SUBMIT_TRIES; tries++) {
        if (!takeFrames(v, 0))
            return false;
        Result rc = videoDecoderSubmit(&v->decoder, p->data, p->size, ns, last, SUBMIT_TIMEOUT_NS);
        if (R_SUCCEEDED(rc))
            return true;
        if (rc != KERNELRESULT(TimedOut) && rc != VIDDEC_RESULT_SUBMIT_BUSY) {
            char message[96];
            snprintf(message, sizeof(message), T("The video decoder refused the video (0x%x)."), rc);
            fail(v, message);
            return false;
        }
    }
    if (!interrupted(v))
        fail(v, T("The video decoder stopped taking the video."));
    return false;
}

// Goes to the keyframe before target and drops everything decoded or queued.
static void doSeek(Video *v, int64_t target_ns)
{
    int64_t ts = av_rescale_q(target_ns, (AVRational){1, 1000000000}, v->time_base) + v->time_shift;
    av_seek_frame(v->fmt, v->stream, ts, AVSEEK_FLAG_BACKWARD);
    if (v->bsf)
        av_bsf_flush(v->bsf);
    if (v->software) {
        avcodec_flush_buffers(v->software);
        v->software_last_ns = target_ns;
    } else {
        videoDecoderFlush(&v->decoder);
    }
    for (int i = 0; i < v->sub_count; i++)
        if (v->subs[i].ctx)
            avcodec_flush_buffers(v->subs[i].ctx);
    v->skip_until_ns = target_ns;
    v->submitted_max_ns = INT64_MIN;
    v->preview_wanted = v->preview.y != NULL;
    pthread_mutex_lock(&v->lock);
    v->head = 0;
    v->count = 0;
    v->done = false;
    v->preview_ready = false;
    pthread_cond_broadcast(&v->wake);
    pthread_mutex_unlock(&v->lock);
}

// A subtitle packet: its lines go into its track, with their times.
static void decodeSubtitle(Video *v, SubStream *s, AVPacket *pkt)
{
    int64_t pts = pkt->pts != AV_NOPTS_VALUE ? pkt->pts : pkt->dts;
    AVSubtitle sub;
    int got = 0;
    if (pts == AV_NOPTS_VALUE || avcodec_decode_subtitle2(s->ctx, &sub, &got, pkt) < 0 || !got)
        return;
    AVRational tb = v->fmt->streams[s->stream]->time_base, ns = {1, 1000000000};
    int64_t base = av_rescale_q(pts, tb, ns);
    int64_t start = base + (int64_t)sub.start_display_time * 1000000;
    int64_t end;
    if (sub.end_display_time > sub.start_display_time && sub.end_display_time != UINT32_MAX)
        end = base + (int64_t)sub.end_display_time * 1000000;
    else if (pkt->duration > 0)
        end = base + av_rescale_q(pkt->duration, tb, ns);
    else
        end = start + SUB_DEFAULT_NS;
    char text[SUB_TEXT_MAX];
    size_t used = 0;
    text[0] = '\0';
    for (unsigned i = 0; i < sub.num_rects; i++) {
        const AVSubtitleRect *r = sub.rects[i];
        const char *line = r->type == SUBTITLE_ASS && r->ass ? subsAssText(r->ass)
                         : r->type == SUBTITLE_TEXT ? r->text : NULL;
        if (!line)
            continue;  // A picture subtitle: not drawn.
        int n = snprintf(text + used, sizeof(text) - used, "%s%s", used ? "\\N" : "", line);
        if (n < 0 || (size_t)n >= sizeof(text) - used)
            break;
        used += (size_t)n;
    }
    avsubtitle_free(&sub);
    if (!used)
        return;
    pthread_mutex_lock(&v->lock);
    subsAdd(&s->track, start, end, text);
    pthread_mutex_unlock(&v->lock);
}

// A styled line goes to libass as the file holds it, with its times.
static void addStyledLine(Video *v, const SubStream *s, const AVPacket *pkt)
{
    int64_t pts = pkt->pts != AV_NOPTS_VALUE ? pkt->pts : pkt->dts;
    if (pts == AV_NOPTS_VALUE || !pkt->data || pkt->size <= 0)
        return;
    AVRational tb = v->fmt->streams[s->stream]->time_base, ms = {1, 1000};
    int64_t duration = pkt->duration > 0 ? av_rescale_q(pkt->duration, tb, ms) : SUB_DEFAULT_NS / 1000000;
    assSubsAddEvent(v->styled, s->styled_track, (const char *)pkt->data, (size_t)pkt->size, av_rescale_q(pts, tb, ms),
                    duration);
}

static SubStream *subStreamFor(Video *v, int index)
{
    for (int i = 0; i < v->sub_count; i++)
        if (v->subs[i].stream == index)
            return &v->subs[i];
    return NULL;
}

// Reads to the end of the file: the stream's packets go to the decoder one
// behind, so the last can say it's the last. False if interrupted or failed.
static bool decodeToEnd(Video *v, AVPacket *pkt, AVPacket *held)
{
    bool have_held = false;
    while (!interrupted(v) && av_read_frame(v->fmt, pkt) >= 0) {
        if (pkt->stream_index == v->stream && v->time_shift) {
            if (pkt->pts != AV_NOPTS_VALUE)
                pkt->pts -= v->time_shift;
            if (pkt->dts != AV_NOPTS_VALUE)
                pkt->dts -= v->time_shift;
        }
        if (pkt->stream_index != v->stream) {
            SubStream *s = subStreamFor(v, pkt->stream_index);
            if (s && s->styled_track != NO_STYLED_TRACK)
                addStyledLine(v, s, pkt);
            else if (s)
                decodeSubtitle(v, s, pkt);
            av_packet_unref(pkt);
            continue;
        }
        if (v->bsf && av_bsf_send_packet(v->bsf, pkt) < 0) {
            av_packet_unref(pkt);
            continue;
        }
        // Without a filter, the packet itself is the sample.
        while (v->bsf ? av_bsf_receive_packet(v->bsf, pkt) >= 0 : pkt->size > 0) {
            if (have_held && !submit(v, held, false)) {
                av_packet_unref(pkt);
                av_packet_unref(held);
                return false;
            }
            av_packet_unref(held);
            av_packet_move_ref(held, pkt);
            have_held = true;
        }
        av_packet_unref(pkt);
    }
    if (!interrupted(v) && mediaFileFailed(v->input.file)) {  // Not the end: the network went.
        av_packet_unref(held);
        fail(v, T("The connection to the server was lost."));
        return false;
    }
    bool ok = !interrupted(v) && (!have_held || submit(v, held, true));
    av_packet_unref(held);
    return ok && takeFrames(v, TAIL_TIMEOUT_NS);
}

static void *decodeThread(void *arg)
{
    Video *v = arg;
    AVPacket *pkt = av_packet_alloc(), *held = av_packet_alloc();
    if (!pkt || !held)
        fail(v, T("Out of memory."));
    while (pkt && held) {
        pthread_mutex_lock(&v->lock);
        bool stop = v->stop, seek = v->seek_requested;
        int64_t target = v->seek_ns;
        v->seek_requested = false;
        pthread_mutex_unlock(&v->lock);
        if (stop)
            break;
        if (seek)
            doSeek(v, target);
        if (decodeToEnd(v, pkt, held) || !interrupted(v)) {
            // The end (or a failure): wait for a seek back, or to stop.
            pthread_mutex_lock(&v->lock);
            v->done = true;
            while (!v->stop && !v->seek_requested)
                pthread_cond_wait(&v->wake, &v->lock);
            pthread_mutex_unlock(&v->lock);
        }
    }
    av_packet_free(&pkt);
    av_packet_free(&held);
    return NULL;
}

// --- Opening ---

static const char *codecName(enum AVCodecID id)
{
    switch (id) {
    case AV_CODEC_ID_H264: return "H.264";
    case AV_CODEC_ID_VP9: return "VP9";
    case AV_CODEC_ID_AV1: return "AV1";
    case AV_CODEC_ID_VP8: return "VP8";
    case AV_CODEC_ID_HEVC: return "HEVC (H.265)";
    case AV_CODEC_ID_MPEG4: return "MPEG-4 Part 2";
    default: return avcodec_get_name(id);
    }
}

static SwframeCodec swframeCodecOf(enum AVCodecID id)
{
    switch (id) {
    case AV_CODEC_ID_H264: return SWFRAME_H264;
    case AV_CODEC_ID_HEVC: return SWFRAME_HEVC;
    case AV_CODEC_ID_VP9: return SWFRAME_VP9;
    case AV_CODEC_ID_AV1: return SWFRAME_AV1;
    case AV_CODEC_ID_VP8: return SWFRAME_VP8;
    default: return SWFRAME_OTHER;
    }
}

static int cpuCores(void)
{
    u64 mask = 0;
    return R_SUCCEEDED(svcGetInfo(&mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0)) ? __builtin_popcountll(mask) : 1;
}

static bool openSoftware(Video *v, AVStream *st, double fps, char *error, size_t error_size)
{
    AVCodecParameters *par = st->codecpar;
    const AVCodec *codec = avcodec_find_decoder(par->codec_id);
    v->software = codec ? avcodec_alloc_context3(codec) : NULL;
    v->software_frame = av_frame_alloc();
    if (!v->software || !v->software_frame || avcodec_parameters_to_context(v->software, par) < 0) {
        snprintf(error, error_size, T("This %s video can't be decoded."), codecName(par->codec_id));
        return false;
    }
    int threads = swframeThreads(cpuCores());
    v->software->thread_count = v->max_threads > 0 && v->max_threads < threads ? v->max_threads : threads;
    v->software->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    v->software->pkt_timebase = st->time_base;
    if (avcodec_open2(v->software, codec, NULL) < 0) {
        snprintf(error, error_size, T("This %s video can't be decoded."), codecName(par->codec_id));
        return false;
    }
    v->frame_ns = fps > 0 ? (int64_t)(1e9 / fps) : DEFAULT_FRAME_NS;
    snprintf(v->description, sizeof(v->description), T("%s %dx%d, %.3g fps, software"), codecName(par->codec_id),
             par->width, par->height, fps);
    return true;
}

// The console's decoder, with the filter its codec needs. False, with a
// reason, if it can't take this video.
static bool openHardware(Video *v, AVStream *st, double fps, char *error, size_t error_size)
{
    AVCodecParameters *par = st->codecpar;
    ViddecCodec codec;
    const char *filter = NULL;
    switch (par->codec_id) {
    case AV_CODEC_ID_H264: codec = ViddecCodec_H264; filter = "h264_mp4toannexb"; break;
    case AV_CODEC_ID_VP9: codec = ViddecCodec_Vp9; filter = "vp9_superframe_split"; break;
    case AV_CODEC_ID_AV1: codec = ViddecCodec_Av1; break;
    default: codec = ViddecCodec_Vp8; break;
    }
    if (filter) {
        const AVBitStreamFilter *spec = av_bsf_get_by_name(filter);
        if (!spec || av_bsf_alloc(spec, &v->bsf) < 0 || avcodec_parameters_copy(v->bsf->par_in, par) < 0 ||
            (v->bsf->time_base_in = st->time_base, av_bsf_init(v->bsf) < 0)) {
            snprintf(error, error_size, "%s", T("The video can't be prepared for the decoder."));
            return false;
        }
    }
    // H.264 codes 1080 lines as 1088: the decoder sizes its reference images
    // from what it's told, and with B-frames 1080 isn't enough (it stalls).
    VideoDecoderConfig config = {
        .codec = codec,
        .width = codec == ViddecCodec_H264 ? (u32)FFALIGN(par->width, 16) : (u32)par->width,
        .height = codec == ViddecCodec_H264 ? (u32)FFALIGN(par->height, 16) : (u32)par->height,
        .frame_rate = fps >= 1 && fps <= VIDEO_DECODER_MAX_FRAME_RATE ? (u32)(fps + 0.5) : 0,
        .max_sample_size = 16 * 1024 * 1024,
        .codec_config = v->bsf && codec == ViddecCodec_H264 ? v->bsf->par_out->extradata : NULL,
        .codec_config_size = v->bsf && codec == ViddecCodec_H264 ? (size_t)v->bsf->par_out->extradata_size : 0,
    };
    Result rc = videoDecoderCreate(&v->decoder, &config);
    if (R_FAILED(rc)) {
        snprintf(error, error_size, T("The console's decoder can't play this %s video (%dx%d, 0x%x)."),
                 codecName(par->codec_id), par->width, par->height, rc);
        return false;
    }
    v->decoder_open = true;
    snprintf(v->description, sizeof(v->description), T("%s %dx%d, %.3g fps"), codecName(par->codec_id), par->width,
             par->height, fps);
    return true;
}

static bool openDecoder(Video *v, AVStream *st, char *error, size_t error_size)
{
    AVCodecParameters *par = st->codecpar;
    const AVPixFmtDescriptor *pix = av_pix_fmt_desc_get((enum AVPixelFormat)par->format);
    int bits = pix ? pix->comp[0].depth : 0;
    SwframeCodec kind = swframeCodecOf(par->codec_id);
    double fps = av_q2d(av_guess_frame_rate(v->fmt, st, NULL));
    switch (swframeRoute(kind, bits, par->width, par->height)) {
    case SWFRAME_SOFTWARE:
        return openSoftware(v, st, fps, error, error_size);
    case SWFRAME_HARDWARE:
        if (openHardware(v, st, fps, error, error_size))
            return true;
        if (kind != SWFRAME_H264 && kind != SWFRAME_VP9)
            return false;
        av_bsf_free(&v->bsf);  // H.264 or VP9 the decoder refuses: FFmpeg can still play it.
        return openSoftware(v, st, fps, error, error_size);
    default:
        if (kind == SWFRAME_OTHER)
            snprintf(error, error_size, T("%s video can't be played: KaiVideo plays H.264, HEVC, VP9, AV1 and VP8."),
                     codecName(par->codec_id));
        else
            snprintf(error, error_size, T("%d-bit %s video can't be played."), bits, codecName(par->codec_id));
        return false;
    }
}

static const AssFont *g_subtitle_fonts;
static int g_subtitle_font_count;

void videoSetSubtitleFonts(const AssFont *fonts, int count)
{
    g_subtitle_fonts = fonts;
    g_subtitle_font_count = count;
}

static bool isStyledSubtitle(const AVStream *st)
{
    return st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE &&
           (st->codecpar->codec_id == AV_CODEC_ID_ASS || st->codecpar->codec_id == AV_CODEC_ID_SSA);
}

// A font the file carries for its subtitles.
static bool isFontAttachment(const AVStream *st, const char **name)
{
    if (st->codecpar->codec_type != AVMEDIA_TYPE_ATTACHMENT || !st->codecpar->extradata ||
        st->codecpar->extradata_size <= 0)
        return false;
    const AVDictionaryEntry *file = av_dict_get(st->metadata, "filename", NULL, 0);
    const AVDictionaryEntry *type = av_dict_get(st->metadata, "mimetype", NULL, 0);
    *name = file ? file->value : "attachment";
    const char *dot = file ? strrchr(file->value, '.') : NULL;
    bool font_name = dot && (!strcasecmp(dot, ".ttf") || !strcasecmp(dot, ".otf") || !strcasecmp(dot, ".ttc"));
    bool font_type = type && (strstr(type->value, "font") || strstr(type->value, "truetype") ||
                              strstr(type->value, "opentype"));
    return font_name || font_type;
}

// Starts libass if the file has ASS tracks, with our fonts and the file's.
static void openStyledSubtitles(Video *v)
{
    bool wanted = false;
    for (unsigned i = 0; i < v->fmt->nb_streams; i++)
        wanted = wanted || isStyledSubtitle(v->fmt->streams[i]);
    if (!wanted)
        return;
    AssFont fonts[SUB_MAX_FONTS];
    int count = 0;
    size_t bytes = 0;
    for (; count < g_subtitle_font_count && count < SUB_MAX_FONTS; count++)
        fonts[count] = g_subtitle_fonts[count];
    for (unsigned i = 0; i < v->fmt->nb_streams && count < SUB_MAX_FONTS; i++) {
        const AVStream *st = v->fmt->streams[i];
        const char *name;
        if (!isFontAttachment(st, &name) || bytes + (size_t)st->codecpar->extradata_size > SUB_MAX_FONT_BYTES)
            continue;
        bytes += (size_t)st->codecpar->extradata_size;
        fonts[count++] = (AssFont){name, st->codecpar->extradata, (size_t)st->codecpar->extradata_size};
    }
    v->styled = assSubsCreate(v->width, v->height, fonts, count);
}

// Prepares s for this stream. False if it can't be read.
static bool openSubStream(Video *v, SubStream *s, AVStream *st)
{
    s->styled_track = NO_STYLED_TRACK;
    if (v->styled && isStyledSubtitle(st)) {
        s->styled_track = assSubsAddTrack(v->styled, (const char *)st->codecpar->extradata,
                                          (size_t)st->codecpar->extradata_size);
        if (s->styled_track != NO_STYLED_TRACK)
            return true;
    }
    const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
    AVCodecContext *ctx = codec ? avcodec_alloc_context3(codec) : NULL;
    if (!ctx)
        return false;
    ctx->pkt_timebase = st->time_base;
    if (avcodec_parameters_to_context(ctx, st->codecpar) < 0 || avcodec_open2(ctx, codec, NULL) < 0) {
        avcodec_free_context(&ctx);
        return false;
    }
    s->ctx = ctx;
    return true;
}

// The text subtitle tracks (SubRip, ASS, mov_text, WebVTT), up to
// VIDEO_SUB_TRACKS; picture ones (DVD, Blu-ray) aren't drawn.
static void openSubtitles(Video *v)
{
    if (!v->max_threads)  // Not for thumbnails.
        openStyledSubtitles(v);
    for (unsigned i = 0; i < v->fmt->nb_streams && v->sub_count < VIDEO_SUB_TRACKS; i++) {
        AVStream *st = v->fmt->streams[i];
        const AVCodecDescriptor *desc = avcodec_descriptor_get(st->codecpar->codec_id);
        if (st->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE || !desc || !(desc->props & AV_CODEC_PROP_TEXT_SUB))
            continue;
        SubStream *s = &v->subs[v->sub_count];
        if (!openSubStream(v, s, st))
            continue;
        s->stream = (int)i;
        s->preferred = st->disposition & (AV_DISPOSITION_DEFAULT | AV_DISPOSITION_FORCED);
        const AVDictionaryEntry *title = av_dict_get(st->metadata, "title", NULL, 0);
        const AVDictionaryEntry *lang = av_dict_get(st->metadata, "language", NULL, 0);
        subsTrackName(s->label, sizeof(s->label), title ? title->value : NULL, lang ? lang->value : NULL,
                      v->sub_count + 1);
        v->sub_count++;
    }
}

static void openChapters(Video *v)
{
    for (unsigned i = 0; i < v->fmt->nb_chapters && v->chapter_count < VIDEO_CHAPTERS; i++) {
        const AVChapter *c = v->fmt->chapters[i];
        const AVDictionaryEntry *title = av_dict_get(c->metadata, "title", NULL, 0);
        PlaybackChapter *out = &v->chapters[v->chapter_count++];
        int64_t start = av_rescale_q(c->start, c->time_base, (AVRational){1, 1000000000});
        out->start_ns = start > 0 ? start : 0;
        snprintf(out->title, sizeof(out->title), "%s", title ? title->value : "");
    }
    playbackChaptersPrepare(v->chapters, v->chapter_count);
}

// Without the memory, seeks just don't show a picture on the way. Not for
// thumbnails (max_threads), which want the target itself.
static void allocatePreview(Video *v)
{
    v->preview.y = malloc((size_t)v->width * v->height);
    v->preview.uv = malloc((size_t)v->width * v->height / 2);
    if (!v->preview.y || !v->preview.uv) {
        free(v->preview.y);
        free(v->preview.uv);
        v->preview = (Slot){0};
    }
}

Video *videoOpen(const char *path, char *error, size_t error_size)
{
    return videoOpenLimited(path, 0, error, error_size);
}

Video *videoOpenLimited(const char *path, int max_threads, char *error, size_t error_size)
{
    Video *v = calloc(1, sizeof(*v));
    if (!v) {
        snprintf(error, error_size, "%s", T("Out of memory."));
        return NULL;
    }
    pthread_mutex_init(&v->lock, NULL);
    pthread_cond_init(&v->wake, NULL);
    v->clock_ns = SWFRAME_NO_CLOCK;
    v->max_threads = max_threads;
    v->fmt = avformat_alloc_context();
    if (!v->fmt) {
        snprintf(error, error_size, "%s", T("Out of memory."));
        videoClose(v);
        return NULL;
    }
    if (hlsIsLocation(path))
        v->fmt->interrupt_callback = (AVIOInterruptCB){readInterrupted, v};
    if (!mediaInputOpen(&v->input, &v->fmt, path, IO_BUFFER)) {
        snprintf(error, error_size, "%s", T("This isn't a video KaiVideo can read (MP4, MKV or WebM)."));
        videoClose(v);
        return NULL;
    }
    v->stream = av_find_best_stream(v->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (v->stream < 0) {
        snprintf(error, error_size, "%s", T("The file has no video."));
        videoClose(v);
        return NULL;
    }
    AVStream *st = v->fmt->streams[v->stream];
    AVCodecParameters *par = st->codecpar;
    v->time_base = st->time_base;
    v->time_shift = av_rescale_q(v->input.start_us, AV_TIME_BASE_Q, st->time_base);
    v->duration_ns = v->fmt->duration > 0 ? av_rescale(v->fmt->duration + v->input.skipped_us, 1000000000, AV_TIME_BASE)
                                          : 0;
    v->width = par->width & ~1;
    v->height = par->height & ~1;
    AVRational sar = av_guess_sample_aspect_ratio(v->fmt, st, NULL);
    v->aspect = (double)v->width / v->height * (sar.num > 0 && sar.den > 0 ? av_q2d(sar) : 1.0);
    v->bt709 = par->color_space == AVCOL_SPC_BT709 ||
               (par->color_space == AVCOL_SPC_UNSPECIFIED && par->height >= 720);
    v->full_range = par->color_range == AVCOL_RANGE_JPEG;
    for (int i = 0; i <= VIDEO_QUEUE; i++) {
        Slot *s = i < VIDEO_QUEUE ? &v->slots[i] : &v->shown;
        s->y = malloc((size_t)v->width * v->height);
        s->uv = malloc((size_t)v->width * v->height / 2);
        if (!s->y || !s->uv) {
            snprintf(error, error_size, "%s", T("Not enough memory for this video's frames."));
            videoClose(v);
            return NULL;
        }
    }
    if (!openDecoder(v, st, error, error_size)) {
        videoClose(v);
        return NULL;
    }
    if (v->software && !max_threads)
        allocatePreview(v);
    openSubtitles(v);
    openChapters(v);
    for (unsigned i = 0; i < v->fmt->nb_streams && v->audio_count < VIDEO_AUDIO_TRACKS; i++) {
        AVStream *as = v->fmt->streams[i];
        if (as->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
            continue;
        const AVDictionaryEntry *title = av_dict_get(as->metadata, "title", NULL, 0);
        const AVDictionaryEntry *lang = av_dict_get(as->metadata, "language", NULL, 0);
        subsTrackName(v->audio_names[v->audio_count], sizeof(v->audio_names[0]), title ? title->value : NULL,
                      lang ? lang->value : NULL, v->audio_count + 1);
        v->audio_count++;
    }
    if (pthread_create(&v->thread, NULL, decodeThread, v) != 0) {
        snprintf(error, error_size, "%s", T("The decoding thread can't start."));
        videoClose(v);
        return NULL;
    }
    v->thread_started = true;
    return v;
}

void videoClose(Video *v)
{
    if (!v)
        return;
    if (v->thread_started) {
        pthread_mutex_lock(&v->lock);
        v->stop = true;
        pthread_cond_broadcast(&v->wake);
        pthread_mutex_unlock(&v->lock);
        pthread_join(v->thread, NULL);
    }
    if (v->decoder_open)
        videoDecoderClose(&v->decoder);
    avcodec_free_context(&v->software);
    av_frame_free(&v->software_frame);
    av_bsf_free(&v->bsf);
    for (int i = 0; i < v->sub_count; i++) {
        avcodec_free_context(&v->subs[i].ctx);
        subsFree(&v->subs[i].track);
    }
    assSubsClose(v->styled);
    if (v->fmt)
        avformat_close_input(&v->fmt);
    mediaInputClose(&v->input);
    for (int i = 0; i <= VIDEO_QUEUE; i++) {
        Slot *s = i < VIDEO_QUEUE ? &v->slots[i] : &v->shown;
        free(s->y);
        free(s->uv);
    }
    free(v->preview.y);
    free(v->preview.uv);
    pthread_cond_destroy(&v->wake);
    pthread_mutex_destroy(&v->lock);
    free(v);
}

// --- Showing ---

const char *videoDescription(const Video *v)
{
    return v->description;
}

void videoSize(const Video *v, int *width, int *height, double *aspect)
{
    *width = v->width;
    *height = v->height;
    *aspect = v->aspect;
}

int64_t videoDuration(const Video *v)
{
    return v->duration_ns;
}

int64_t videoStartNs(const Video *v)
{
    return v->input.start_us * 1000;
}

const GfxVideoFrame *videoFrameAt(Video *v, int64_t media_ns)
{
    bool taken = false, previewed = false;
    pthread_mutex_lock(&v->lock);
    v->clock_ns = media_ns;
    // Every frame whose time has come: the latest is shown, the others were late.
    while (v->count > 0 && v->slots[v->head].pts_ns <= media_ns) {
        if (taken)
            v->stats.dropped++;
        Slot due = v->slots[v->head];
        v->slots[v->head] = v->shown;  // The slot gets the old frame's buffers.
        v->shown = due;
        v->head = (v->head + 1) % VIDEO_QUEUE;
        v->count--;
        taken = true;
    }
    if (taken) {
        v->stats.shown++;
        pthread_cond_signal(&v->wake);
    } else if (v->preview_ready) {
        Slot picture = v->preview;
        v->preview = v->shown;
        v->shown = picture;
        v->stats.previews++;
        previewed = true;
    }
    v->preview_ready = false;
    pthread_mutex_unlock(&v->lock);
    if (taken || previewed) {
        v->frame = (GfxVideoFrame){
            .width = v->width, .height = v->height, .y = v->shown.y, .uv = v->shown.uv,
            .y_pitch = v->width, .uv_pitch = v->width, .id = ++v->frame_id,
            .bt709 = v->bt709, .full_range = v->full_range,
        };
    }
    return v->frame_id ? &v->frame : NULL;
}

bool videoNextPts(Video *v, int64_t *pts_ns)
{
    pthread_mutex_lock(&v->lock);
    bool have = v->count > 0;
    if (have)
        *pts_ns = v->slots[v->head].pts_ns;
    pthread_mutex_unlock(&v->lock);
    return have;
}

void videoSeek(Video *v, int64_t pts_ns)
{
    pthread_mutex_lock(&v->lock);
    v->seek_requested = true;
    v->seek_ns = pts_ns < 0 ? 0 : pts_ns;
    v->clock_ns = SWFRAME_NO_CLOCK;
    v->preview_ready = false;
    v->count = 0;  // What's queued is from before; the thread empties it properly too.
    pthread_cond_broadcast(&v->wake);
    pthread_mutex_unlock(&v->lock);
}

bool videoFinished(Video *v, char *error, size_t error_size)
{
    pthread_mutex_lock(&v->lock);
    bool finished = v->failed || (v->done && v->count == 0 && !v->seek_requested);
    if (v->failed && error)
        snprintf(error, error_size, "%s", v->error);
    else if (error && error_size)
        error[0] = '\0';
    pthread_mutex_unlock(&v->lock);
    return finished;
}

VideoStats videoStats(const Video *v)
{
    VideoStats s = v->stats;
    s.copy_ms = s.decoded ? v->copy_total_ms / s.decoded : 0.0;
    return s;
}

// --- Subtitles ---

int videoSubtitleCount(const Video *v)
{
    return v->sub_count;
}

const char *videoSubtitleLabel(const Video *v, int track)
{
    return track >= 0 && track < v->sub_count ? v->subs[track].label : "";
}

int videoSubtitlePreferred(const Video *v)
{
    for (int i = 0; i < v->sub_count; i++)
        if (v->subs[i].preferred)
            return i;
    return -1;
}

bool videoSubtitleAt(Video *v, int track, int64_t pos_ns, char *out, size_t size)
{
    if (track < 0 || track >= v->sub_count)
        return false;
    pthread_mutex_lock(&v->lock);
    bool on = subsAt(&v->subs[track].track, pos_ns, out, size);
    pthread_mutex_unlock(&v->lock);
    return on;
}

bool videoSubtitleStyled(const Video *v, int track)
{
    return track >= 0 && track < v->sub_count && v->subs[track].styled_track != NO_STYLED_TRACK;
}

bool videoSubtitlePicture(Video *v, int track, int64_t pos_ns, const AssArea *area, AssPicture *out)
{
    if (!videoSubtitleStyled(v, track))
        return false;
    return assSubsRender(v->styled, v->subs[track].styled_track, pos_ns / 1000000, area, out);
}

// --- Chapters ---

const PlaybackChapter *videoChapters(const Video *v, int *count)
{
    *count = v->chapter_count;
    return v->chapters;
}

// --- Sound tracks ---

int videoAudioTrackCount(const Video *v)
{
    return v->audio_count;
}

const char *videoAudioTrackName(const Video *v, int track)
{
    return track >= 0 && track < v->audio_count ? v->audio_names[track] : "";
}
