// Makes picture subtitles to try KaiVideo's with, all made up here: two
// languages of numbered lines, drawn with the app's own font and made DVD
// subtitles by FFmpeg's encoder. Writes, next to a copy of a video of yours:
//   NAME.idx + NAME.sub  a VobSub pair next to the video (the card or a network folder)
//   NAME-vobsub.mkv      the same video with both languages inside (VobSub in MKV)
// Built and run on a PC with the host tests' image:
//   docker run --rm -v "$PWD:/w" -w /w kaivideo-host sh -c
//     "gcc -O1 -I third_party tools/picsubtest/make_picsubs.c -o /tmp/mk -lavformat -lavcodec -lavutil -lm &&
//      /tmp/mk testvideos/h264_720p30_high.mp4 data/font_bold.ttf testvideos/picsubs/clip"
// (FFmpeg has no PGS encoder, so there's no PGS file to make here.)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#define CANVAS_W 720
#define CANVAS_H 480
#define LINES 12
#define FIRST_S 2
#define EVERY_S 4
#define SHOWN_MS 3000
#define TEXT_PX 34
#define MAX_W 700
#define MAX_H 60

static const char *const LANGS[2] = {"en", "es"};

typedef struct {
    uint8_t *data;
    int size;
} Spu;

static Spu g_spus[2][LINES];
static char g_header[2048];

// Text as palette indexes: 1 white, 2 its black outline, 0 clear. Its box in *w, *h.
static uint8_t *drawText(const stbtt_fontinfo *font, const char *text, int *w, int *h)
{
    static uint8_t cover[MAX_W * MAX_H];
    memset(cover, 0, sizeof(cover));
    float scale = stbtt_ScaleForPixelHeight(font, TEXT_PX);
    int ascent;
    stbtt_GetFontVMetrics(font, &ascent, NULL, NULL);
    int x = 6, base = 6 + (int)(ascent * scale);
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        int c = *p, advance, lsb, x0, y0, x1, y1;
        if (c == 0xC3 && p[1]) {  // The two-byte letters the lines use (í).
            c = 0x00C0 | (p[1] & 0x3F);
            p++;
        }
        stbtt_GetCodepointHMetrics(font, c, &advance, &lsb);
        stbtt_GetCodepointBitmapBox(font, c, scale, scale, &x0, &y0, &x1, &y1);
        if (x + x1 < MAX_W - 6 && base + y1 < MAX_H)
            stbtt_MakeCodepointBitmap(font, cover + (base + y0) * MAX_W + x + x0, x1 - x0, y1 - y0, MAX_W, scale, scale,
                                      c);
        x += (int)(advance * scale);
    }
    *w = x + 6 < MAX_W ? x + 6 : MAX_W;
    *h = MAX_H;
    uint8_t *out = calloc((size_t)*w * *h, 1);
    for (int y = 0; y < *h; y++)
        for (int i = 0; i < *w; i++) {
            if (cover[y * MAX_W + i] > 110) {
                out[y * *w + i] = 1;
                continue;
            }
            for (int dy = -2; dy <= 2 && !out[y * *w + i]; dy++)  // The outline: near a letter.
                for (int dx = -2; dx <= 2; dx++) {
                    int yy = y + dy, xx = i + dx;
                    if (yy >= 0 && yy < *h && xx >= 0 && xx < *w && cover[yy * MAX_W + xx] > 110) {
                        out[y * *w + i] = 2;
                        break;
                    }
                }
        }
    return out;
}

