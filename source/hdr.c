#include "hdr.h"

#include <math.h>

// SMPTE ST 2084's constants.
#define PQ_M1 0.1593017578125f
#define PQ_M2 78.84375f
#define PQ_C1 0.8359375f
#define PQ_C2 18.8515625f
#define PQ_C3 18.6875f
#define PQ_PEAK_NITS 10000.0f
// ARIB STD-B67's, and BT.2100's HLG display at its nominal 1000 nits (system gamma 1.2).
#define HLG_A 0.17883277f
#define HLG_B 0.28466892f
#define HLG_C 0.55991073f
#define HLG_GAMMA_MINUS_ONE 0.2f
// BT.2020's luma weights.
#define KR_2020 0.2627f
#define KG_2020 0.6780f
#define KB_2020 0.0593f
#define SCREEN_GAMMA 2.4f  // BT.1886.

HdrKind hdrDetect(int color_trc, int color_primaries, bool has_mastering)
{
    if (color_trc == HDR_TRC_PQ)
        return HDR_PQ;
    if (color_trc == HDR_TRC_HLG)
        return HDR_HLG;
    // HDR10 data on a BT.2020 stream whose transfer isn't tagged: it's PQ.
    return has_mastering && color_primaries == HDR_PRIMARIES_BT2020 ? HDR_PQ : HDR_NONE;
}

static float clampPeak(float nits)
{
    return nits < HDR_MIN_PEAK_NITS ? HDR_MIN_PEAK_NITS : nits > HDR_MAX_PEAK_NITS ? HDR_MAX_PEAK_NITS : nits;
}

float hdrSourcePeak(HdrKind kind, float max_cll_nits, float mastering_peak_nits)
{
    if (kind != HDR_PQ)
        return HDR_DEFAULT_PEAK_NITS;
    if (max_cll_nits > 0)
        return clampPeak(max_cll_nits);
    if (mastering_peak_nits > 0)
        return clampPeak(mastering_peak_nits);
    return HDR_DEFAULT_PEAK_NITS;
}

float hdrPqToNits(float signal)
{
    float e = signal < 0 ? 0 : signal > 1 ? 1 : signal;
    float p = powf(e, 1.0f / PQ_M2);
    float num = p - PQ_C1 > 0 ? p - PQ_C1 : 0;
    return PQ_PEAK_NITS * powf(num / (PQ_C2 - PQ_C3 * p), 1.0f / PQ_M1);
}

float hdrNitsToPq(float nits)
{
    float y = nits <= 0 ? 0 : nits >= PQ_PEAK_NITS ? 1 : nits / PQ_PEAK_NITS;
    float l = powf(y, PQ_M1);
    return powf((PQ_C1 + PQ_C2 * l) / (1.0f + PQ_C3 * l), PQ_M2);
}

float hdrHlgToScene(float signal)
{
    float e = signal < 0 ? 0 : signal > 1 ? 1 : signal;
    return e <= 0.5f ? e * e / 3.0f : (expf((e - HLG_C) / HLG_A) + HLG_B) / 12.0f;
}

HdrParams hdrParams(HdrKind kind, float source_peak_nits)
{
    HdrParams p = {.kind = kind, .source_peak = clampPeak(source_peak_nits)};
    p.source_pq = hdrNitsToPq(p.source_peak);
    p.max_lum = hdrNitsToPq(HDR_SDR_WHITE_NITS) / p.source_pq;
    if (p.max_lum > 1)
        p.max_lum = 1;
    p.knee = 1.5f * p.max_lum - 0.5f;
    return p;
}

float hdrToneMap(const HdrParams *p, float nits)
{
    float e = hdrNitsToPq(nits) / p->source_pq;  // 0-1 over the source's range.
    if (e > 1)
        e = 1;
    if (e > p->knee && p->knee < 1) {
        // BT.2390's Hermite spline from the knee to the SDR white.
        float t = (e - p->knee) / (1 - p->knee), t2 = t * t, t3 = t2 * t;
        e = (2 * t3 - 3 * t2 + 1) * p->knee + (t3 - 2 * t2 + t) * (1 - p->knee) + (-2 * t3 + 3 * t2) * p->max_lum;
    }
    float out = hdrPqToNits(e * p->source_pq);
    return out > HDR_SDR_WHITE_NITS ? HDR_SDR_WHITE_NITS : out;
}

// BT.2020's colors in BT.709's (linear light), by rows.
static const float TO_709[3][3] = {
    {1.6605f, -0.5876f, -0.0728f},
    {-0.1246f, 1.1329f, -0.0083f},
    {-0.0182f, -0.1006f, 1.1187f},
};

static void toNits(const HdrParams *p, const float rgb[3], float nits[3])
{
    if (p->kind == HDR_HLG) {
        float scene[3];
        for (int c = 0; c < 3; c++)
            scene[c] = hdrHlgToScene(rgb[c]);
        float ys = KR_2020 * scene[0] + KG_2020 * scene[1] + KB_2020 * scene[2];
        float gain = HDR_DEFAULT_PEAK_NITS * (ys > 0 ? powf(ys, HLG_GAMMA_MINUS_ONE) : 0);
        for (int c = 0; c < 3; c++)
            nits[c] = gain * scene[c];
        return;
    }
    for (int c = 0; c < 3; c++)
        nits[c] = hdrPqToNits(rgb[c]);
}

