#include "asssubs.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <ass/ass.h>

#include "asscompose.h"

#define MAX_TRACKS 8
#define GLYPH_CACHE_ENTRIES 2000
#define BITMAP_CACHE_MB 32

struct AssSubs {
    ASS_Library *library;
    ASS_Renderer *renderer;
    ASS_Track *tracks[MAX_TRACKS];
    int track_count;
    pthread_mutex_t lock;  // Events come from the decoding thread.
    uint32_t *pixels;      // Room for a whole area.
    int width, height;
    double font_scale, lift_percent;
    int last_track;
    AssPicture picture;
};

static unsigned g_next_id = 1;  // Unique across videos: the GPU keeps images by it.

static void ignoreMessage(int level, const char *format, va_list args, void *data)
{
    (void)level;
    (void)format;
    (void)args;
    (void)data;
}

AssSubs *assSubsCreate(int video_w, int video_h, const AssFont *fonts, int font_count)
{
    AssSubs *a = calloc(1, sizeof(*a));
    if (!a)
        return NULL;
    pthread_mutex_init(&a->lock, NULL);
    a->last_track = -1;
    a->library = ass_library_init();
    if (a->library) {
        ass_set_message_cb(a->library, ignoreMessage, NULL);
        for (int i = 0; i < font_count; i++)
            ass_add_font(a->library, fonts[i].name, fonts[i].data, (int)fonts[i].size);
        a->renderer = ass_renderer_init(a->library);
    }
    if (!a->renderer) {
        assSubsClose(a);
        return NULL;
    }
    ass_set_storage_size(a->renderer, video_w, video_h);
    ass_set_cache_limits(a->renderer, GLYPH_CACHE_ENTRIES, BITMAP_CACHE_MB);
    ass_set_fonts(a->renderer, NULL, ASSSUBS_DEFAULT_FAMILY, ASS_FONTPROVIDER_NONE, NULL, 1);
    return a;
}

void assSubsClose(AssSubs *a)
{
    if (!a)
        return;
    for (int i = 0; i < a->track_count; i++)
        ass_free_track(a->tracks[i]);
    if (a->renderer)
        ass_renderer_done(a->renderer);
    if (a->library)
        ass_library_done(a->library);
    pthread_mutex_destroy(&a->lock);
    free(a->pixels);
    free(a);
}

int assSubsAddTrack(AssSubs *a, const char *header, size_t size)
{
    pthread_mutex_lock(&a->lock);
    int number = -1;
    ASS_Track *track = a->track_count < MAX_TRACKS ? ass_new_track(a->library) : NULL;
    if (track) {
        if (header && size)
            ass_process_codec_private(track, (char *)header, (int)size);  // Older libass isn't const.
        number = a->track_count;
        a->tracks[a->track_count++] = track;
    }
    pthread_mutex_unlock(&a->lock);
    return number;
}

void assSubsAddEvent(AssSubs *a, int track, const char *data, size_t size, int64_t start_ms, int64_t duration_ms)
{
    pthread_mutex_lock(&a->lock);
    if (track >= 0 && track < a->track_count)
        ass_process_chunk(a->tracks[track], (char *)data, (int)size, start_ms, duration_ms);
    pthread_mutex_unlock(&a->lock);
}

// Sets the renderer up for this area. False if there's no memory for it.
static bool prepareArea(AssSubs *a, const AssArea *area)
{
    if (area->width != a->width || area->height != a->height) {
        uint32_t *pixels = realloc(a->pixels, (size_t)area->width * (size_t)area->height * sizeof(*pixels));
        if (!pixels)
            return false;
        a->pixels = pixels;
        a->width = area->width;
        a->height = area->height;
        a->last_track = -1;
        ass_set_frame_size(a->renderer, area->width, area->height);
    }
    if (area->font_scale != a->font_scale) {
        a->font_scale = area->font_scale;
        a->last_track = -1;
        ass_set_font_scale(a->renderer, area->font_scale);
    }
    if (area->lift_percent != a->lift_percent) {
        a->lift_percent = area->lift_percent;
        a->last_track = -1;
        ass_set_line_position(a->renderer, area->lift_percent);
    }
    return true;
}

static void compose(AssSubs *a, const ASS_Image *images)
{
    AssBox box = {0, 0, 0, 0};
    for (const ASS_Image *i = images; i; i = i->next)
        box = assBoxAdd(box, i->dst_x, i->dst_y, i->w, i->h, a->width, a->height);
    a->picture = (AssPicture){.x = box.x, .y = box.y, .pixels = a->pixels, .id = g_next_id++};
    if (box.w <= 0 || box.h <= 0)
        return;
    a->picture.w = box.w;
    a->picture.h = box.h;
    memset(a->pixels, 0, (size_t)box.w * (size_t)box.h * sizeof(*a->pixels));
    for (const ASS_Image *i = images; i; i = i->next) {
        AssLayer layer = {i->dst_x, i->dst_y, i->w, i->h, i->stride, i->bitmap, i->color};
        assComposeLayer(a->pixels, box, &layer);
    }
}

bool assSubsRender(AssSubs *a, int track, int64_t time_ms, const AssArea *area, AssPicture *out)
{
    memset(out, 0, sizeof(*out));
    pthread_mutex_lock(&a->lock);
    bool exists = track >= 0 && track < a->track_count;
    if (exists && area->width > 0 && area->height > 0 && prepareArea(a, area)) {
        int change = 0;
        ASS_Image *images = ass_render_frame(a->renderer, a->tracks[track], time_ms, &change);
        if (change || track != a->last_track)
            compose(a, images);
        a->last_track = track;
        *out = a->picture;
    }
    pthread_mutex_unlock(&a->lock);
    return exists;
}
