#include "picsubs.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

// A rectangle kept: its indexes run-length coded as (count, index) byte pairs, row after row.
typedef struct {
    int x, y, w, h;
    uint8_t *runs;
    size_t run_bytes;
    uint32_t palette[256];
} KeptRect;

typedef struct {
    int64_t start_ns, end_ns;  // end_ns < 0: until the next one.
    int canvas_w, canvas_h;
    KeptRect *rects;
    int count;
    size_t bytes;
    unsigned serial;           // Tells pictures apart in the cache.
} Picture;

struct PicSubs {
    Picture *pictures;  // By start time.
    int count, capacity;
    size_t bytes;
    unsigned next_serial;
    // What was drawn last, kept while nothing changes.
    uint32_t *pixels;
    size_t pixels_size;
    unsigned drawn_serial;
    int drawn_w, drawn_h, drawn_lift;
    AssPicture drawn;
    unsigned id;
    // The picture on its canvas, premultiplied, before scaling.
    uint32_t *canvas;
    size_t canvas_size;
};

PicSubs *picSubsCreate(void)
{
    return calloc(1, sizeof(PicSubs));
}

static void freePicture(Picture *p)
{
    for (int i = 0; i < p->count; i++)
        free(p->rects[i].runs);
    free(p->rects);
}

void picSubsFree(PicSubs *s)
{
    if (!s)
        return;
    for (int i = 0; i < s->count; i++)
        freePicture(&s->pictures[i]);
    free(s->pictures);
    free(s->pixels);
    free(s->canvas);
    free(s);
}

int picSubsCount(const PicSubs *s)
{
    return s ? s->count : 0;
}

size_t picSubsBytes(const PicSubs *s)
{
    return s ? s->bytes : 0;
}

// --- Keeping ---

// The rectangle's indexes as (count, index) pairs. NULL if out of memory.
static uint8_t *encodeRuns(const PicRect *r, size_t *bytes)
{
    size_t capacity = 256, n = 0;
    uint8_t *out = malloc(capacity);
    for (int y = 0; out && y < r->h; y++) {
        const uint8_t *row = r->indices + (size_t)y * (size_t)r->stride;
        for (int x = 0; x < r->w;) {
            int run = 1;
            while (x + run < r->w && run < 255 && row[x + run] == row[x])
                run++;
            if (n + 2 > capacity) {
                uint8_t *grown = realloc(out, capacity * 2);
                if (!grown) {
                    free(out);
                    return NULL;
                }
                out = grown;
                capacity *= 2;
            }
            out[n++] = (uint8_t)run;
            out[n++] = row[x];
            x += run;
        }
    }
    *bytes = n;
    return out;
}

static bool validRect(const PicRect *r, int canvas_w, int canvas_h)
{
    return r->w > 0 && r->h > 0 && r->x >= 0 && r->y >= 0 && r->x <= canvas_w - r->w && r->y <= canvas_h - r->h &&
           r->indices && r->stride >= r->w && r->palette && r->palette_count >= 1 && r->palette_count <= 256;
}

static bool keepRect(KeptRect *k, const PicRect *r)
{
    memset(k, 0, sizeof(*k));
    k->x = r->x;
    k->y = r->y;
    k->w = r->w;
    k->h = r->h;
    memcpy(k->palette, r->palette, (size_t)r->palette_count * sizeof(uint32_t));
    k->runs = encodeRuns(r, &k->run_bytes);
    return k->runs != NULL;
}

