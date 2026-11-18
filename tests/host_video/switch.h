// What video.c needs from libnx and libounce, for running it on a PC. The
// hardware decoder always refuses, so videos go down the software path.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

typedef uint32_t u32;
typedef uint64_t u64;
typedef int64_t s64;
typedef uint8_t u8;
typedef u32 Result;

#define R_SUCCEEDED(rc) ((rc) == 0)
#define R_FAILED(rc) ((rc) != 0)
#define KERNELRESULT(x) (0xea01u + 0 * (x))
#define TimedOut 0
#define VIDDEC_RESULT_SUBMIT_BUSY 0xbeefu
#define VIDEO_DECODER_MAX_FRAME_RATE 60
#define CUR_PROCESS_HANDLE 0
#define InfoType_CoreMask 0

typedef enum { ViddecCodec_H264, ViddecCodec_Vp9, ViddecCodec_Av1 = 3, ViddecCodec_Vp8 } ViddecCodec;
typedef struct { int unused; } VideoDecoder;
typedef struct { s64 timestamp_ns; } VideoFrame;
typedef struct {
    ViddecCodec codec;
    u32 width, height, frame_rate;
    size_t max_sample_size;
    const void *codec_config;
    size_t codec_config_size;
} VideoDecoderConfig;

static inline Result videoDecoderCreate(VideoDecoder *d, const VideoDecoderConfig *c) { (void)d; (void)c; return 1; }
static inline void videoDecoderClose(VideoDecoder *d) { (void)d; abort(); }
static inline void videoDecoderFlush(VideoDecoder *d) { (void)d; abort(); }
static inline Result videoDecoderGetFrame(VideoDecoder *d, VideoFrame *f, u64 t) { (void)d; (void)f; (void)t; abort(); }
static inline void videoDecoderReleaseFrame(VideoDecoder *d, VideoFrame *f) { (void)d; (void)f; abort(); }
static inline Result videoDecoderCopyFrame(VideoDecoder *d, VideoFrame *f, u8 *y, size_t yp, u8 *uv, size_t uvp)
{
    (void)d; (void)f; (void)y; (void)yp; (void)uv; (void)uvp;
    abort();
}
static inline Result videoDecoderSubmit(VideoDecoder *d, const void *p, size_t n, s64 t, bool last, u64 wait)
{
    (void)d; (void)p; (void)n; (void)t; (void)last; (void)wait;
    abort();
}
static inline Result svcGetInfo(u64 *out, int kind, int handle, int sub)
{
    (void)kind; (void)handle; (void)sub;
    *out = 0x3f;
    return 0;
}
static inline u64 armGetSystemTick(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (u64)t.tv_sec * 1000000000ull + (u64)t.tv_nsec;
}
static inline u64 armTicksToNs(u64 ticks) { return ticks; }
