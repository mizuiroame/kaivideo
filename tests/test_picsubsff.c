// Picture subtitles through FFmpeg: a DVD subtitle made here by FFmpeg's own
// encoder, put in a .sub's packets with its .idx, then read back by KaiVideo
// as the player would (vobsubDecode) and drawn.
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>

#include "check.h"
#include "picsubsff.h"

#define S_NS 1000000000ll
#define BOX_W 64
#define BOX_H 16

// An MPEG-2 pack header, then a private stream 1 packet of stream 0 with data.
static size_t packet(uint8_t *out, const uint8_t *data, size_t n)
{
    static const uint8_t PACK[14] = {0, 0, 1, 0xBA, 0x44, 0, 4, 0, 4, 1, 1, 0x89, 0xC3, 0xF8};
    memcpy(out, PACK, sizeof(PACK));
    size_t length = 3 + 5 + 1 + n;
    uint8_t head[] = {0, 0, 1, 0xBD, (uint8_t)(length >> 8), (uint8_t)length, 0x81, 0x80, 5, 0x21, 0, 1, 0, 1, 0x20};
    memcpy(out + sizeof(PACK), head, sizeof(head));
    memcpy(out + sizeof(PACK) + sizeof(head), data, n);
    return sizeof(PACK) + sizeof(head) + n;
}

// A white box with a black frame at (100, 400) on a 720 x 480 DVD frame, shown for 2 s, made a DVD
// subtitle by FFmpeg. Its length (0 if the encoder isn't there), and the encoder's .idx header.
static int encodeBox(uint8_t *out, int size, char *header, size_t header_size)
{
    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_DVD_SUBTITLE);
    AVCodecContext *ctx = codec ? avcodec_alloc_context3(codec) : NULL;
    if (!ctx)
        return 0;
    ctx->width = 720;
    ctx->height = 480;
    ctx->time_base = (AVRational){1, 1000};
    if (avcodec_open2(ctx, codec, NULL) < 0) {
        avcodec_free_context(&ctx);
        return 0;
    }
    snprintf(header, header_size, "%.*s", ctx->extradata_size, (const char *)ctx->extradata);
    static uint8_t indices[BOX_W * BOX_H];
    for (int y = 0; y < BOX_H; y++)
        for (int x = 0; x < BOX_W; x++)
            indices[y * BOX_W + x] = (x < 2 || y < 2 || x >= BOX_W - 2 || y >= BOX_H - 2) ? 2 : 1;
    static uint32_t palette[4] = {0x00000000, 0xFFFFFFFF, 0xFF000000, 0x00000000};
    AVSubtitleRect rect = {.x = 100, .y = 400, .w = BOX_W, .h = BOX_H, .nb_colors = 4, .type = SUBTITLE_BITMAP};
    rect.data[0] = indices;
    rect.linesize[0] = BOX_W;
    rect.data[1] = (uint8_t *)palette;
    AVSubtitleRect *rects[1] = {&rect};
    AVSubtitle sub = {.format = 0, .start_display_time = 0, .end_display_time = 2000, .num_rects = 1, .rects = rects};
    int n = avcodec_encode_subtitle(ctx, out, size, &sub);
    avcodec_free_context(&ctx);
    return n > 0 ? n : 0;
}

static void testVobsubFile(void)
{
    uint8_t spu[8192], sub[16384];
    char header[2048], idx[4096];
    int n = encodeBox(spu, sizeof(spu), header, sizeof(header));
    CHECK(n > 0);
    if (n <= 0)
        return;
    size_t size = packet(sub, spu, (size_t)n);
    snprintf(idx, sizeof(idx), "%s\nid: en, index: 0\ntimestamp: 00:00:03:000, filepos: 000000000\n", header);
    VobsubIndex x;
    CHECK(vobsubParseIndex(idx, strlen(idx), &x) && x.count == 1);
    PicSubs *s = vobsubDecode(&x, 0, sub, size, 0, 0, NULL);
    CHECK(s && picSubsCount(s) == 1);
    AssArea area = {720, 480, 1.0, 0.0};
    AssPicture out;
    CHECK(picSubsRender(s, 2 * S_NS, &area, &out) && out.w == 0);  // Before its time.
    CHECK(picSubsRender(s, 3 * S_NS + S_NS / 2, &area, &out) && out.w > 0);
    // The white middle, where it was put on the frame.
    int cx = 100 + BOX_W / 2 - out.x, cy = 400 + BOX_H / 2 - out.y;
    CHECK(cx >= 0 && cy >= 0 && cx < out.w && cy < out.h);
    if (cx >= 0 && cy >= 0 && cx < out.w && cy < out.h)
        CHECK(out.pixels[cy * out.w + cx] == 0xFFFFFFFF);
    CHECK(picSubsRender(s, 5 * S_NS + S_NS / 2, &area, &out) && out.w == 0);  // Its 2 s are over.
    CHECK(!vobsubDecode(&x, 0, sub, 10, 0, 0, NULL));  // A .sub cut short: nothing.
    CHECK(!vobsubDecode(&x, 3, sub, size, 0, 0, NULL));
    picSubsFree(s);
    vobsubIndexFree(&x);
}

// A decoded subtitle into a track: its times, and PGS's empty one that ends the one before.
static void testDecoded(void)
{
    PicSubs *s = picSubsCreate();
    uint8_t indices[4] = {1, 1, 1, 1};
    uint32_t palette[2] = {0, 0xFF00FF00};
    AVSubtitleRect rect = {.x = 10, .y = 10, .w = 2, .h = 2, .nb_colors = 2, .type = SUBTITLE_BITMAP};
    rect.data[0] = indices;
    rect.linesize[0] = 2;
    rect.data[1] = (uint8_t *)palette;
    AVSubtitleRect *rects[1] = {&rect};
    AVSubtitle shown = {.start_display_time = 0, .end_display_time = UINT32_MAX, .num_rects = 1, .rects = rects};
    AVSubtitle ends = {.start_display_time = 0, .end_display_time = 0, .num_rects = 0};
    picSubsAddDecoded(s, &shown, 10 * S_NS, 1920, 1080);
    picSubsAddDecoded(s, &ends, 14 * S_NS, 1920, 1080);
    AVSubtitleRect text = {.type = SUBTITLE_TEXT, .text = "hello"};
    AVSubtitleRect *texts[1] = {&text};
    AVSubtitle words = {.num_rects = 1, .rects = texts};
    picSubsAddDecoded(s, &words, 20 * S_NS, 1920, 1080);  // Text: left out.
    CHECK(picSubsCount(s) == 2);
    AssArea area = {1920, 1080, 1.0, 0.0};
    AssPicture out;
    CHECK(picSubsRender(s, 12 * S_NS, &area, &out) && out.w == 2 && out.pixels[0] == 0xFF00FF00);
    CHECK(picSubsRender(s, 14 * S_NS, &area, &out) && out.w == 0);
    int w, h;
    picSubsCanvas(NULL, 0, 0, &w, &h);
    CHECK(w == 720 && h == 480);
    picSubsCanvas(NULL, 1280, 720, &w, &h);
    CHECK(w == 1280 && h == 720);
    picSubsFree(s);
}

void testPicSubsFf(void)
{
    testDecoded();
    testVobsubFile();
}
