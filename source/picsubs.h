// Picture subtitles (Blu-ray PGS, DVD VobSub): the pictures FFmpeg decodes,
// each a few rectangles of palette colors on the subtitle's canvas, kept
// run-length coded with their times, and drawn into one image scaled over the
// box the video is shown in. Pure logic.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "asssubs.h"

#define PICSUBS_RECTS_MAX 8                // Rectangles kept from one picture.
#define PICSUBS_BYTES_MAX (24u << 20)      // A track's kept pictures, at most: the oldest go first.
#define PICSUBS_OPEN_NS 60000000000ll      // A picture without an end shows at most this long.
#define PICSUBS_CANVAS_MAX 4096

// One rectangle as FFmpeg gives it (AVSubtitleRect): w x h palette indexes.
typedef struct {
    int x, y, w, h;              // On the canvas.
    const uint8_t *indices;
    int stride;                  // Bytes from one row of indices to the next.
    const uint32_t *palette;     // 0xAARRGGBB (straight alpha).
    int palette_count;           // 1 to 256.
} PicRect;

typedef struct PicSubs PicSubs;

PicSubs *picSubsCreate(void);
void picSubsFree(PicSubs *s);

// A picture shown from start_ns, its rectangles on a canvas_w x canvas_h canvas
// (the video's frame for VobSub, the stream's for PGS); until end_ns, or with
// end_ns < 0 until the next picture (at most PICSUBS_OPEN_NS). count 0: nothing
// shows from start_ns on (PGS ends a picture so). A picture at the same time
// as one kept is left out (a track is read again after a seek). False if it
// can't be kept (out of memory, or a canvas or rectangle that makes no sense).
bool picSubsAdd(PicSubs *s, int64_t start_ns, int64_t end_ns, int canvas_w, int canvas_h, const PicRect *rects,
                int count);
// How many pictures are kept (those that end one included), and their bytes.
int picSubsCount(const PicSubs *s);
size_t picSubsBytes(const PicSubs *s);

// The picture shown at pos_ns, drawn for area (the video's box, in pixels):
// out's pixels are the part it covers, 0xAABBGGRR with straight alpha, at
// out->x, out->y in the area, valid until the next call. out->w is 0 when
// nothing shows; out->id changes when the pixels do. Pictures in the lower
// half go up when area->lift_percent of the area's bottom is covered. False
// only if out of memory.
bool picSubsRender(PicSubs *s, int64_t pos_ns, const AssArea *area, AssPicture *out);
