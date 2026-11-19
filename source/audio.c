#include "mediafile.h"
#include "audio.h"
#include "clock.h"
#include "hlsio.h"
#include "playback.h"
#include "video.h"

#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/dict.h>
#include <libswresample/swresample.h>
#include <switch.h>

#define IO_BUFFER (64 * 1024)
#define BUFFER_COUNT 6              // A little in hand, for when the network or the CPU is busy.
#define BUFFER_FRAMES 2048           // About 43 ms at 48 kHz: the played count moves in these steps (see clock.h).
#define BUFFER_BYTES (BUFFER_FRAMES * 2 * sizeof(s16))
#define FADE_FRAMES 480              // 10 ms at 48 kHz: in and out of silence without a click.
#define BUFFER_ALIGN 0x1000
#define WAIT_TIMEOUT_NS 10000000ull
#define THREAD_STACK_SIZE 0x40000
#define THREAD_PRIORITY 0x2B
// Only these are opened while reading the file's format: not the video decoders.
#define AUDIO_DECODERS "aac,mp3float,mp3,vorbis,opus,flac,ac3,eac3,dca,truehd,mlp"
#define NO_TRACK (-1)
#define RING_PACKETS 1024             // Of each sound track, the latest kept for changing to it.
#define RING_SECONDS 8
#define NS_TIME_BASE ((AVRational){1, 1000000000})

static u32 g_out_rate;
static bool g_ready;

// The latest packets read of a sound track, playing or not: changing to it
// starts from these instead of going back in the file.
typedef struct {
    AVPacket *items[RING_PACKETS];
    int head, count;
} PacketRing;

struct Audio {
    MediaInput input;
    int64_t seek_shift;  // Taken from the seek's stream's times (shiftPacket does the sound's).
    AVFormatContext *fmt;
    AVCodecContext *codec;
    SwrContext *swr;
    int stream;
    AVRational time_base;
    // Seeks go by this stream: the video's, which the file's index (MKV cues)
    // always covers. Seeking by the sound's own stream, often left out of
    // the index, reads the file up to the target: the longer the jump, the
    // longer the wait. The sound then skips what comes before the target.
    int seek_stream;
    AVRational seek_time_base;
    AVPacket *pkt;
    AVFrame *frame;
    char description[64];
    int tracks[VIDEO_AUDIO_TRACKS];  // The file's sound streams, in order.
    int track_count, track;          // track: the one playing, an index into tracks.
    PacketRing rings[VIDEO_AUDIO_TRACKS];  // Only the sound thread touches these.
    int replay_index, replay_left;   // Kept packets of the track to decode before reading on.

    AudioOutBuffer buffers[BUFFER_COUNT];
    bool queued[BUFFER_COUNT];
    s16 *pending;                 // Converted sound not yet in a buffer.
    size_t pending_frames, pending_capacity;
    int64_t pending_pts_ns;       // Time of pending's first frame.

    // Another speed than normal: converted sound goes through FFmpeg's atempo
    // filter on its way to pending. Only the sound thread touches these.
    AVFilterGraph *tempo_graph;
    AVFilterContext *tempo_in, *tempo_out;
    AVFrame *tempo_frame;
    s16 *source;                  // Converted sound on its way into the filter.
    size_t source_frames, source_capacity;
    int64_t source_pts_ns;
    bool tempo_started, tempo_drained;
    int64_t tempo_base_ns;        // The file's time of the filter's first sound.
    int64_t tempo_input, tempo_output;  // Frames into and out of the filter.

    Thread thread;
    bool thread_started;
    Mutex lock;
    volatile bool stop;
    bool paused;
    bool seek_requested;
    bool seek_pending;            // Asked for and not finished: what's queued meanwhile is from before it.
    int64_t seek_ns;
    int track_wanted;             // A track to change to, or NO_TRACK. Under lock, with track_from_ns.
    int64_t track_from_ns;
    char switch_error[96];        // Why the last change failed. Under lock.
    int64_t skip_until_ns;        // After a seek: sound before this is dropped.
    int speed, speed_wanted;      // Thousandths; speed changes with a seek, under lock.
    bool read_done, finished;
    bool read_failed;             // The network went. Under lock.

