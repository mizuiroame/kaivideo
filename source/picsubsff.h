// Picture subtitles (picsubs.h) from what FFmpeg decodes: a PGS or VobSub
// track inside a video (video.c), or the pictures of a VobSub .idx/.sub pair
// next to it (player.c), decoded with FFmpeg's DVD subtitle decoder.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <libavcodec/avcodec.h>

#include "picsubs.h"
#include "vobsub.h"

// A decoded subtitle's pictures, from base_ns (its packet's time) on, on a canvas_w x canvas_h canvas.
// One without pictures ends the one before (PGS). Its text, if any, is left out.
void picSubsAddDecoded(PicSubs *s, const AVSubtitle *sub, int64_t base_ns, int canvas_w, int canvas_h);

// The canvas a decoder's pictures are on: its own size once it knows one, else the video's.
void picSubsCanvas(const AVCodecContext *ctx, int video_w, int video_h, int *w, int *h);

// One of a VobSub index's languages, its pictures taken out of the .sub's bytes and decoded. NULL if
// none could be. cancel stops it (may be NULL).
PicSubs *vobsubDecode(const VobsubIndex *x, int language, const uint8_t *sub, size_t size, int video_w, int video_h,
                      const volatile bool *cancel);