void hdrPixel(const HdrParams *p, const float rgb2020[3], float out709[3])
{
    float nits[3], lin[3];
    toNits(p, rgb2020, nits);
    float peak = 0;
    for (int r = 0; r < 3; r++) {
        lin[r] = TO_709[r][0] * nits[0] + TO_709[r][1] * nits[1] + TO_709[r][2] * nits[2];
        if (lin[r] < 0)
            lin[r] = 0;  // Outside BT.709: clipped.
        if (lin[r] > peak)
            peak = lin[r];
    }
    // The brightest channel goes through the curve and the others follow it, so colors keep their hue.
    float scale = peak > 0 ? hdrToneMap(p, peak) / peak : 0;
    for (int c = 0; c < 3; c++) {
        float v = lin[c] * scale / HDR_SDR_WHITE_NITS;
        out709[c] = v >= 1 ? 1 : powf(v, 1.0f / SCREEN_GAMMA);
    }
}

static uint8_t toByte(float v)
{
    return v <= 0 ? 0 : v >= 1 ? 255 : (uint8_t)(v * 255.0f + 0.5f);
}

static float unit(float v)
{
    return v < 0 ? 0 : v > 1 ? 1 : v;
}

void hdrYuvToRgb(float y, float cb, float cr, float rgb[3])
{
    // BT.2020's non-constant luminance matrix.
    rgb[0] = unit(y + 1.4746f * cr);
    rgb[1] = unit(y - 0.16455f * cb - 0.57135f * cr);
    rgb[2] = unit(y + 1.8814f * cb);
}

void hdrPixelYuv8(const HdrParams *p, uint8_t y, uint8_t cb, uint8_t cr, uint8_t rgb[3])
{
    float in[3], out[3];  // Limited range.
    hdrYuvToRgb((y - 16.0f) / 219.0f, (cb - 128.0f) / 224.0f, (cr - 128.0f) / 224.0f, in);
    hdrPixel(p, in, out);
    for (int c = 0; c < 3; c++)
        rgb[c] = toByte(out[c]);
}

void hdrLutInit(HdrLut *t, HdrKind kind, float source_peak_nits)
{
    t->p = hdrParams(kind, source_peak_nits);
    for (int i = 0; i <= HDR_LUT_SIGNAL; i++) {
        float e = (float)i / HDR_LUT_SIGNAL;
        t->light[i] = kind == HDR_HLG ? hdrHlgToScene(e) : hdrPqToNits(e);
    }
    for (int i = 0; i <= HDR_LUT_TONE; i++)
        t->tone[i] = hdrToneMap(&t->p, t->p.source_peak * i / HDR_LUT_TONE);
    // toByte(powf(v, 1 / 2.4)) is level k from ((k - 0.5) / 255)^2.4 on.
    for (int k = 1; k <= 255; k++)
        t->level[k - 1] = powf((k - 0.5f) / 255.0f, SCREEN_GAMMA);
    t->level[255] = 2.0f;  // Past any light (it's at most 1).
    // A step of the fourth root spans less than a level, so from a step's start
    // the level is found in a move or two.
    int k = 0;
    for (int i = 0; i <= HDR_LUT_LEVEL; i++) {
        float w = (float)i / HDR_LUT_LEVEL, v = w * w * w * w;
        while (t->level[k] <= v)
            k++;
        t->level_at[i] = (uint8_t)k;
    }
}

// A table over 0-1 in steps of 1 / steps, read between its steps.
static float lookUp(const float *table, int steps, float x)
{
    float at = x * steps;
    if (!(at > 0))
        return table[0];
    if (at >= steps)
        return table[steps];
    int i = (int)at;
    return table[i] + (table[i + 1] - table[i]) * (at - i);
}

// The 8-bit level of light v (of the SDR white): how many levels start at or under it.
static uint8_t levelOf(const HdrLut *t, float v)
{
    if (!(v > 0))
        return 0;
    if (v >= 1)
        return 255;
    int k = t->level_at[(int)(sqrtf(sqrtf(v)) * HDR_LUT_LEVEL)];
    while (t->level[k] <= v)
        k++;
    return (uint8_t)k;
}

void hdrLutPixel(const HdrLut *t, const float rgb2020[3], uint8_t out[3])
{
    float nits[3], lin[3];
    for (int c = 0; c < 3; c++)
        nits[c] = lookUp(t->light, HDR_LUT_SIGNAL, rgb2020[c]);
    if (t->p.kind == HDR_HLG) {  // Scene light to the display's, as toNits does.
        float ys = KR_2020 * nits[0] + KG_2020 * nits[1] + KB_2020 * nits[2];
        float gain = HDR_DEFAULT_PEAK_NITS * (ys > 0 ? powf(ys, HLG_GAMMA_MINUS_ONE) : 0);
        for (int c = 0; c < 3; c++)
            nits[c] *= gain;
    }
    float peak = 0;
    for (int r = 0; r < 3; r++) {
        lin[r] = TO_709[r][0] * nits[0] + TO_709[r][1] * nits[1] + TO_709[r][2] * nits[2];
        if (lin[r] < 0)
            lin[r] = 0;
        if (lin[r] > peak)
            peak = lin[r];
    }
    // As hdrPixel: the brightest channel through the curve (over the peak, as the peak), the others following.
    float scale = peak > 0 ? lookUp(t->tone, HDR_LUT_TONE, peak / t->p.source_peak) / peak / HDR_SDR_WHITE_NITS : 0;
    for (int c = 0; c < 3; c++)
        out[c] = levelOf(t, lin[c] * scale);
}
