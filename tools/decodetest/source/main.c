// KaiVideo decode test: for each video in sdmc:/kaivideo-test, reads it with
// FFmpeg, hands its video to libounce's hardware VideoDecoder, and reports
// whether the decoder opens, how many frames come out and how fast, and the
// first frame's size and brightness (a gray mid value means a real picture).
// Nothing is drawn. Results also go to sdmc:/kaivideo-decodetest.txt.
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
#include <switch.h>

#define FOLDER "sdmc:/kaivideo-test"
#define LOG_PATH "sdmc:/kaivideo-decodetest.txt"
#define IO_BUFFER (64 * 1024)
#define MAX_FRAMES 600           // 10 seconds at 60.
#define SUBMIT_TIMEOUT_NS 2000000ull
#define SUBMIT_TRIES 2000        // About 4 seconds of retries.
#define DRAIN_TIMEOUT_NS 500000000ull

static FILE *g_log;

static void say(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    if (g_log) {
        va_start(args, fmt);
        vfprintf(g_log, fmt, args);
        va_end(args);
        fflush(g_log);
    }
    consoleUpdate(NULL);
}

// FFmpeg reads the file through these (it has no file protocol here).
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

typedef struct {
    VideoDecoder decoder;
    int frames;
    bool first_seen;
    double first_luma;
    u32 width, height;
} Run;

// Takes every frame that's ready (waiting up to timeout_ns for the first).
static void drain(Run *run, u64 timeout_ns)
{
    VideoFrame frame;
    while (R_SUCCEEDED(videoDecoderGetFrame(&run->decoder, &frame, timeout_ns))) {
        timeout_ns = 0;
        if (!run->first_seen) {
            run->first_seen = true;
            run->width = frame.width;
            run->height = frame.height;
            size_t w = frame.width, h = frame.height;
            u8 *y = malloc(w * h), *uv = malloc(w * h / 2);
            if (y && uv && R_SUCCEEDED(videoDecoderCopyFrame(&run->decoder, &frame, y, w, uv, w))) {
                u64 sum = 0;
                for (size_t i = 0; i < w * h; i += 7)
                    sum += y[i];
                run->first_luma = (double)sum / (double)((w * h + 6) / 7);
            }
            free(y);
            free(uv);
        }
        run->frames++;
        videoDecoderReleaseFrame(&run->decoder, &frame);
    }
}

static bool submit(Run *run, const AVPacket *p, AVRational time_base, Result *last_rc)
{
    int64_t ts = p->pts != AV_NOPTS_VALUE ? p->pts : p->dts != AV_NOPTS_VALUE ? p->dts : 0;
    s64 ns = av_rescale_q(ts, time_base, (AVRational){1, 1000000000});
    for (int tries = 0; tries < SUBMIT_TRIES; tries++) {
        drain(run, 0);
        Result rc = videoDecoderSubmit(&run->decoder, p->data, p->size, ns, false, SUBMIT_TIMEOUT_NS);
        *last_rc = rc;
        if (R_SUCCEEDED(rc))
            return true;
        if (rc != KERNELRESULT(TimedOut) && rc != VIDDEC_RESULT_SUBMIT_BUSY)
            return false;
    }
    return false;
}

