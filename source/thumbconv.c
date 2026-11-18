// thumbgen.h's picture maths, apart from its thread so the host tests run it.
#include "thumbgen.h"

static uint8_t clampByte(float v)
{
    return v <= 0.0f ? 0 : v >= 255.0f ? 255 : (uint8_t)(v + 0.5f);
}

void thumbgenConvert(const uint8_t *y, const uint8_t *uv, int w, int h, int y_pitch, int uv_pitch, bool bt709,
                     bool full_range, uint8_t *rgb, int out_w, int out_h)
{
    // The middle of the frame with the picture's shape, each output pixel
    // the average of the luma under it (chroma from its middle).
    double scale = (double)out_w / w > (double)out_h / h ? (double)out_w / w : (double)out_h / h;
    double cw = out_w / scale, ch = out_h / scale, cx = (w - cw) / 2.0, cy = (h - ch) / 2.0;
    float kr = bt709 ? 1.5748f : 1.402f, kgb = bt709 ? 0.1873f : 0.344136f, kgr = bt709 ? 0.4681f : 0.714136f,
          kb = bt709 ? 1.8556f : 1.772f;
    for (int oy = 0; oy < out_h; oy++) {
        int y0 = (int)(cy + oy * ch / out_h), y1 = (int)(cy + (oy + 1) * ch / out_h);
        if (y1 <= y0) y1 = y0 + 1;
        if (y1 > h) y1 = h;
        for (int ox = 0; ox < out_w; ox++) {
            int x0 = (int)(cx + ox * cw / out_w), x1 = (int)(cx + (ox + 1) * cw / out_w);
            if (x1 <= x0) x1 = x0 + 1;
            if (x1 > w) x1 = w;
            unsigned sum = 0, n = 0;
            for (int sy = y0; sy < y1; sy++)
                for (int sx = x0; sx < x1; sx++, n++)
                    sum += y[sy * y_pitch + sx];
            int mx = ((x0 + x1) / 2) & ~1, my = (y0 + y1) / 2;
            const uint8_t *c = &uv[(my / 2) * uv_pitch + mx];
            float lum = n ? (float)sum / n : 0.0f, cb = c[0] - 128.0f, cr = c[1] - 128.0f;
            if (!full_range) {
                lum = (lum - 16.0f) * (255.0f / 219.0f);
                cb *= 255.0f / 224.0f;
                cr *= 255.0f / 224.0f;
            }
            uint8_t *p = &rgb[(oy * out_w + ox) * 3];
            p[0] = clampByte(lum + kr * cr);
            p[1] = clampByte(lum - kgb * cb - kgr * cr);
            p[2] = clampByte(lum + kb * cb);
        }
    }
}

int thumbgenLuma(const uint8_t *y, int w, int h, int pitch)
{
    unsigned long long sum = 0, n = 0;
    for (int row = 0; row < h; row += 8)  // A sample is enough.
        for (int col = 0; col < w; col += 8, n++)
            sum += y[row * pitch + col];
    return n ? (int)(sum / n) : 0;
}