static int encodeLines(const stbtt_fontinfo *font)
{
    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_DVD_SUBTITLE);
    AVCodecContext *ctx = codec ? avcodec_alloc_context3(codec) : NULL;
    if (!ctx)
        return 0;
    ctx->width = CANVAS_W;
    ctx->height = CANVAS_H;
    ctx->time_base = (AVRational){1, 1000};
    if (avcodec_open2(ctx, codec, NULL) < 0)
        return 0;
    snprintf(g_header, sizeof(g_header), "%.*s", ctx->extradata_size, (const char *)ctx->extradata);
    static uint32_t palette[4] = {0x00000000, 0xFFFFFFFF, 0xFF000000, 0x00000000};
    for (int l = 0; l < 2; l++)
        for (int i = 0; i < LINES; i++) {
            char text[96];
            snprintf(text, sizeof(text), l == 0 ? "Picture subtitle %d of %d" : "Subt\xC3\xADtulo de imagen %d de %d", i + 1,
                     LINES);
            int w, h;
            uint8_t *indices = drawText(font, text, &w, &h);
            AVSubtitleRect rect = {.x = (CANVAS_W - w) / 2, .y = CANVAS_H - h - 24, .w = w, .h = h,
                                   .nb_colors = 4, .type = SUBTITLE_BITMAP};
            rect.data[0] = indices;
            rect.linesize[0] = w;
            rect.data[1] = (uint8_t *)palette;
            AVSubtitleRect *rects[1] = {&rect};
            AVSubtitle sub = {.end_display_time = SHOWN_MS, .num_rects = 1, .rects = rects};
            uint8_t buf[65536];
            int n = avcodec_encode_subtitle(ctx, buf, sizeof(buf), &sub);
            free(indices);
            if (n <= 0)
                return 0;
            g_spus[l][i].data = malloc((size_t)n);
            memcpy(g_spus[l][i].data, buf, (size_t)n);
            g_spus[l][i].size = n;
        }
    avcodec_free_context(&ctx);
    return 1;
}

// An MPEG-2 pack header, then private stream 1 packets of stream with the picture.
static void writePacket(FILE *f, int stream, const uint8_t *data, int n)
{
    static const uint8_t PACK[14] = {0, 0, 1, 0xBA, 0x44, 0, 4, 0, 4, 1, 1, 0x89, 0xC3, 0xF8};
    for (int at = 0; at < n;) {
        int part = n - at < 2000 ? n - at : 2000;
        int length = 3 + 5 + 1 + part;
        uint8_t head[] = {0, 0, 1, 0xBD, (uint8_t)(length >> 8), (uint8_t)length, 0x81, 0x80, 5, 0x21, 0, 1, 0, 1,
                          (uint8_t)(0x20 + stream)};
        fwrite(PACK, 1, sizeof(PACK), f);
        fwrite(head, 1, sizeof(head), f);
        fwrite(data + at, 1, (size_t)part, f);
        at += part;
    }
}

static int writeVobsub(const char *stem)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s.sub", stem);
    FILE *sub = fopen(path, "wb");
    snprintf(path, sizeof(path), "%s.idx", stem);
    FILE *idx = fopen(path, "wb");
    if (!sub || !idx)
        return 0;
    fprintf(idx, "%s\n", g_header);
    long positions[2][LINES];
    for (int i = 0; i < LINES; i++)  // Interleaved by time, as DVDs have them.
        for (int l = 0; l < 2; l++) {
            positions[l][i] = ftell(sub);
            writePacket(sub, l, g_spus[l][i].data, g_spus[l][i].size);
        }
    for (int l = 0; l < 2; l++) {
        fprintf(idx, "\nid: %s, index: %d\n", LANGS[l], l);
        for (int i = 0; i < LINES; i++) {
            int s = FIRST_S + i * EVERY_S;
            fprintf(idx, "timestamp: 00:%02d:%02d:000, filepos: %09lx\n", s / 60, s % 60, positions[l][i]);
        }
    }
    fclose(sub);
    fclose(idx);
    return 1;
}

