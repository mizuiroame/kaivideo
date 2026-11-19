#include "trickplay.h"

#include <stdlib.h>
#include <string.h>

#include <jansson.h>

#define NS_PER_MS 1000000ll

static int intOf(const json_t *object, const char *key)
{
    return (int)json_integer_value(json_object_get(object, key));
}

bool trickplayParse(const char *json, const char *media_source, TrickplayInfo *out)
{
    json_t *root = json_loads(json, 0, NULL);
    const json_t *item = json_array_get(json_object_get(root, "Items"), 0);
    const json_t *sizes = json_object_get(json_object_get(item, "Trickplay"), media_source);
    memset(out, 0, sizeof(*out));
    const char *key;
    const json_t *size;
    json_object_foreach((json_t *)sizes, key, size) {
        int width = intOf(size, "Width");
        bool fits = width > 0 && width <= TRICKPLAY_MAX_WIDTH;
        if (!fits || width <= out->width)
            continue;  // Too wide, or not larger than the one kept.
        TrickplayInfo t = {width, intOf(size, "Height"), intOf(size, "TileWidth"), intOf(size, "TileHeight"),
                           intOf(size, "ThumbnailCount"), (int64_t)intOf(size, "Interval") * NS_PER_MS};
        if (t.height > 0 && t.tile_w > 0 && t.tile_h > 0 && t.count > 0 && t.interval_ns > 0)
            *out = t;
    }
    json_decref(root);
    return out->width > 0;
}

bool trickplayLocate(const TrickplayInfo *t, int64_t pos_ns, TrickplaySpot *out)
{
    if (t->count <= 0 || t->interval_ns <= 0 || t->tile_w <= 0 || t->tile_h <= 0)
        return false;
    int64_t index = pos_ns > 0 ? pos_ns / t->interval_ns : 0;
    if (index >= t->count)
        index = t->count - 1;
    int per_sheet = t->tile_w * t->tile_h;
    int within = (int)(index % per_sheet);
    out->sheet = (int)(index / per_sheet);
    out->x = (within % t->tile_w) * t->width;
    out->y = (within / t->tile_w) * t->height;
    return true;
}
