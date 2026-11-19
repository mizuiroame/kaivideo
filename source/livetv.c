#include "livetv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jansson.h>

#include "jellyfin.h"

#define SECONDS_PER_DAY 86400
#define SECONDS_PER_HOUR 3600
#define SECONDS_PER_MINUTE 60
#define NOON 12

static const char *textOf(const json_t *object, const char *key)
{
    const char *value = json_string_value(json_object_get(object, key));
    return value ? value : "";
}

void liveGuideInit(LiveGuide *g)
{
    memset(g, 0, sizeof(*g));
}

void liveGuideFree(LiveGuide *g)
{
    free(g->channels);
    liveGuideInit(g);
}

// By number as a number ("2" before "10"), then by name.
static int compareChannels(const void *pa, const void *pb)
{
    const LiveChannel *a = pa, *b = pb;
    double na = atof(a->number), nb = atof(b->number);
    if (na != nb)
        return na < nb ? -1 : 1;
    return strcmp(a->name, b->name);
}

int liveGuideSetChannels(LiveGuide *g, const char *json)
{
    json_t *root = json_loads(json, 0, NULL);
    const json_t *items = json_object_get(root, "Items");
    if (!json_is_array(items)) {
        json_decref(root);
        return -1;
    }
    size_t total = json_array_size(items);
    LiveChannel *channels = calloc(total ? total : 1, sizeof(LiveChannel));
    int count = 0;
    size_t i;
    const json_t *item;
    json_array_foreach(items, i, item) {
        const char *id = textOf(item, "Id");
        if (!channels || count >= LIVE_CHANNELS_MAX || !id[0] || strlen(id) >= LIVE_ID_MAX)
            continue;
        LiveChannel *c = &channels[count++];
        snprintf(c->id, sizeof(c->id), "%s", id);
        snprintf(c->name, sizeof(c->name), "%s", textOf(item, "Name"));
        snprintf(c->number, sizeof(c->number), "%s", textOf(item, "ChannelNumber"));
    }
    json_decref(root);
    if (!channels)
        return -1;
    qsort(channels, (size_t)count, sizeof(LiveChannel), compareChannels);
    free(g->channels);
    g->channels = channels;
    g->count = count;
    return count;
}

static LiveChannel *findChannel(LiveGuide *g, const char *id)
{
    for (int i = 0; i < g->count; i++)
        if (!strcmp(g->channels[i].id, id))
            return &g->channels[i];
    return NULL;
}

// Adds p to the channel's programs by its start: the same start twice is kept
// once, and with no room the latest goes.
static void addProgram(LiveChannel *c, const LiveProgram *p)
{
    int at = c->program_count;
    while (at > 0 && c->programs[at - 1].start > p->start)
        at--;
    if ((at > 0 && c->programs[at - 1].start == p->start) || at >= LIVE_PROGRAMS_PER_CHANNEL)
        return;
    int keep = c->program_count < LIVE_PROGRAMS_PER_CHANNEL ? c->program_count : LIVE_PROGRAMS_PER_CHANNEL - 1;
    memmove(&c->programs[at + 1], &c->programs[at], (size_t)(keep - at) * sizeof(LiveProgram));
    c->programs[at] = *p;
    c->program_count = keep + 1;
}

// Keeps p as the channel's program now or next, if it's that, and in its list.
static void place(LiveChannel *c, const LiveProgram *p, int64_t now_s)
{
    if (p->end <= now_s)
        return;  // Over.
    addProgram(c, p);
    if (p->start <= now_s) {
        c->now = *p;
    } else if (!c->next.title[0] || p->start < c->next.start) {
        c->next = *p;
    }
}

void liveGuideSetPrograms(LiveGuide *g, const char *json, int64_t now_s)
{
    for (int i = 0; i < g->count; i++) {
        g->channels[i].now = (LiveProgram){0};
        g->channels[i].next = (LiveProgram){0};
        g->channels[i].program_count = 0;
    }
    json_t *root = json_loads(json, 0, NULL);
    size_t i;
    const json_t *item;
    json_array_foreach(json_object_get(root, "Items"), i, item) {
        LiveChannel *c = findChannel(g, textOf(item, "ChannelId"));
        if (!c)
            continue;
        LiveProgram p = {.start = jfParseDate(textOf(item, "StartDate")), .end = jfParseDate(textOf(item, "EndDate"))};
        snprintf(p.title, sizeof(p.title), "%s", textOf(item, "Name"));
        if (p.title[0] && p.end > p.start)
            place(c, &p, now_s);
    }
    json_decref(root);
}

