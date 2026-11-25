#include "picsubsff.h"

#include <stdlib.h>
#include <string.h>

#define MS_NS 1000000ll
#define DVD_W 720  // A VobSub's canvas when nothing says otherwise.
#define DVD_H 480

void picSubsAddDecoded(PicSubs *s, const AVSubtitle *sub, int64_t base_ns, int canvas_w, int canvas_h)
{
    PicRect rects[PICSUBS_RECTS_MAX];
    int count = 0;
    for (unsigned i = 0; i < sub->num_rects && count < PICSUBS_RECTS_MAX; i++) {
        const AVSubtitleRect *r = sub->rects[i];
        if (r->type != SUBTITLE_BITMAP || !r->data[0] || !r->data[1] || r->w <= 0 || r->h <= 0)
            continue;
        rects[count++] = (PicRect){r->x, r->y, r->w, r->h, r->data[0], r->linesize[0], (const uint32_t *)r->data[1],
                                   r->nb_colors > 0 && r->nb_colors <= 256 ? r->nb_colors : 256};
    }
    if (count == 0 && sub->num_rects > 0)
        return;  // Text only: not a picture subtitle's.
    int64_t start = base_ns + (int64_t)sub->start_display_time * MS_NS;
    int64_t end = sub->end_display_time > sub->start_display_time && sub->end_display_time != UINT32_MAX
                      ? base_ns + (int64_t)sub->end_display_time * MS_NS
                      : -1;  // Until the next one.
    picSubsAdd(s, start, end, canvas_w, canvas_h, rects, count);
}

void picSubsCanvas(const AVCodecContext *ctx, int video_w, int video_h, int *w, int *h)
{
    bool own = ctx && ctx->width > 0 && ctx->height > 0;
    *w = own ? ctx->width : video_w > 0 ? video_w : DVD_W;
    *h = own ? ctx->height : video_h > 0 ? video_h : DVD_H;
}

// The DVD subtitle decoder, with the index's size and palette.
static AVCodecContext *openDvdDecoder(const VobsubIndex *x)
{
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_DVD_SUBTITLE);
    AVCodecContext *ctx = codec ? avcodec_alloc_context3(codec) : NULL;
    if (!ctx)
        return NULL;
    ctx->extradata = av_mallocz(x->header_size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (ctx->extradata) {
        memcpy(ctx->extradata, x->header, x->header_size);
        ctx->extradata_size = (int)x->header_size;
    }
    if (!ctx->extradata || avcodec_open2(ctx, codec, NULL) < 0)
        avcodec_free_context(&ctx);
    return ctx;
}

PicSubs *vobsubDecode(const VobsubIndex *x, int language, const uint8_t *sub, size_t size, int video_w, int video_h,
                      const volatile bool *cancel)
{
    if (language < 0 || language >= x->count)
        return NULL;
    const VobsubLanguage *l = &x->languages[language];
    AVCodecContext *ctx = openDvdDecoder(x);
    PicSubs *s = ctx ? picSubsCreate() : NULL;
    uint8_t *bytes = s ? av_malloc(VOBSUB_PICTURE_MAX + AV_INPUT_BUFFER_PADDING_SIZE) : NULL;
    AVPacket *pkt = bytes ? av_packet_alloc() : NULL;
    int canvas_w, canvas_h;
    picSubsCanvas(ctx, video_w, video_h, &canvas_w, &canvas_h);
    for (int i = 0; pkt && i < l->count && !(cancel && *cancel); i++) {
        size_t n = vobsubPicture(sub, size, l->entries[i].filepos, l->stream, bytes, VOBSUB_PICTURE_MAX);
        if (n == 0)
            continue;
        memset(bytes + n, 0, AV_INPUT_BUFFER_PADDING_SIZE);
        pkt->data = bytes;
        pkt->size = (int)n;
        AVSubtitle decoded;
        int got = 0;
        if (avcodec_decode_subtitle2(ctx, &decoded, &got, pkt) >= 0 && got) {
            picSubsAddDecoded(s, &decoded, l->entries[i].start_ns, canvas_w, canvas_h);
            avsubtitle_free(&decoded);
        }
    }
    if (pkt) {
        pkt->data = NULL;
        pkt->size = 0;
    }
    av_packet_free(&pkt);
    av_free(bytes);
    avcodec_free_context(&ctx);
    if (s && picSubsCount(s) == 0) {
        picSubsFree(s);
        s = NULL;
    }
    return s;
}
