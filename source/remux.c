#include "remux.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>

#define PROGRESS_EVERY 500  // Packets between reports.
#define STEP_EVERY 20000    // Packets between steps in the log.
#define OWN_STACK (2 * 1024 * 1024)

static RemuxLog g_log;

void remuxSetLog(RemuxLog log)
{
    g_log = log;
}

#define STEP(...)               \
    do {                        \
        if (g_log)              \
            g_log(__VA_ARGS__); \
    } while (0)

typedef struct {
    AVFormatContext *in, *out;
    int *map;          // Each input stream's output stream, -1 if it's left out.
    int64_t *last_dts; // The last time written on each output stream (its time base), INT64_MIN before any.
    int64_t shift;     // Taken from every time (AV_TIME_BASE): the picture's start becomes 0.
} Remux;

// Where the picture starts (the first video stream), else the file.
static int64_t pictureStart(const AVFormatContext *fmt)
{
    for (unsigned i = 0; i < fmt->nb_streams; i++) {
        const AVStream *st = fmt->streams[i];
        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && st->start_time != AV_NOPTS_VALUE)
            return av_rescale_q(st->start_time, st->time_base, AV_TIME_BASE_Q);
    }
    return fmt->start_time != AV_NOPTS_VALUE ? fmt->start_time : 0;
}

static bool keeps(const AVCodecParameters *par)
{
    return par->codec_type == AVMEDIA_TYPE_VIDEO || par->codec_type == AVMEDIA_TYPE_AUDIO ||
           par->codec_type == AVMEDIA_TYPE_SUBTITLE;
}

// The output's tracks, one for each video, sound and subtitle track of the input.
static bool addStreams(Remux *r)
{
    r->map = malloc(r->in->nb_streams * sizeof(int));
    r->last_dts = malloc(r->in->nb_streams * sizeof(int64_t));
    if (!r->map || !r->last_dts)
        return false;
    int next = 0;
    for (unsigned i = 0; i < r->in->nb_streams; i++) {
        const AVStream *from = r->in->streams[i];
        r->map[i] = -1;
        r->last_dts[i] = INT64_MIN;
        if (!keeps(from->codecpar))
            continue;
        STEP("  remux: track %u, %s %s", i, av_get_media_type_string(from->codecpar->codec_type),
             avcodec_get_name(from->codecpar->codec_id));
        AVStream *to = avformat_new_stream(r->out, NULL);
        if (!to || avcodec_parameters_copy(to->codecpar, from->codecpar) < 0)
            return false;
        to->codecpar->codec_tag = 0;
        to->time_base = from->time_base;
        av_dict_copy(&to->metadata, from->metadata, 0);
        to->disposition = from->disposition;
        r->map[i] = next++;
    }
    return next > 0;
}

// Writes one packet, moved to the picture's start. A packet before it, or
// one no later than the last on its track (a resumed conversion's overlap),
// is left out. False if writing failed.
static bool writePacket(Remux *r, AVPacket *pkt)
{
    int to = pkt->stream_index < (int)r->in->nb_streams ? r->map[pkt->stream_index] : -1;
    if (to < 0)
        return true;
    AVRational from_tb = r->in->streams[pkt->stream_index]->time_base;
    AVRational to_tb = r->out->streams[to]->time_base;
    int64_t shift = av_rescale_q(r->shift, AV_TIME_BASE_Q, from_tb);
    if (pkt->pts != AV_NOPTS_VALUE)
        pkt->pts -= shift;
    if (pkt->dts != AV_NOPTS_VALUE)
        pkt->dts -= shift;
    av_packet_rescale_ts(pkt, from_tb, to_tb);
    int64_t when = pkt->dts != AV_NOPTS_VALUE ? pkt->dts : pkt->pts;
    if (when == AV_NOPTS_VALUE || when < 0 || when <= r->last_dts[pkt->stream_index])
        return true;
    r->last_dts[pkt->stream_index] = when;
    pkt->stream_index = to;
    pkt->pos = -1;
    return av_interleaved_write_frame(r->out, pkt) >= 0;
}

static void finish(Remux *r)
{
    if (r->out) {
        if (r->out->pb)
            avio_closep(&r->out->pb);
        avformat_free_context(r->out);
    }
    if (r->in)
        avformat_close_input(&r->in);
    free(r->map);
    free(r->last_dts);
}