    // The clock: base_pts is the time of the first frame queued since the
    // last flush, base_played the output's played count when it was queued.
    bool clock_valid;
    int64_t base_pts_ns;
    u64 base_played;
    u64 queued_frames;            // Frames queued since the base.
    SmoothClock smooth;           // The clock as the picture reads it (clock.h).
    bool primed;                  // Paused with sound decoded: it starts at primed_pts.
    int64_t primed_pts_ns;
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

// Room for frames more stereo frames after the used ones.
static bool reserveFrames(s16 **data, size_t *capacity, size_t used, size_t frames)
{
    if (used + frames <= *capacity)
        return true;
    size_t wanted = (used + frames) * 2;
    s16 *grown = realloc(*data, wanted * 2 * sizeof(s16));
    if (!grown)
        return false;
    *data = grown;
    *capacity = wanted;
    return true;
}

// --- Another speed: FFmpeg's atempo filter ---

static void tempoClose(Audio *a)
{
    avfilter_graph_free(&a->tempo_graph);
    a->tempo_in = a->tempo_out = NULL;
}

static bool tempoOpen(Audio *a, int permille)
{
    char source[96], tempo[32];
    snprintf(source, sizeof(source), "time_base=1/%u:sample_rate=%u:sample_fmt=s16:channel_layout=stereo", g_out_rate,
             g_out_rate);
    snprintf(tempo, sizeof(tempo), "tempo=%d.%03d", permille / PLAYBACK_PERMILLE, permille % PLAYBACK_PERMILLE);
    AVFilterContext *filter = NULL;
    a->tempo_graph = avfilter_graph_alloc();
    if (!a->tempo_frame)
        a->tempo_frame = av_frame_alloc();
    bool ok = a->tempo_graph && a->tempo_frame &&
              avfilter_graph_create_filter(&a->tempo_in, avfilter_get_by_name("abuffer"), "in", source, NULL,
                                           a->tempo_graph) >= 0 &&
              avfilter_graph_create_filter(&filter, avfilter_get_by_name("atempo"), "tempo", tempo, NULL,
                                           a->tempo_graph) >= 0 &&
              avfilter_graph_create_filter(&a->tempo_out, avfilter_get_by_name("abuffersink"), "out", NULL, NULL,
                                           a->tempo_graph) >= 0 &&
              avfilter_link(a->tempo_in, 0, filter, 0) >= 0 && avfilter_link(filter, 0, a->tempo_out, 0) >= 0 &&
              avfilter_graph_config(a->tempo_graph, NULL) >= 0;
    if (!ok)
        tempoClose(a);
    return ok;
}

// Moves what the filter has ready into pending. A frame out of it stands for
// speed times its length of the file, counted from the filter's first sound.
static void tempoPull(Audio *a)
{
    AVFrame *f = a->tempo_frame;
    while (av_buffersink_get_frame(a->tempo_out, f) >= 0) {
        size_t n = (size_t)f->nb_samples;
        if (reserveFrames(&a->pending, &a->pending_capacity, a->pending_frames, n)) {
            if (a->pending_frames == 0)
                a->pending_pts_ns = a->tempo_base_ns + av_rescale(a->tempo_output * a->speed, 1000000, g_out_rate);
            memcpy(a->pending + a->pending_frames * 2, f->data[0], n * 2 * sizeof(s16));
            a->pending_frames += n;
        }
        a->tempo_output += (int64_t)n;
        av_frame_unref(f);
    }
}

// Sends the converted sound through the filter.
static void tempoFeed(Audio *a)
{
    AVFrame *f = a->tempo_frame;
    if (a->source_frames == 0)
        return;
    if (!a->tempo_started) {
        a->tempo_started = true;
        a->tempo_base_ns = a->source_pts_ns;
    }
    av_frame_unref(f);
    f->nb_samples = (int)a->source_frames;
    f->format = AV_SAMPLE_FMT_S16;
    f->sample_rate = (int)g_out_rate;
    f->pts = a->tempo_input;
    f->ch_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;
    if (av_frame_get_buffer(f, 0) >= 0) {
        memcpy(f->data[0], a->source, a->source_frames * 2 * sizeof(s16));
        a->tempo_input += (int64_t)a->source_frames;
        if (av_buffersrc_add_frame(a->tempo_in, f) < 0)
            av_frame_unref(f);  // That bit of sound is lost; the clock counts from the filter's output.
    }
    a->source_frames = 0;
    tempoPull(a);
}

// At the end of the file: the filter gives up what it still holds.
static void tempoDrain(Audio *a)
{
    if (!a->tempo_graph || a->tempo_drained)
        return;
    a->tempo_drained = true;
    tempoFeed(a);
    if (av_buffersrc_add_frame(a->tempo_in, NULL) >= 0)
        tempoPull(a);
}

// Starts the speed asked for, with nothing of the old one left in the filter.
// Returns the speed that could be set up.
static int tempoReset(Audio *a, int permille)
{
    tempoClose(a);
    if (permille != PLAYBACK_PERMILLE && !tempoOpen(a, permille))
        permille = PLAYBACK_PERMILLE;  // Sound at normal speed beats none: the picture follows it.
    a->source_frames = 0;
    a->tempo_started = a->tempo_drained = false;
    a->tempo_input = a->tempo_output = 0;
    return permille;
}

// Adds a decoded frame as output sound, dropping what's before skip_until: to
// pending, or at another speed on its way through the filter.
static void addFrame(Audio *a, AVFrame *f)
{
    if (!prepareResampler(a, f))
        return;
    int64_t pts = f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts;
    int64_t pts_ns = pts != AV_NOPTS_VALUE ? av_rescale_q(pts, a->time_base, (AVRational){1, 1000000000}) : -1;
    int out = swr_get_out_samples(a->swr, f->nb_samples);
    if (out <= 0)
        return;
    bool tempo = a->tempo_graph != NULL;
    s16 **data = tempo ? &a->source : &a->pending;
    size_t *frames = tempo ? &a->source_frames : &a->pending_frames;
    size_t *capacity = tempo ? &a->source_capacity : &a->pending_capacity;
    int64_t *start_ns = tempo ? &a->source_pts_ns : &a->pending_pts_ns;
    if (!reserveFrames(data, capacity, *frames, (size_t)out))
        return;
    uint8_t *dst = (uint8_t *)(*data + *frames * 2);
    int n = swr_convert(a->swr, &dst, out, (const uint8_t **)f->extended_data, f->nb_samples);
    if (n <= 0)
        return;
    if (*frames == 0 && pts_ns >= 0)
        *start_ns = pts_ns;
    *frames += (size_t)n;
    // After a seek, drop sound before the target (seeks land on an earlier packet).
    if (a->skip_until_ns > 0 && *start_ns >= 0 && *start_ns < a->skip_until_ns) {
        size_t drop = (size_t)((a->skip_until_ns - *start_ns) * (int64_t)g_out_rate / 1000000000);
        if (drop > *frames)
            drop = *frames;
        memmove(*data, *data + drop * 2, (*frames - drop) * 2 * sizeof(s16));
        *frames -= drop;
        *start_ns += (int64_t)drop * 1000000000 / g_out_rate;
        if (*frames > 0)
            a->skip_until_ns = 0;
    }
    if (tempo)
        tempoFeed(a);
}

// --- The packets kept of each track ---

static AVPacket *ringAt(PacketRing *r, int index)
{
    return r->items[(r->head + index) % RING_PACKETS];
}

static void ringDropOldest(PacketRing *r)
{
    av_packet_unref(r->items[r->head]);
    r->head = (r->head + 1) % RING_PACKETS;
    r->count--;
}

static int64_t packetTime(const AVPacket *p)
{
    return p->pts != AV_NOPTS_VALUE ? p->pts : p->dts;
}

static int trackOfStream(const Audio *a, int stream)
{
    for (int i = 0; i < a->track_count; i++)
        if (a->tracks[i] == stream)
            return i;
    return NO_TRACK;
}

static void ringKeep(Audio *a, const AVPacket *p)
{
    int track = trackOfStream(a, p->stream_index);
    if (track == NO_TRACK)
        return;
    PacketRing *r = &a->rings[track];
    AVRational tb = a->fmt->streams[p->stream_index]->time_base;
    int64_t window = av_rescale_q(RING_SECONDS, (AVRational){1, 1}, tb), now = packetTime(p);
    while (r->count && (r->count == RING_PACKETS || (now != AV_NOPTS_VALUE && now - packetTime(ringAt(r, 0)) > window)))
        ringDropOldest(r);
    int slot = (r->head + r->count) % RING_PACKETS;
    if (!r->items[slot])
        r->items[slot] = av_packet_alloc();
    if (r->items[slot] && av_packet_ref(r->items[slot], p) >= 0)
        r->count++;
}

static void ringsClear(Audio *a)
{
    for (int t = 0; t < VIDEO_AUDIO_TRACKS; t++)
        while (a->rings[t].count)
            ringDropOldest(&a->rings[t]);
    a->replay_left = 0;
}

static void ringsFree(Audio *a)
{
    for (int t = 0; t < VIDEO_AUDIO_TRACKS; t++)
        for (int i = 0; i < RING_PACKETS; i++)
            av_packet_free(&a->rings[t].items[i]);
}

// Converted and live streams don't start at 0: every sound track's packets
// (those kept for a change of track too) go back by where playback's 0 is,
// in their own stream's time.
static void shiftPacket(const Audio *a, AVPacket *p)
{
    if (!a->input.start_us || trackOfStream(a, p->stream_index) == NO_TRACK)
        return;
    int64_t shift = av_rescale_q(a->input.start_us, AV_TIME_BASE_Q, a->fmt->streams[p->stream_index]->time_base);
    if (p->pts != AV_NOPTS_VALUE)
        p->pts -= shift;
    if (p->dts != AV_NOPTS_VALUE)
        p->dts -= shift;
}

// From the packet before the one holding from_ns: a decoder needs one to start well.
static void replayFrom(Audio *a, int64_t from_ns)
{
    PacketRing *r = &a->rings[a->track];
    AVRational tb = a->fmt->streams[a->stream]->time_base;
    int64_t from = av_rescale_q(from_ns, NS_TIME_BASE, tb);
    int first = 0;
    for (int i = 0; i < r->count; i++) {
        const AVPacket *p = ringAt(r, i);
        if (packetTime(p) + p->duration > from)
            break;
        first = i;
    }
    a->replay_index = first;
    a->replay_left = r->count - first;
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
        if (r == AVERROR_EOF || a->read_done) {
            tempoDrain(a);
            return a->pending_frames > 0;
        }
        if (a->replay_left > 0) {
            avcodec_send_packet(a->codec, ringAt(&a->rings[a->track], a->replay_index++));
            a->replay_left--;
            continue;
        }
        if (av_read_frame(a->fmt, a->pkt) < 0) {
            a->read_done = true;
            mutexLock(&a->lock);
            a->read_failed = mediaFileFailed(a->input.file);
            mutexUnlock(&a->lock);
            avcodec_send_packet(a->codec, NULL);  // Drain what's left.
            continue;
        }
        shiftPacket(a, a->pkt);
        ringKeep(a, a->pkt);
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
    a->pending_pts_ns += (int64_t)n * 1000000 * a->speed / g_out_rate;
    b->data_size = BUFFER_BYTES;
    b->data_offset = 0;
    if (R_FAILED(audoutAppendAudioOutBuffer(b)))
        return;
    a->queued[i] = true;
    mutexLock(&a->lock);
    if (!a->clock_valid && !a->seek_pending) {  // Sound from before a seek doesn't start the clock.
        a->clock_valid = true;
        a->base_pts_ns = pts;
        a->base_played = played();
        a->queued_frames = 0;
        smoothClockReset(&a->smooth);
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

// Opens the decoder for a stream. NULL, with a reason, if it can't be.
static AVCodecContext *openDecoder(const AVStream *st, char *error, size_t error_size)
{
    const AVCodec *decoder = avcodec_find_decoder(st->codecpar->codec_id);
    if (!decoder) {
        snprintf(error, error_size, "This video's sound (%s) can't be played.", avcodec_get_name(st->codecpar->codec_id));
        return NULL;
    }
    AVCodecContext *codec = avcodec_alloc_context3(decoder);
    AVDictionary *options = NULL;
    if (st->codecpar->codec_id == AV_CODEC_ID_DTS)  // The output is stereo: the lossless layer would only cost time.
        av_dict_set(&options, "core_only", "1", 0);
    bool ok = codec && avcodec_parameters_to_context(codec, st->codecpar) >= 0 &&
              avcodec_open2(codec, decoder, &options) >= 0;
    av_dict_free(&options);
    if (!ok) {
        snprintf(error, error_size, "This video's sound can't be played.");
        avcodec_free_context(&codec);
    }
    return codec;
}

static void describeSound(Audio *a, const AVStream *st)
{
    snprintf(a->description, sizeof(a->description), "%s, %d kHz, %d channel%s", avcodec_get_name(st->codecpar->codec_id),
             st->codecpar->sample_rate / 1000, st->codecpar->ch_layout.nb_channels,
             st->codecpar->ch_layout.nb_channels == 1 ? "" : "s");
}

// Sound starts anew from target_ns, with nothing of what came before.
static void restartFrom(Audio *a, int64_t target_ns)
{
    a->pending_frames = 0;
    a->read_done = false;
    a->skip_until_ns = target_ns > 0 ? target_ns : 0;
    mutexLock(&a->lock);
    int speed = a->speed_wanted;
    mutexUnlock(&a->lock);
    speed = tempoReset(a, speed);
    mutexLock(&a->lock);
    a->speed = speed;
    a->seek_pending = a->seek_requested;  // Another may have come meanwhile.
    a->clock_valid = false;
    a->finished = false;
    a->primed = false;
    mutexUnlock(&a->lock);
}

static void doSeek(Audio *a, int64_t target_ns)
{
    flushOutput(a);  // Already faded out and played out: nothing is cut.
    int64_t ts = av_rescale_q(target_ns, NS_TIME_BASE, a->seek_time_base) + a->seek_shift;
    av_seek_frame(a->fmt, a->seek_stream, ts, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(a->codec);
    ringsClear(a);  // What was kept is from another part of the file.
    restartFrom(a, target_ns);
}

// Another track of the same file: its kept packets first, then reading goes
// on from where it is, so nothing has to be read again.
static void switchTrack(Audio *a, int track, int64_t from_ns)
{
    char error[sizeof(a->switch_error)] = "";
    flushOutput(a);
    AVStream *st = track >= 0 && track < a->track_count ? a->fmt->streams[a->tracks[track]] : NULL;
    AVCodecContext *codec = st ? openDecoder(st, error, sizeof(error)) : NULL;
    if (codec) {
        avcodec_free_context(&a->codec);
        swr_free(&a->swr);
        a->codec = codec;
        a->stream = st->index;
        a->time_base = st->time_base;
    } else {
        avcodec_flush_buffers(a->codec);
    }
    mutexLock(&a->lock);
    if (codec) {
        a->track = track;
        describeSound(a, st);
    }
    snprintf(a->switch_error, sizeof(a->switch_error), "%s", codec ? "" : error[0] ? error : "That track can't be played.");
    mutexUnlock(&a->lock);
    restartFrom(a, from_ns);
    replayFrom(a, from_ns);
}

static void audioThread(void *arg)
{
    Audio *a = arg;
    bool ended = false, silent = true;  // silent: nothing sounding, the next buffer fades in.
    while (true) {
        mutexLock(&a->lock);
        bool stop = a->stop, paused = a->paused, seek = a->seek_requested;
        int64_t target = a->seek_ns, track_from = a->track_from_ns;
        int track = a->track_wanted;
        a->seek_requested = false;
        a->track_wanted = NO_TRACK;
        mutexUnlock(&a->lock);
        if ((stop || paused || seek || track != NO_TRACK) && !silent && !ended) {
            fadeOut(a);
            silent = true;
        }
        if (stop)
            break;
        if (track != NO_TRACK) {
            switchTrack(a, track, track_from);
            ended = false;
        }
        if (seek) {
            doSeek(a, target);
            ended = false;
        }
        if (paused && !ended && a->pending_frames < BUFFER_FRAMES && !a->read_done) {
            // Ready while paused: the first buffer decoded, so it starts at once.
            fill(a);
            mutexLock(&a->lock);
            a->primed = a->pending_frames > 0 && a->skip_until_ns == 0;
            a->primed_pts_ns = a->pending_pts_ns;
            // Sent past the end of the sound: there's nothing to get ready.
            if (a->read_done && a->pending_frames == 0 && !anyQueued(a))
                a->finished = true;
            mutexUnlock(&a->lock);
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

// For FFmpeg: a jump doesn't wait for a segment of the place being left.
static int readInterrupted(void *opaque)
{
    Audio *a = opaque;
    mutexLock(&a->lock);
    bool yes = a->stop || a->seek_requested;
    mutexUnlock(&a->lock);
    return yes;
}

// The file's sound streams, as video.c counts them (videoAudioTrackName).
static void listTracks(Audio *a)
{
    for (unsigned i = 0; i < a->fmt->nb_streams && a->track_count < VIDEO_AUDIO_TRACKS; i++)
        if (a->fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
            a->tracks[a->track_count++] = (int)i;
}

Audio *audioOpen(const char *path, int track, char *error, size_t error_size)
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
    a->track_wanted = NO_TRACK;
    a->fmt = avformat_alloc_context();
    a->pkt = av_packet_alloc();
    a->frame = av_frame_alloc();
    if (!a->fmt || !a->pkt || !a->frame) {
        audioClose(a);
        return NULL;
    }
    a->fmt->codec_whitelist = av_strdup(AUDIO_DECODERS);
    if (hlsIsLocation(path))
        a->fmt->interrupt_callback = (AVIOInterruptCB){readInterrupted, a};
    const AVCodec *decoder = NULL;
    if (!mediaInputOpen(&a->input, &a->fmt, path, IO_BUFFER)) {
        audioClose(a);
        return NULL;  // No sound: the video plays silent.
    }
    listTracks(a);
    if (track >= 0 && track < a->track_count) {
        a->stream = a->tracks[track];
        decoder = avcodec_find_decoder(a->fmt->streams[a->stream]->codecpar->codec_id);
    } else {
        a->stream = av_find_best_stream(a->fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
    }
    if (a->stream < 0) {
        audioClose(a);
        return NULL;
    }
    for (int i = 0; i < a->track_count; i++)
        if (a->tracks[i] == a->stream)
            a->track = i;
    AVStream *st = a->fmt->streams[a->stream];
    a->codec = openDecoder(st, error, error_size);
    if (!a->codec) {
        audioClose(a);
        return NULL;
    }
    a->time_base = st->time_base;
    a->seek_stream = av_find_best_stream(a->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (a->seek_stream < 0)
        a->seek_stream = a->stream;  // Sound only: its own.
    a->seek_time_base = a->fmt->streams[a->seek_stream]->time_base;
    a->seek_shift = av_rescale_q(a->input.start_us, AV_TIME_BASE_Q, a->seek_time_base);
    describeSound(a, st);
    for (int i = 0; i < BUFFER_COUNT; i++) {
        a->buffers[i].buffer = memalign(BUFFER_ALIGN, BUFFER_BYTES);
        a->buffers[i].buffer_size = BUFFER_BYTES;
        if (!a->buffers[i].buffer) {
            audioClose(a);
            return NULL;
        }
    }
    a->paused = true;  // Until the player says the picture is ready.
    a->speed = a->speed_wanted = PLAYBACK_PERMILLE;
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
    free(a->source);
    tempoClose(a);
    av_frame_free(&a->tempo_frame);
    swr_free(&a->swr);
    avcodec_free_context(&a->codec);
    av_packet_free(&a->pkt);
    av_frame_free(&a->frame);
    ringsFree(a);
    if (a->fmt)
        avformat_close_input(&a->fmt);
    mediaInputClose(&a->input);
    free(a);
}

const char *audioDescription(const Audio *a)
{
    return a->description;
}

bool audioClock(Audio *a, int64_t *pts_ns)
{
    mutexLock(&a->lock);
    u64 heard = a->clock_valid ? played() - a->base_played : 0;
    bool valid = heard > 0;  // Not before sound is really coming out: the picture starts with it.
    if (valid) {
        bool stalled = heard >= a->queued_frames;
        if (heard > a->queued_frames)
            heard = a->queued_frames;  // Not past what was queued.
        // In the file's time: at another speed, a second heard is more or less of it.
        int64_t raw = a->base_pts_ns + av_rescale((int64_t)heard * a->speed, 1000000, g_out_rate);
        int64_t step = (int64_t)BUFFER_FRAMES * 1000000 * a->speed / g_out_rate;
        int64_t now = playbackScaled((int64_t)armTicksToNs(armGetSystemTick()), a->speed);
        *pts_ns = smoothClockRead(&a->smooth, raw, now, step, !a->paused && !stalled);
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
    a->seek_pending = true;
    a->seek_ns = pts_ns;
    a->clock_valid = false;
    a->primed = false;
    mutexUnlock(&a->lock);
}

void audioSwitchTrack(Audio *a, int track, int64_t from_ns)
{
    mutexLock(&a->lock);
    a->track_wanted = track;
    a->track_from_ns = from_ns;
    a->seek_pending = true;
    a->clock_valid = false;
    a->primed = false;
    mutexUnlock(&a->lock);
}

bool audioTakeSwitchError(Audio *a, char *out, size_t size)
{
    mutexLock(&a->lock);
    bool failed = a->switch_error[0] != '\0';
    if (failed) {
        snprintf(out, size, "%s", a->switch_error);
        a->switch_error[0] = '\0';
    }
    mutexUnlock(&a->lock);
    return failed;
}

void audioSetSpeed(Audio *a, int permille)
{
    mutexLock(&a->lock);
    a->speed_wanted = permille;
    mutexUnlock(&a->lock);
}

int audioSpeed(Audio *a)
{
    mutexLock(&a->lock);
    int speed = a->speed;
    mutexUnlock(&a->lock);
    return speed;
}

bool audioReadFailed(Audio *a)
{
    mutexLock(&a->lock);
    bool failed = a->read_failed;
    mutexUnlock(&a->lock);
    return failed;
}

bool audioFinished(Audio *a)
{
    mutexLock(&a->lock);
    bool finished = a->finished;
    mutexUnlock(&a->lock);
    return finished;
}

int audioTrack(Audio *a)
{
    mutexLock(&a->lock);
    int track = a->track;
    mutexUnlock(&a->lock);
    return track;
}

bool audioReadyAt(Audio *a, int64_t *pts_ns)
{
    mutexLock(&a->lock);
    bool ready = a->primed && !a->seek_requested && a->track_wanted == NO_TRACK;
    *pts_ns = a->primed_pts_ns;
    mutexUnlock(&a->lock);
    return ready;
}