// The video again, with both languages as VobSub tracks inside.
static int writeMkv(const char *video, const char *stem)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s-vobsub.mkv", stem);
    AVFormatContext *in = NULL, *out = NULL;
    if (avformat_open_input(&in, video, NULL, NULL) < 0 || avformat_find_stream_info(in, NULL) < 0 ||
        avformat_alloc_output_context2(&out, NULL, "matroska", path) < 0)
        return 0;
    int map[16];
    for (unsigned i = 0; i < in->nb_streams && i < 16; i++) {
        AVStream *s = avformat_new_stream(out, NULL);
        avcodec_parameters_copy(s->codecpar, in->streams[i]->codecpar);
        s->codecpar->codec_tag = 0;
        s->time_base = in->streams[i]->time_base;
        map[i] = s->index;
    }
    int subs[2];
    for (int l = 0; l < 2; l++) {
        AVStream *s = avformat_new_stream(out, NULL);
        s->codecpar->codec_type = AVMEDIA_TYPE_SUBTITLE;
        s->codecpar->codec_id = AV_CODEC_ID_DVD_SUBTITLE;
        s->codecpar->width = CANVAS_W;
        s->codecpar->height = CANVAS_H;
        size_t n = strlen(g_header);
        s->codecpar->extradata = av_mallocz(n + AV_INPUT_BUFFER_PADDING_SIZE);
        memcpy(s->codecpar->extradata, g_header, n);
        s->codecpar->extradata_size = (int)n;
        s->time_base = (AVRational){1, 1000};
        av_dict_set(&s->metadata, "language", l == 0 ? "eng" : "spa", 0);
        subs[l] = s->index;
    }
    if (avio_open(&out->pb, path, AVIO_FLAG_WRITE) < 0 || avformat_write_header(out, NULL) < 0)
        return 0;
    AVPacket *pkt = av_packet_alloc();
    int next = 0;  // The next line to put in, by time.
    while (av_read_frame(in, pkt) >= 0) {
        int64_t ms = av_rescale_q(pkt->pts != AV_NOPTS_VALUE ? pkt->pts : pkt->dts, in->streams[pkt->stream_index]->time_base,
                                  (AVRational){1, 1000});
        while (next < LINES && (FIRST_S + next * EVERY_S) * 1000 <= ms) {
            for (int l = 0; l < 2; l++) {
                AVPacket *s = av_packet_alloc();
                av_new_packet(s, g_spus[l][next].size);
                memcpy(s->data, g_spus[l][next].data, (size_t)g_spus[l][next].size);
                s->stream_index = subs[l];
                s->pts = s->dts = (FIRST_S + next * EVERY_S) * 1000;
                s->duration = SHOWN_MS;
                av_interleaved_write_frame(out, s);
                av_packet_free(&s);
            }
            next++;
        }
        if (pkt->stream_index >= 16) {
            av_packet_unref(pkt);
            continue;
        }
        AVStream *o = out->streams[map[pkt->stream_index]];
        av_packet_rescale_ts(pkt, in->streams[pkt->stream_index]->time_base, o->time_base);
        pkt->stream_index = map[pkt->stream_index];
        pkt->pos = -1;
        av_interleaved_write_frame(out, pkt);
    }
    av_write_trailer(out);
    av_packet_free(&pkt);
    avio_closep(&out->pb);
    avformat_free_context(out);
    avformat_close_input(&in);
    return 1;
}

static int copyFile(const char *from, const char *to)
{
    FILE *a = fopen(from, "rb"), *b = fopen(to, "wb");
    if (!a || !b)
        return 0;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), a)) > 0)
        fwrite(buf, 1, n, b);
    fclose(a);
    return fclose(b) == 0;
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "make_picsubs VIDEO FONT.ttf OUT_STEM\n");
        return 2;
    }
    FILE *f = fopen(argv[2], "rb");
    static unsigned char ttf[8 << 20];
    size_t n = f ? fread(ttf, 1, sizeof(ttf), f) : 0;
    stbtt_fontinfo font;
    if (!n || !stbtt_InitFont(&font, ttf, 0) || !encodeLines(&font)) {
        fprintf(stderr, "can't make the pictures\n");
        return 1;
    }
    const char *ext = strrchr(argv[1], '.');
    char copy[1024];
    snprintf(copy, sizeof(copy), "%s%s", argv[3], ext ? ext : ".mp4");
    if (!writeVobsub(argv[3]) || !copyFile(argv[1], copy) || !writeMkv(argv[1], argv[3])) {
        fprintf(stderr, "can't write the files\n");
        return 1;
    }
    printf("made %s.idx, %s.sub, %s and %s-vobsub.mkv\n", argv[3], argv[3], copy, argv[3]);
    return 0;
}
