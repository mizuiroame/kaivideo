#include "video.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
#include <switch.h>

#include "subs.h"

#define IO_BUFFER (256 * 1024)
#define SUBMIT_TIMEOUT_NS 2000000ull
#define SUBMIT_TRIES 2500             // About 5 seconds of retries before giving up.
#define TAIL_TIMEOUT_NS 300000000ull  // After the last sample: how long to wait for frames.

#define SUB_TEXT_MAX 1024
#define SUB_DEFAULT_NS 4000000000ll  // A line without an end shows this long.

typedef struct {
    uint8_t *y, *uv;   // NV12, width bytes a row.
    int64_t pts_ns;
} Slot;

// A text subtitle track inside the file, decoded as it is read.
typedef struct {
    int stream;
    AVCodecContext *ctx;
    SubTrack track;    // Under the Video's lock.
    char label[64];
    bool preferred;    // Marked default or forced in the file.
} SubStream;

struct Video {
    FILE *file;
    AVFormatContext *fmt;
    AVIOContext *io;
    AVBSFContext *bsf;
    int stream;
    AVRational time_base;
    int64_t duration_ns;
    VideoDecoder decoder;
    bool decoder_open;
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
    bool done;                     // The thread has decoded all there is.
    bool failed;
    char error[128];

    unsigned frame_id;
    VideoStats stats;
    double copy_total_ms;

    SubStream subs[VIDEO_SUB_TRACKS];
    int sub_count;
    char audio_names[VIDEO_AUDIO_TRACKS][64];
    int audio_count;
};

// --- Reading the file (FFmpeg has no file protocol in this build) ---

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

