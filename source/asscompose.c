#include "asscompose.h"

#define OPAQUE 255u

static int minInt(int a, int b)
{
    return a < b ? a : b;
}

static int maxInt(int a, int b)
{
    return a > b ? a : b;
}

AssBox assBoxAdd(AssBox box, int x, int y, int w, int h, int frame_w, int frame_h)
{
    int x0 = maxInt(x, 0), y0 = maxInt(y, 0);
    int x1 = minInt(x + w, frame_w), y1 = minInt(y + h, frame_h);
    if (x1 <= x0 || y1 <= y0)
        return box;
    if (box.w > 0 && box.h > 0) {
        x1 = maxInt(x1, box.x + box.w);
        y1 = maxInt(y1, box.y + box.h);
        x0 = minInt(x0, box.x);
        y0 = minInt(y0, box.y);
    }
    return (AssBox){x0, y0, x1 - x0, y1 - y0};
}

// A color channel of source over destination, weighted by what each adds.
static uint32_t blendChannel(uint32_t src, uint32_t dst, uint32_t src_weight, uint32_t dst_weight, uint32_t total)
{
    return (src * src_weight + dst * dst_weight + total / 2) / total;
}

static uint32_t blendPixel(uint32_t dst, uint32_t r, uint32_t g, uint32_t b, uint32_t alpha)
{
    uint32_t dst_alpha = dst >> 24;
    if (alpha == OPAQUE || dst_alpha == 0)
        return alpha << 24 | b << 16 | g << 8 | r;
    uint32_t dst_weight = (dst_alpha * (OPAQUE - alpha) + OPAQUE / 2) / OPAQUE;
    uint32_t total = alpha + dst_weight;
    uint32_t out_r = blendChannel(r, dst & 0xFF, alpha, dst_weight, total);
    uint32_t out_g = blendChannel(g, (dst >> 8) & 0xFF, alpha, dst_weight, total);
    uint32_t out_b = blendChannel(b, (dst >> 16) & 0xFF, alpha, dst_weight, total);
    return total << 24 | out_b << 16 | out_g << 8 | out_r;
}

void assComposeLayer(uint32_t *pixels, AssBox box, const AssLayer *layer)
{
    uint32_t r = layer->rgba >> 24, g = (layer->rgba >> 16) & 0xFF, b = (layer->rgba >> 8) & 0xFF;
    uint32_t opacity = OPAQUE - (layer->rgba & 0xFF);
    int x0 = maxInt(layer->x, box.x), x1 = minInt(layer->x + layer->w, box.x + box.w);
    int y0 = maxInt(layer->y, box.y), y1 = minInt(layer->y + layer->h, box.y + box.h);
    for (int y = y0; y < y1; y++) {
        const uint8_t *mask = layer->mask + (y - layer->y) * layer->stride - layer->x;
        uint32_t *row = pixels + (y - box.y) * box.w - box.x;
        for (int x = x0; x < x1; x++) {
            uint32_t alpha = (mask[x] * opacity + OPAQUE / 2) / OPAQUE;
            if (alpha)
                row[x] = blendPixel(row[x], r, g, b, alpha);
        }
    }
}