double liveProgress(const LiveProgram *p, int64_t now_s)
{
    if (!p->title[0] || p->end <= p->start)
        return -1;
    double f = (double)(now_s - p->start) / (double)(p->end - p->start);
    return f < 0 ? 0 : f > 1 ? 1 : f;
}

void liveClock(int64_t utc_s, int64_t offset_s, bool clock_24h, char *out, size_t size)
{
    int64_t local = utc_s + offset_s, of_day = ((local % SECONDS_PER_DAY) + SECONDS_PER_DAY) % SECONDS_PER_DAY;
    int hour = (int)(of_day / SECONDS_PER_HOUR), minute = (int)(of_day % SECONDS_PER_HOUR / SECONDS_PER_MINUTE);
    if (clock_24h)
        snprintf(out, size, "%d:%02d", hour, minute);
    else
        snprintf(out, size, "%d:%02d %s", hour % NOON ? hour % NOON : NOON, minute, hour < NOON ? "AM" : "PM");
}

void liveTimeRange(const LiveProgram *p, int64_t utc_offset_s, bool clock_24h, char *out, size_t size)
{
    char from[16], to[16];
    liveClock(p->start, utc_offset_s, clock_24h, from, sizeof(from));
    liveClock(p->end, utc_offset_s, clock_24h, to, sizeof(to));
    snprintf(out, size, "%s - %s", from, to);
}

int liveProgramAt(const LiveChannel *c, int64_t t)
{
    for (int i = 0; i < c->program_count; i++)
        if (c->programs[i].end > t)
            return i;
    return -1;
}

int64_t liveStep(const LiveChannel *c, int64_t at, int64_t now_s, int dx)
{
    int here = liveProgramAt(c, at > now_s ? at : now_s);
    int to = here + (dx > 0 ? 1 : -1);
    if (here < 0 || to < 0 || to >= c->program_count)
        return at > now_s ? at : 0;
    return c->programs[to].start > now_s ? c->programs[to].start : 0;
}

// Floors to a multiple of step, for times before 1970 too.
static int64_t floorTo(int64_t t, int64_t step)
{
    int64_t r = t % step;
    return r < 0 ? t - r - step : t - r;
}

int64_t liveGridStart(int64_t now_s, int64_t at, int64_t utc_offset_s)
{
    int64_t base = floorTo(now_s + utc_offset_s, LIVE_GRID_STEP_S) - utc_offset_s;
    if (at <= now_s)
        return base;
    // The cursor goes no further right than the column before the last, so
    // what comes after it shows too.
    int64_t columns = LIVE_GRID_SPAN_S / LIVE_GRID_STEP_S;
    int64_t past = (at - base) / LIVE_GRID_STEP_S - (columns - 2);
    return base + (past > 0 ? past : 0) * LIVE_GRID_STEP_S;
}

bool liveGridSpan(const LiveProgram *p, int64_t from, int x0, int width, int *x, int *w)
{
    int64_t to = from + LIVE_GRID_SPAN_S;
    int64_t s = p->start > from ? p->start : from, e = p->end < to ? p->end : to;
    if (e <= s)
        return false;
    *x = x0 + (int)((s - from) * width / LIVE_GRID_SPAN_S);
    *w = x0 + (int)((e - from) * width / LIVE_GRID_SPAN_S) - *x;
    return true;
}

void liveLocation(char *out, size_t size, const char *channel_id)
{
    snprintf(out, size, JF_PREFIX "%s/" LIVE_SOURCE, channel_id);
}

bool liveIsLocation(const char *location)
{
    size_t prefix = sizeof(JF_PREFIX) - 1, n = strlen(location), tail = sizeof("/" LIVE_SOURCE) - 1;
    return n > prefix + tail && !strncmp(location, JF_PREFIX, prefix) && !strcmp(location + n - tail, "/" LIVE_SOURCE);
}