// Copies every packet. False if it stopped (cancel) or failed.
static bool copyPackets(Remux *r, const volatile bool *cancel, RemuxProgress progress, void *user, char *error,
                        size_t error_size)
{
    AVPacket *pkt = av_packet_alloc();
    int64_t size = r->in->pb ? avio_size(r->in->pb) : -1;
    bool ok = pkt != NULL;
    for (long n = 0; ok; n++) {
        if (cancel && *cancel) {
            ok = false;
            break;
        }
        if (n == 0 || n % STEP_EVERY == 0)
            STEP("  remux: packet %ld", n);
        int got = av_read_frame(r->in, pkt);
        if (got == AVERROR_EOF)
            break;
        if (got < 0) {
            snprintf(error, error_size, "The downloaded video can't be read to the end.");
            ok = false;
            break;
        }
        ok = writePacket(r, pkt);
        if (n == 0)
            STEP("  remux: the first packet written (%d)", ok);
        av_packet_unref(pkt);
        if (!ok)
            snprintf(error, error_size, "It couldn't be written to the card.");
        if (progress && size > 0 && n % PROGRESS_EVERY == 0)
            progress(user, (double)avio_tell(r->in->pb) / (double)size);
    }
    av_packet_free(&pkt);
    return ok;
}

// FFmpeg takes what comes before a ':' for a protocol ("sdmc:/..." isn't
// one): a path goes to it as "file:<path>".
#define PATH_URL_MAX 1100

bool remuxToMkv(const char *in_path, const char *out_path, const volatile bool *cancel, RemuxProgress progress,
                void *user, char *error, size_t error_size)
{
    Remux r = {0};
    error[0] = '\0';
    char in_url[PATH_URL_MAX], out_url[PATH_URL_MAX];
    snprintf(in_url, sizeof(in_url), "file:%s", in_path);
    snprintf(out_url, sizeof(out_url), "file:%s", out_path);
    STEP("  remux: opening %s", in_url);
    int opened = avformat_open_input(&r.in, in_url, NULL, NULL);
    STEP("  remux: opened (%d), reading its tracks", opened);
    bool ok = opened == 0 && avformat_find_stream_info(r.in, NULL) >= 0;
    STEP("  remux: tracks read (%d), making the MKV", ok);
    ok = ok && avformat_alloc_output_context2(&r.out, NULL, "matroska", out_url) >= 0 && r.out;
    // Fixed track and segment ids: otherwise the MKV writer asks for random
    // numbers, which closes KaiVideo on the console (no /dev/urandom, no clock()).
    if (ok)
        r.out->flags |= AVFMT_FLAG_BITEXACT;
    if (!ok)
        snprintf(error, error_size, "The downloaded video can't be read.");
    if (ok) {
        r.shift = pictureStart(r.in);
        ok = addStreams(&r);
        STEP("  remux: tracks added (%d), opening %s", ok, out_url);
        ok = ok && avio_open(&r.out->pb, out_url, AVIO_FLAG_WRITE) >= 0;
        STEP("  remux: opened (%d), writing its header", ok);
        ok = ok && avformat_write_header(r.out, NULL) >= 0;
        STEP("  remux: header written (%d)", ok);
        if (!ok)
            snprintf(error, error_size, "It couldn't be written to the card.");
    }
    if (ok)
        ok = copyPackets(&r, cancel, progress, user, error, error_size);
    STEP("  remux: packets copied (%d), writing the index", ok);
    if (ok && av_write_trailer(r.out) < 0) {  // The index goes in here.
        snprintf(error, error_size, "It couldn't be written to the card.");
        ok = false;
    }
    finish(&r);
    STEP("  remux: done (%d) %s", ok, error);
    if (!ok)
        remove(out_path);
    return ok;
}

typedef struct {
    const char *in, *out;
    const volatile bool *cancel;
    char *error;
    size_t error_size;
    bool ok;
} Job;

static void *runJob(void *arg)
{
    Job *job = arg;
    job->ok = remuxToMkv(job->in, job->out, job->cancel, NULL, NULL, job->error, job->error_size);
    return NULL;
}

bool remuxToMkvOnOwnThread(const char *in_path, const char *out_path, const volatile bool *cancel, char *error,
                           size_t error_size)
{
    Job job = {in_path, out_path, cancel, error, error_size, false};
    pthread_attr_t attr;
    pthread_t thread;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, OWN_STACK);
    bool started = pthread_create(&thread, &attr, runJob, &job) == 0;
    pthread_attr_destroy(&attr);
    STEP("  remux: its thread %s", started ? "started" : "couldn't start");
    if (!started) {
        snprintf(error, error_size, "Its thread couldn't start.");
        return false;
    }
    pthread_join(thread, NULL);
    return job.ok;
}
