// HDR video shown on an SDR screen: which streams are HDR (HDR10's PQ, or
// HLG), and the maths that brings their light down to what the screen shows.
// The GPU runs the same maths per pixel in gfx_gl.c's HDR shader; this C
// version is what the host tests check, and what thumbnails use. Pure logic.
//
// The steps, per pixel: Y'CbCr (BT.2020) to R'G'B'; PQ or HLG to light in
// nits; BT.2020 colors to BT.709's; the brightest channel's light through
// BT.2390's curve, which keeps everything up to about 100 nits as it was
// mastered and squeezes the brighter part under the SDR white (203 nits);
// and BT.1886's gamma (2.4) for the screen.
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum { HDR_NONE, HDR_PQ, HDR_HLG } HdrKind;

// FFmpeg's numbers for them (AVColorTransferCharacteristic, AVColorPrimaries).
#define HDR_TRC_PQ 16       // AVCOL_TRC_SMPTE2084
#define HDR_TRC_HLG 18      // AVCOL_TRC_ARIB_STD_B67
#define HDR_PRIMARIES_BT2020 9  // AVCOL_PRI_BT2020

#define HDR_SDR_WHITE_NITS 203.0f    // BT.2408's reference white: the SDR screen's 100%.
#define HDR_DEFAULT_PEAK_NITS 1000.0f  // When the file doesn't say (and HLG's nominal peak).
#define HDR_MIN_PEAK_NITS 400.0f
#define HDR_MAX_PEAK_NITS 10000.0f

// The stream's kind, from its transfer, its primaries, and whether it carries
// HDR10's mastering display data (a PQ file whose transfer isn't tagged).
HdrKind hdrDetect(int color_trc, int color_primaries, bool has_mastering);

// The brightest the picture gets, in nits: the content light level (MaxCLL)
// if the file has it, else the mastering display's peak, else 1000. 0 means
// "not said". HLG is always 1000.
float hdrSourcePeak(HdrKind kind, float max_cll_nits, float mastering_peak_nits);

// What the shader needs, worked out once a video: BT.2390's curve in PQ.
typedef struct {
    HdrKind kind;
    float source_peak;   // Nits.
    float source_pq;     // The peak in PQ (0-1).
    float max_lum;       // The SDR white over the peak, both in PQ (normalized, <= 1).
    float knee;          // Where the curve starts bending (normalized PQ); 1 when nothing bends.
} HdrParams;

HdrParams hdrParams(HdrKind kind, float source_peak_nits);

// PQ (SMPTE ST 2084): signal (0-1) to light (0-10000 nits), and back.
float hdrPqToNits(float signal);
float hdrNitsToPq(float nits);
// HLG (ARIB STD-B67): signal (0-1) to scene light (0-1).
float hdrHlgToScene(float signal);
// BT.2390's curve: light in, nits, to light out, at most the SDR white.
float hdrToneMap(const HdrParams *p, float nits);

// One pixel: R'G'B' (0-1, BT.2020, as coded) to the screen's R'G'B' (0-1, BT.709).
void hdrPixel(const HdrParams *p, const float rgb2020[3], float out709[3]);
// One limited-range 8-bit Y'CbCr pixel (BT.2020) to the screen's 8-bit RGB.
void hdrPixelYuv8(const HdrParams *p, uint8_t y, uint8_t cb, uint8_t cr, uint8_t rgb[3]);