static void testFile(const char *path)
{
    say("\n== %s\n", path);
    FILE *f = fopen(path, "rb");
    if (!f) {
        say("  can't open the file\n");
        return;
    }
    AVFormatContext *fmt = avformat_alloc_context();
    uint8_t *buffer = av_malloc(IO_BUFFER);
    AVIOContext *io = buffer ? avio_alloc_context(buffer, IO_BUFFER, 0, f, readFile, NULL, seekFile) : NULL;
    AVBSFContext *bsf = NULL;
    AVPacket *pkt = av_packet_alloc();
    Run run;
    memset(&run, 0, sizeof(run));
    bool created = false;
    if (!fmt || !io || !pkt)
        goto done;
    fmt->pb = io;
    fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
    if (avformat_open_input(&fmt, NULL, NULL, NULL) < 0 || avformat_find_stream_info(fmt, NULL) < 0) {
        say("  FFmpeg can't read it\n");
        goto done;
    }
    int vs = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (vs < 0) {
        say("  no video stream\n");
        goto done;
    }
    AVStream *st = fmt->streams[vs];
    AVCodecParameters *par = st->codecpar;
    ViddecCodec codec;
    const char *filter = NULL;
    switch (par->codec_id) {
    case AV_CODEC_ID_H264: codec = ViddecCodec_H264; filter = "h264_mp4toannexb"; break;
    case AV_CODEC_ID_VP9: codec = ViddecCodec_Vp9; filter = "vp9_superframe_split"; break;
    case AV_CODEC_ID_AV1: codec = ViddecCodec_Av1; break;
    case AV_CODEC_ID_VP8: codec = ViddecCodec_Vp8; break;
    default: say("  codec %s: not one the hardware decodes\n", avcodec_get_name(par->codec_id)); goto done;
    }
    double fps = av_q2d(av_guess_frame_rate(fmt, st, NULL));
    say("  %s %dx%d, %.2f fps, container %s\n", avcodec_get_name(par->codec_id), par->width, par->height, fps,
        fmt->iformat->name);

    const AVBitStreamFilter *spec = av_bsf_get_by_name(filter ? filter : "null");
    if (!spec || av_bsf_alloc(spec, &bsf) < 0 || avcodec_parameters_copy(bsf->par_in, par) < 0) {
        say("  bitstream filter %s missing\n", filter ? filter : "null");
        goto done;
    }
    bsf->time_base_in = st->time_base;
    if (av_bsf_init(bsf) < 0) {
        say("  bitstream filter failed to start\n");
        goto done;
    }

    VideoDecoderConfig config = {
        .codec = codec,
        .width = (u32)par->width,
        .height = (u32)par->height,
        .frame_rate = fps >= 1 && fps <= VIDEO_DECODER_MAX_FRAME_RATE ? (u32)(fps + 0.5) : 0,
        .max_sample_size = 16 * 1024 * 1024,
        .codec_config = codec == ViddecCodec_H264 ? bsf->par_out->extradata : NULL,
        .codec_config_size = codec == ViddecCodec_H264 ? (size_t)bsf->par_out->extradata_size : 0,
    };
    u64 t0 = armGetSystemTick();
    Result rc = videoDecoderCreate(&run.decoder, &config);
    say("  videoDecoderCreate: 0x%x %s (%.0f ms)\n", rc, R_SUCCEEDED(rc) ? "OK" : "FAILED",
        armTicksToNs(armGetSystemTick() - t0) / 1e6);
    if (R_FAILED(rc)) {
        say("    module %u, description %u\n", R_MODULE(rc), R_DESCRIPTION(rc));
        goto done;
    }
    created = true;

    Result last = 0;
    bool ok = true;
    int samples = 0;
    t0 = armGetSystemTick();
    while (ok && run.frames < MAX_FRAMES && av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == vs && av_bsf_send_packet(bsf, pkt) >= 0) {
            while (ok && av_bsf_receive_packet(bsf, pkt) >= 0) {
                ok = submit(&run, pkt, st->time_base, &last);
                samples++;
                av_packet_unref(pkt);
            }
        }
        av_packet_unref(pkt);
    }
    drain(&run, DRAIN_TIMEOUT_NS);
    drain(&run, DRAIN_TIMEOUT_NS);
    double secs = armTicksToNs(armGetSystemTick() - t0) / 1e9;
    if (!ok)
        say("  videoDecoderSubmit stopped: 0x%x (module %u, description %u)\n", last, R_MODULE(last),
            R_DESCRIPTION(last));
    say("  %d samples in, %d frames out in %.2f s: %.1f frames/s\n", samples, run.frames, secs,
        secs > 0 ? run.frames / secs : 0.0);
    if (run.first_seen)
        say("  first frame %ux%u, average brightness %.0f of 255\n", run.width, run.height, run.first_luma);

done:
    if (created)
        videoDecoderClose(&run.decoder);
    av_bsf_free(&bsf);
    av_packet_free(&pkt);
    if (fmt)
        avformat_close_input(&fmt);
    if (io) {
        av_freep(&io->buffer);
        avio_context_free(&io);
    } else {
        av_free(buffer);
    }
    fclose(f);
}

static bool isVideo(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot && (!strcasecmp(dot, ".mp4") || !strcasecmp(dot, ".mkv") || !strcasecmp(dot, ".webm"));
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    g_log = fopen(LOG_PATH, "w");
    say("KaiVideo decode test: libounce's hardware VideoDecoder.\n");
    say("Videos from " FOLDER "; results also in " LOG_PATH ".\n");

    DIR *dir = opendir(FOLDER);
    if (!dir) {
        say("\nNo folder " FOLDER ".\n");
    } else {
        struct dirent *e;
        char path[512];
        while ((e = readdir(dir)) != NULL) {
            if (!isVideo(e->d_name))
                continue;
            snprintf(path, sizeof(path), "%s/%s", FOLDER, e->d_name);
            testFile(path);
        }
        closedir(dir);
    }
    say("\nDone. Press + to exit.\n");
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
            break;
        consoleUpdate(NULL);
        svcSleepThread(50000000ull);
    }
    if (g_log)
        fclose(g_log);
    consoleExit(NULL);
    return 0;
}