// The first picture starting at or after start_ns.
static int firstFrom(const PicSubs *s, int64_t start_ns)
{
    int lo = 0, hi = s->count;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (s->pictures[mid].start_ns < start_ns)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

// Lets the oldest pictures go while the track holds too much.
static void dropOldest(PicSubs *s)
{
    int drop = 0;
    while (drop < s->count - 1 && s->bytes > PICSUBS_BYTES_MAX) {
        s->bytes -= s->pictures[drop].bytes;
        freePicture(&s->pictures[drop]);
        drop++;
    }
    if (drop > 0) {
        memmove(s->pictures, s->pictures + drop, (size_t)(s->count - drop) * sizeof(Picture));
        s->count -= drop;
    }
}

bool picSubsAdd(PicSubs *s, int64_t start_ns, int64_t end_ns, int canvas_w, int canvas_h, const PicRect *rects,
                int count)
{
    if (!s || canvas_w <= 0 || canvas_h <= 0 || canvas_w > PICSUBS_CANVAS_MAX || canvas_h > PICSUBS_CANVAS_MAX ||
        count < 0)
        return false;
    count = count < PICSUBS_RECTS_MAX ? count : PICSUBS_RECTS_MAX;
    for (int i = 0; i < count; i++)
        if (!validRect(&rects[i], canvas_w, canvas_h))
            return false;
    int at = firstFrom(s, start_ns);
    if (at < s->count && s->pictures[at].start_ns == start_ns)
        return true;  // Read again after a seek: kept already.
    if (s->count == s->capacity) {
        int capacity = s->capacity ? s->capacity * 2 : 64;
        Picture *grown = realloc(s->pictures, (size_t)capacity * sizeof(Picture));
        if (!grown)
            return false;
        s->pictures = grown;
        s->capacity = capacity;
    }
    Picture p = {start_ns, end_ns > start_ns ? end_ns : -1, canvas_w, canvas_h, NULL, 0, 0, ++s->next_serial};
    p.rects = count ? calloc((size_t)count, sizeof(KeptRect)) : NULL;
    if (count && !p.rects)
        return false;
    for (int i = 0; i < count; i++) {
        if (!keepRect(&p.rects[i], &rects[i])) {
            p.count = i;
            freePicture(&p);
            return false;
        }
        p.bytes += p.rects[i].run_bytes + sizeof(KeptRect);
    }
    p.count = count;
    memmove(s->pictures + at + 1, s->pictures + at, (size_t)(s->count - at) * sizeof(Picture));
    s->pictures[at] = p;
    s->count++;
    s->bytes += p.bytes;
    dropOldest(s);
    return true;
}

// --- Drawing ---

// The picture shown at pos_ns, or NULL.
static const Picture *shownAt(const PicSubs *s, int64_t pos_ns)
{
    int at = firstFrom(s, pos_ns + 1) - 1;  // The last one started by pos_ns.
    if (at < 0 || s->pictures[at].count == 0)
        return NULL;
    const Picture *p = &s->pictures[at];
    int64_t end = p->end_ns;
    if (end < 0) {
        end = p->start_ns + PICSUBS_OPEN_NS;
        if (at + 1 < s->count && s->pictures[at + 1].start_ns < end)
            end = s->pictures[at + 1].start_ns;
    }
    return pos_ns < end ? p : NULL;
}

static bool reserve(uint32_t **buffer, size_t *size, size_t pixels)
{
    if (pixels <= *size)
        return true;
    uint32_t *grown = realloc(*buffer, pixels * sizeof(uint32_t));
    if (!grown)
        return false;
    *buffer = grown;
    *size = pixels;
    return true;
}

// 0xAARRGGBB straight into 0xAABBGGRR premultiplied.
static uint32_t premultiplied(uint32_t argb)
{
    uint32_t a = argb >> 24, r = (argb >> 16) & 0xFF, g = (argb >> 8) & 0xFF, b = argb & 0xFF;
    r = (r * a + 127) / 255;
    g = (g * a + 127) / 255;
    b = (b * a + 127) / 255;
    return a << 24 | b << 16 | g << 8 | r;
}

// The picture's rectangles into s->canvas (box_w x box_h, from box_x, box_y on its canvas).
static bool paint(PicSubs *s, const Picture *p, int box_x, int box_y, int box_w, int box_h)
{
    if (!reserve(&s->canvas, &s->canvas_size, (size_t)box_w * (size_t)box_h))
        return false;
    memset(s->canvas, 0, (size_t)box_w * (size_t)box_h * sizeof(uint32_t));
    for (int i = 0; i < p->count; i++) {
        const KeptRect *k = &p->rects[i];
        uint32_t colors[256];
        for (int c = 0; c < 256; c++)
            colors[c] = premultiplied(k->palette[c]);
        size_t at = 0;
        int x = 0, y = 0;
        while (at + 1 < k->run_bytes && y < k->h) {
            int run = k->runs[at];
            uint32_t color = colors[k->runs[at + 1]];
            at += 2;
            uint32_t *row = s->canvas + (size_t)(k->y - box_y + y) * (size_t)box_w + (size_t)(k->x - box_x);
            for (int n = 0; n < run && x < k->w; n++, x++)
                if (color)
                    row[x] = color;
            if (x >= k->w) {
                x = 0;
                y++;
            }
        }
    }
    return true;
}

// One channel of four premultiplied pixels, weighed.
static uint32_t mix(uint32_t a, uint32_t b, uint32_t c, uint32_t d, int shift, float fx, float fy)
{
    float top = ((a >> shift) & 0xFF) * (1 - fx) + ((b >> shift) & 0xFF) * fx;
    float bottom = ((c >> shift) & 0xFF) * (1 - fx) + ((d >> shift) & 0xFF) * fx;
    return (uint32_t)(top * (1 - fy) + bottom * fy + 0.5f);
}

// The canvas's pixel at (x, y), transparent outside it.
static uint32_t pixelAt(const uint32_t *canvas, int w, int h, int x, int y)
{
    return x >= 0 && y >= 0 && x < w && y < h ? canvas[(size_t)y * (size_t)w + (size_t)x] : 0;
}

// s->canvas (box_w x box_h) scaled into s->pixels (out_w x out_h), straight alpha again.
static void scale(PicSubs *s, int box_w, int box_h, int out_w, int out_h)
{
    float sx = (float)box_w / (float)out_w, sy = (float)box_h / (float)out_h;
    for (int oy = 0; oy < out_h; oy++) {
        float v = (oy + 0.5f) * sy - 0.5f;
        int y0 = (int)floorf(v);
        float fy = v - y0;
        for (int ox = 0; ox < out_w; ox++) {
            float u = (ox + 0.5f) * sx - 0.5f;
            int x0 = (int)floorf(u);
            float fx = u - x0;
            uint32_t a = pixelAt(s->canvas, box_w, box_h, x0, y0), b = pixelAt(s->canvas, box_w, box_h, x0 + 1, y0);
            uint32_t c = pixelAt(s->canvas, box_w, box_h, x0, y0 + 1);
            uint32_t d = pixelAt(s->canvas, box_w, box_h, x0 + 1, y0 + 1);
            uint32_t alpha = mix(a, b, c, d, 24, fx, fy), out = 0;
            if (alpha > 0) {
                uint32_t r = mix(a, b, c, d, 0, fx, fy), g = mix(a, b, c, d, 8, fx, fy), bl = mix(a, b, c, d, 16, fx, fy);
                r = r * 255 / alpha;
                g = g * 255 / alpha;
                bl = bl * 255 / alpha;
                out = alpha << 24 | (bl > 255 ? 255 : bl) << 16 | (g > 255 ? 255 : g) << 8 | (r > 255 ? 255 : r);
            }
            s->pixels[(size_t)oy * (size_t)out_w + (size_t)ox] = out;
        }
    }
}

// Where a picture goes in the area: its box on the canvas scaled, and lifted above what covers the bottom.
static void place(const Picture *p, int box_x, int box_y, int box_w, int box_h, const AssArea *area, AssPicture *out)
{
    double sx = (double)area->width / p->canvas_w, sy = (double)area->height / p->canvas_h;
    out->x = (int)floor(box_x * sx);
    out->y = (int)floor(box_y * sy);
    out->w = (int)ceil((box_x + box_w) * sx) - out->x;
    out->h = (int)ceil((box_y + box_h) * sy) - out->y;
    out->w = out->w > 0 ? out->w : 1;
    out->h = out->h > 0 ? out->h : 1;
    int limit = (int)(area->height * (1.0 - area->lift_percent / 100.0));
    if (area->lift_percent > 0 && out->y > area->height / 2 && out->y + out->h > limit)
        out->y = limit - out->h > 0 ? limit - out->h : 0;
}

bool picSubsRender(PicSubs *s, int64_t pos_ns, const AssArea *area, AssPicture *out)
{
    memset(out, 0, sizeof(*out));
    const Picture *p = s && area->width > 0 && area->height > 0 ? shownAt(s, pos_ns) : NULL;
    if (!p)
        return true;
    int lift = (int)(area->lift_percent * 100);
    if (s->drawn.w > 0 && s->drawn_serial == p->serial && s->drawn_w == area->width && s->drawn_h == area->height &&
        s->drawn_lift == lift) {
        *out = s->drawn;
        return true;
    }
    int x0 = p->canvas_w, y0 = p->canvas_h, x1 = 0, y1 = 0;
    for (int i = 0; i < p->count; i++) {
        const KeptRect *k = &p->rects[i];
        x0 = k->x < x0 ? k->x : x0;
        y0 = k->y < y0 ? k->y : y0;
        x1 = k->x + k->w > x1 ? k->x + k->w : x1;
        y1 = k->y + k->h > y1 ? k->y + k->h : y1;
    }
    AssPicture placed;
    memset(&placed, 0, sizeof(placed));
    place(p, x0, y0, x1 - x0, y1 - y0, area, &placed);
    if (!paint(s, p, x0, y0, x1 - x0, y1 - y0) ||
        !reserve(&s->pixels, &s->pixels_size, (size_t)placed.w * (size_t)placed.h))
        return false;
    scale(s, x1 - x0, y1 - y0, placed.w, placed.h);
    placed.pixels = s->pixels;
    placed.id = ++s->id;
    s->drawn = placed;
    s->drawn_serial = p->serial;
    s->drawn_w = area->width;
    s->drawn_h = area->height;
    s->drawn_lift = lift;
    *out = placed;
    return true;
}