// Moves every frame the decoder has ready into the queue, waiting up to
// timeout_ns for the first; waits for room when the queue is full. False when interrupted.
static bool takeFrames(Video *v, u64 timeout_ns)
{
    VideoFrame f;
    while (R_SUCCEEDED(videoDecoderGetFrame(&v->decoder, &f, timeout_ns))) {
        timeout_ns = 0;
        if (f.timestamp_ns < v->skip_until_ns) {  // Before a seek's target: decoded only to get there.
            videoDecoderReleaseFrame(&v->decoder, &f);
            continue;
        }
        pthread_mutex_lock(&v->lock);
        while (v->count == VIDEO_QUEUE && !v->stop && !v->seek_requested)
            pthread_cond_wait(&v->wake, &v->lock);
        bool stop = v->stop || v->seek_requested;
        Slot *s = &v->slots[(v->head + v->count) % VIDEO_QUEUE];
        pthread_mutex_unlock(&v->lock);
        if (stop) {
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
        pthread_mutex_lock(&v->lock);
        bool seeking = v->seek_requested;  // A seek came while copying: this frame is from before.
        if (!seeking) {
            v->count++;
            v->stats.decoded++;
            v->copy_total_ms += ms;
        }
        pthread_mutex_unlock(&v->lock);
        if (seeking)
            return false;
    }
    return !interrupted(v);
}

static bool submit(Video *v, const AVPacket *p, bool last)
{
    int64_t ts = p->pts != AV_NOPTS_VALUE ? p->pts : p->dts != AV_NOPTS_VALUE ? p->dts : 0;
    s64 ns = av_rescale_q(ts, v->time_base, (AVRational){1, 1000000000});
    for (int tries = 0; tries < SUBMIT_TRIES; tries++) {
        if (!takeFrames(v, 0))
            return false;
        Result rc = videoDecoderSubmit(&v->decoder, p->data, p->size, ns, last, SUBMIT_TIMEOUT_NS);
        if (R_SUCCEEDED(rc))
            return true;
        if (rc != KERNELRESULT(TimedOut) && rc != VIDDEC_RESULT_SUBMIT_BUSY) {
            char message[96];
            snprintf(message, sizeof(message), "The video decoder refused the video (0x%x).", rc);
            fail(v, message);
            return false;
        }
    }
    if (!interrupted(v))
        fail(v, "The video decoder stopped taking the video.");
    return false;
}

// Goes to the keyframe before target and drops everything decoded or queued.
static void doSeek(Video *v, int64_t target_ns)
{
    int64_t ts = av_rescale_q(target_ns, (AVRational){1, 1000000000}, v->time_base);
    av_seek_frame(v->fmt, v->stream, ts, AVSEEK_FLAG_BACKWARD);
    if (v->bsf)
        av_bsf_flush(v->bsf);
    videoDecoderFlush(&v->decoder);
    for (int i = 0; i < v->sub_count; i++)
        avcodec_flush_buffers(v->subs[i].ctx);
    v->skip_until_ns = target_ns;
    pthread_mutex_lock(&v->lock);
    v->head = 0;
    v->count = 0;
    v->done = false;
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
        if (pkt->stream_index != v->stream) {
            SubStream *s = subStreamFor(v, pkt->stream_index);
            if (s)
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
    bool ok = !interrupted(v) && (!have_held || submit(v, held, true));
    av_packet_unref(held);
    return ok && takeFrames(v, TAIL_TIMEOUT_NS);
}

static void *decodeThread(void *arg)
{
    Video *v = arg;
    AVPacket *pkt = av_packet_alloc(), *held = av_packet_alloc();
    if (!pkt || !held)
        fail(v, "Out of memory.");
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
    default: return avcodec_get_name(id);
    }
}

static bool openDecoder(Video *v, AVStream *st, char *error, size_t error_size)
{
    AVCodecParameters *par = st->codecpar;
    ViddecCodec codec;
    const char *filter = NULL;
    switch (par->codec_id) {
    case AV_CODEC_ID_H264: codec = ViddecCodec_H264; filter = "h264_mp4toannexb"; break;
    case AV_CODEC_ID_VP9: codec = ViddecCodec_Vp9; filter = "vp9_superframe_split"; break;
    case AV_CODEC_ID_AV1: codec = ViddecCodec_Av1; break;
    case AV_CODEC_ID_VP8: codec = ViddecCodec_Vp8; break;
    default:
        snprintf(error, error_size, "%s video can't be played: the console decodes H.264, VP9, AV1 and VP8.",
                 codecName(par->codec_id));
        return false;
    }
    if (filter) {
        const AVBitStreamFilter *spec = av_bsf_get_by_name(filter);
        if (!spec || av_bsf_alloc(spec, &v->bsf) < 0 || avcodec_parameters_copy(v->bsf->par_in, par) < 0 ||
            (v->bsf->time_base_in = st->time_base, av_bsf_init(v->bsf) < 0)) {
            snprintf(error, error_size, "The video can't be prepared for the decoder.");
            return false;
        }
    }
    double fps = av_q2d(av_guess_frame_rate(v->fmt, st, NULL));
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
        snprintf(error, error_size, "The console's decoder can't play this %s video (%dx%d, 0x%x).",
                 codecName(par->codec_id), par->width, par->height, rc);
        return false;
    }
    v->decoder_open = true;
    snprintf(v->description, sizeof(v->description), "%s %dx%d, %.3g fps", codecName(par->codec_id), par->width,
             par->height, fps);
    return true;
}

// The text subtitle tracks (SubRip, ASS, mov_text, WebVTT), up to
// VIDEO_SUB_TRACKS; picture ones (DVD, Blu-ray) aren't drawn.
static void openSubtitles(Video *v)
{
    for (unsigned i = 0; i < v->fmt->nb_streams && v->sub_count < VIDEO_SUB_TRACKS; i++) {
        AVStream *st = v->fmt->streams[i];
        const AVCodecDescriptor *desc = avcodec_descriptor_get(st->codecpar->codec_id);
        if (st->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE || !desc || !(desc->props & AV_CODEC_PROP_TEXT_SUB))
            continue;
        const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
        AVCodecContext *ctx = codec ? avcodec_alloc_context3(codec) : NULL;
        if (!ctx)
            continue;
        ctx->pkt_timebase = st->time_base;
        if (avcodec_parameters_to_context(ctx, st->codecpar) < 0 || avcodec_open2(ctx, codec, NULL) < 0) {
            avcodec_free_context(&ctx);
            continue;
        }
        SubStream *s = &v->subs[v->sub_count];
        s->stream = (int)i;
        s->ctx = ctx;
        s->preferred = st->disposition & (AV_DISPOSITION_DEFAULT | AV_DISPOSITION_FORCED);
        const AVDictionaryEntry *title = av_dict_get(st->metadata, "title", NULL, 0);
        const AVDictionaryEntry *lang = av_dict_get(st->metadata, "language", NULL, 0);
        subsTrackName(s->label, sizeof(s->label), title ? title->value : NULL, lang ? lang->value : NULL,
                      v->sub_count + 1);
        v->sub_count++;
    }
}

