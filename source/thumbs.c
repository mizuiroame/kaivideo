#include "thumbs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "gfx.h"
#include "image_config.h"
#include "stb_image.h"

#define TABLE_SIZE 4096        // Videos known at once (a power of two).
#define LOADED_MAX 64          // Pictures kept in memory.
#define LOADS_PER_FRAME 2      // Reads from the card per frame, so scrolling stays smooth.
#define FILE_MAX (1u << 20)
#define LARGE_KEY_MARK 0x9E3779B97F4A7C15ull  // A large picture is another file.

typedef enum { THUMB_UNKNOWN, THUMB_ASKED, THUMB_ON_CARD, THUMB_NONE, THUMB_LOADED } ThumbState;

typedef struct {
    uint64_t key;  // 0: a free slot.
    uint8_t state;
    bool large;
    uint32_t *pixels;
    unsigned last_used;
} Entry;

static Entry g_table[TABLE_SIZE];
static char g_folder[512];
static unsigned g_frame, g_loaded, g_loads;
static Entry *g_large_loaded;

// FNV-1a over the path and size: a file replaced by another gets a new picture.
static uint64_t keyOf(const char *path, int64_t size)
{
    uint64_t h = 1469598103934665603ull;
    for (const unsigned char *p = (const unsigned char *)path; *p; p++)
        h = (h ^ *p) * 1099511628211ull;
    for (int i = 0; i < 8; i++)
        h = (h ^ (uint8_t)(size >> (i * 8))) * 1099511628211ull;
    return h ? h : 1;
}

static Entry *find(uint64_t key, bool add)
{
    for (unsigned i = 0, at = (unsigned)key & (TABLE_SIZE - 1); i < TABLE_SIZE; i++, at = (at + 1) & (TABLE_SIZE - 1)) {
        if (g_table[at].key == key)
            return &g_table[at];
        if (g_table[at].key == 0) {
            if (!add)
                return NULL;
            g_table[at] = (Entry){.key = key, .state = THUMB_UNKNOWN};
            return &g_table[at];
        }
    }
    return NULL;  // Full: this video shows no picture.
}

static void jpgPath(uint64_t key, char *out, size_t size)
{
    snprintf(out, size, "%s/%016llx.jpg", g_folder, (unsigned long long)key);
}

void thumbsInit(const char *folder)
{
    snprintf(g_folder, sizeof(g_folder), "%s", folder);
    mkdir(g_folder, 0777);
    thumbgenStart();
}

static void unload(Entry *e)
{
    gfxImageChanged(e->pixels);  // The GPU drops its copy.
    free(e->pixels);
    e->pixels = NULL;
    e->state = THUMB_ON_CARD;
    g_loaded--;
    if (e == g_large_loaded)
        g_large_loaded = NULL;
}

void thumbsExit(void)
{
    thumbgenStop();
    for (unsigned i = 0; i < TABLE_SIZE; i++)
        if (g_table[i].pixels)
            unload(&g_table[i]);
}

// The least recently drawn picture goes, to make room.
static void makeRoom(void)
{
    Entry *oldest = NULL;
    for (unsigned i = 0; i < TABLE_SIZE; i++)
        if (g_table[i].pixels && !g_table[i].large && (!oldest || g_table[i].last_used < oldest->last_used))
            oldest = &g_table[i];
    if (oldest)
        unload(oldest);
}

static bool load(Entry *e)
{
    char path[600];
    jpgPath(e->key, path, sizeof(path));
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    unsigned char *data = malloc(FILE_MAX);
    size_t size = data ? fread(data, 1, FILE_MAX, f) : 0;
    fclose(f);
    int w = 0, h = 0, channels = 0;
    unsigned char *rgba = size ? stbi_load_from_memory(data, (int)size, &w, &h, &channels, 4) : NULL;
    free(data);
    int want_w = e->large ? THUMB_LARGE_W : THUMB_W, want_h = e->large ? THUMB_LARGE_H : THUMB_H;
    if (!rgba || w != want_w || h != want_h) {
        if (rgba)
            stbi_image_free(rgba);
        return false;
    }
    if (e->large && g_large_loaded)
        unload(g_large_loaded);
    else if (!e->large && g_loaded >= LOADED_MAX)
        makeRoom();
    e->pixels = malloc((size_t)want_w * want_h * 4);
    if (e->pixels) {
        memcpy(e->pixels, rgba, (size_t)want_w * want_h * 4);  // stb's RGBA bytes are 0xAABBGGRR words.
        g_loaded++;
        if (e->large)
            g_large_loaded = e;
    }
    stbi_image_free(rgba);
    return e->pixels != NULL;
}

static const uint32_t *getPicture(const char *path, int64_t size, bool large)
{
    Entry *e = find(keyOf(path, size) ^ (large ? LARGE_KEY_MARK : 0), true);
    if (!e)
        return NULL;
    e->large = large;
    e->last_used = g_frame;
    if (e->state == THUMB_UNKNOWN) {  // Made before, or never tried?
        char jpg[600];
        jpgPath(e->key, jpg, sizeof(jpg));
        struct stat st;
        ThumbFileKind kind = stat(jpg, &st) == 0 ? thumbgenFileKind(st.st_size) : THUMB_FILE_RETRY;
        if (kind == THUMB_FILE_PICTURE) {
            e->state = THUMB_ON_CARD;
        } else if (kind == THUMB_FILE_NONE) {
            e->state = THUMB_NONE;
        } else {
            e->state = THUMB_ASKED;
            thumbgenRequest(path, jpg, e->key, large);
        }
    } else if (e->state == THUMB_ASKED) {
        char jpg[600];
        jpgPath(e->key, jpg, sizeof(jpg));
        thumbgenRequest(path, jpg, e->key, large);  // Still wanted: to the top of the queue.
    }
    if (e->state == THUMB_ON_CARD && g_loads < LOADS_PER_FRAME) {
        g_loads++;
        e->state = load(e) ? THUMB_LOADED : THUMB_NONE;
    }
    return e->state == THUMB_LOADED ? e->pixels : NULL;
}

const uint32_t *thumbGet(const char *path, int64_t size)
{
    return getPicture(path, size, false);
}

const uint32_t *thumbGetLarge(const char *path, int64_t size)
{
    return getPicture(path, size, true);
}

bool thumbIsNone(const char *path, int64_t size)
{
    Entry *e = find(keyOf(path, size), false);
    return e && e->state == THUMB_NONE;
}

void thumbsRetryMissing(void)
{
    for (unsigned i = 0; i < TABLE_SIZE; i++)
        if (g_table[i].key && g_table[i].state == THUMB_NONE)
            g_table[i].state = THUMB_UNKNOWN;  // A kept mark sends it back to none.
}

void thumbsUpdate(void)
{
    g_frame++;
    g_loads = 0;
    thumbgenNextFrame();
    uint64_t key;
    while (thumbgenTakeDone(&key)) {
        Entry *e = find(key, false);
        if (e && e->state == THUMB_ASKED)
            e->state = THUMB_ON_CARD;  // Read when next drawn (the mark reads as none).
    }
}

void thumbsPause(bool paused)
{
    thumbgenPause(paused);
}