Video *videoOpen(const char *path, char *error, size_t error_size)
{
    Video *v = calloc(1, sizeof(*v));
    if (!v) {
        snprintf(error, error_size, "Out of memory.");
        return NULL;
    }
    pthread_mutex_init(&v->lock, NULL);
    pthread_cond_init(&v->wake, NULL);
    v->file = fopen(path, "rb");
    if (!v->file) {
        snprintf(error, error_size, "The file can't be opened.");
        videoClose(v);
        return NULL;
    }
    uint8_t *buffer = av_malloc(IO_BUFFER);
    v->io = buffer ? avio_alloc_context(buffer, IO_BUFFER, 0, v->file, readFile, NULL, seekFile) : NULL;
    v->fmt = avformat_alloc_context();
    if (!v->io || !v->fmt) {
        if (!v->io)
            av_free(buffer);
        snprintf(error, error_size, "Out of memory.");
        videoClose(v);
        return NULL;
    }
    v->fmt->pb = v->io;
    v->fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
    if (avformat_open_input(&v->fmt, NULL, NULL, NULL) < 0 || avformat_find_stream_info(v->fmt, NULL) < 0) {
        snprintf(error, error_size, "This isn't a video KaiVideo can read (MP4, MKV or WebM).");
        videoClose(v);
        return NULL;
    }
    v->stream = av_find_best_stream(v->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (v->stream < 0) {
        snprintf(error, error_size, "The file has no video.");
        videoClose(v);
        return NULL;
    }
    AVStream *st = v->fmt->streams[v->stream];
    AVCodecParameters *par = st->codecpar;
    v->time_base = st->time_base;
    v->duration_ns = v->fmt->duration > 0 ? av_rescale(v->fmt->duration, 1000000000, AV_TIME_BASE) : 0;
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
            snprintf(error, error_size, "Not enough memory for this video's frames.");
            videoClose(v);
            return NULL;
        }
    }
    if (!openDecoder(v, st, error, error_size)) {
        videoClose(v);
        return NULL;
    }
    openSubtitles(v);
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
        snprintf(error, error_size, "The decoding thread can't start.");
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
    av_bsf_free(&v->bsf);
    for (int i = 0; i < v->sub_count; i++) {
        avcodec_free_context(&v->subs[i].ctx);
        subsFree(&v->subs[i].track);
    }
    if (v->fmt)
        avformat_close_input(&v->fmt);
    if (v->io) {
        av_freep(&v->io->buffer);
        avio_context_free(&v->io);
    }
    if (v->file)
        fclose(v->file);
    for (int i = 0; i <= VIDEO_QUEUE; i++) {
        Slot *s = i < VIDEO_QUEUE ? &v->slots[i] : &v->shown;
        free(s->y);
        free(s->uv);
    }
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

const GfxVideoFrame *videoFrameAt(Video *v, int64_t media_ns)
{
    bool taken = false;
    pthread_mutex_lock(&v->lock);
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
    }
    pthread_mutex_unlock(&v->lock);
    if (taken) {
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

// --- Sound tracks ---

int videoAudioTrackCount(const Video *v)
{
    return v->audio_count;
}

const char *videoAudioTrackName(const Video *v, int track)
{
    return track >= 0 && track < v->audio_count ? v->audio_names[track] : "";
}
