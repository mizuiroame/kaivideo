#include "livetv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jansson.h>

#include "jellyfin.h"
#include "lang.h"

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
    free(g->pool);
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
    liveGuideFree(g);
    g->channels = channels;
    g->count = g->room = count;
    return count;
}

static int findChannel(const LiveGuide *g, const char *id)
{
    for (int i = 0; i < g->count; i++)
        if (!strcmp(g->channels[i].id, id))
            return i;
    return -1;
}

// A program read, with its channel's place in the guide.
typedef struct {
    int channel;
    LiveProgram p;
} Read;

static int byChannelThenStart(const void *pa, const void *pb)
{
    const Read *a = pa, *b = pb;
    if (a->channel != b->channel)
        return a->channel < b->channel ? -1 : 1;
    return a->p.start < b->p.start ? -1 : a->p.start > b->p.start;
}

// The programs of an answer for the guide's channels, in *out (free it): how many.
static int readPrograms(const LiveGuide *g, const char *json, Read **out)
{
    json_t *root = json_loads(json, 0, NULL);
    const json_t *items = json_object_get(root, "Items");
    size_t total = json_array_size(items);
    Read *reads = total ? calloc(total, sizeof(Read)) : NULL;
    int count = 0;
    size_t i;
    const json_t *item;
    json_array_foreach(items, i, item) {
        int channel = reads ? findChannel(g, textOf(item, "ChannelId")) : -1;
        if (channel < 0)
            continue;
        LiveProgram p = {.start = jfParseDate(textOf(item, "StartDate")), .end = jfParseDate(textOf(item, "EndDate"))};
        snprintf(p.title, sizeof(p.title), "%s", textOf(item, "Name"));
        const char *id = textOf(item, "Id");
        if (strlen(id) < sizeof(p.id))
            snprintf(p.id, sizeof(p.id), "%s", id);
        p.marks = (unsigned char)((textOf(item, "TimerId")[0] ? LIVE_TIMER : 0) |
                                  (textOf(item, "SeriesTimerId")[0] ? LIVE_SERIES_TIMER : 0) |
                                  (json_is_true(json_object_get(item, "IsSeries")) ? LIVE_IS_SERIES : 0));
        if (p.title[0] && p.end > p.start)
            reads[count++] = (Read){channel, p};
    }
    json_decref(root);
    *out = reads;
    return count;
}

// Keeps p as the channel's program now or next, if it's that.
static void placeNow(LiveChannel *c, const LiveProgram *p, int64_t now_s)
{
    if (p->end <= now_s)
        return;  // Over.
    if (p->start <= now_s)
        c->now = *p;
    else if (!c->next.title[0] || p->start < c->next.start)
        c->next = *p;
}

void liveGuideSetPrograms(LiveGuide *g, const char *json, int64_t now_s, int64_t from, int64_t to)
{
    for (int i = 0; i < g->count; i++) {
        g->channels[i].now = (LiveProgram){0};
        g->channels[i].next = (LiveProgram){0};
        g->channels[i].programs = NULL;
        g->channels[i].program_count = 0;
    }
    free(g->pool);
    g->pool = NULL;
    g->pool_count = g->pool_room = 0;
    g->from = from;
    g->to = to;
    Read *reads;
    int count = readPrograms(g, json, &reads);
    if (count > 1)
        qsort(reads, (size_t)count, sizeof(Read), byChannelThenStart);
    g->pool = count > 0 ? calloc((size_t)count, sizeof(LiveProgram)) : NULL;
    g->pool_room = g->pool ? count : 0;
    for (int i = 0; i < count; i++) {
        LiveChannel *c = &g->channels[reads[i].channel];
        const LiveProgram *p = &reads[i].p;
        placeNow(c, p, now_s);
        bool in_hours = p->end > now_s && (!to || (p->end > from && p->start < to));  // Not over.
        bool again = c->program_count > 0 && c->programs[c->program_count - 1].start == p->start;
        if (!g->pool || !in_hours || again || c->program_count >= LIVE_PROGRAMS_PER_CHANNEL)
            continue;
        if (!c->programs)
            c->programs = &g->pool[g->pool_count];
        g->pool[g->pool_count++] = *p;
        c->program_count++;
    }
    free(reads);
}

void liveGuideSetNow(LiveGuide *g, const char *json, int64_t now_s)
{
    Read *reads;
    int count = readPrograms(g, json, &reads);
    for (int i = 0; i < count; i++)
        if (reads[i].p.start <= now_s && reads[i].p.end > now_s)
            g->channels[reads[i].channel].now = reads[i].p;
    free(reads);
}

bool liveGuideReserve(LiveGuide *g, int channels, int programs)
{
    liveGuideFree(g);
    g->channels = channels > 0 ? calloc((size_t)channels, sizeof(LiveChannel)) : NULL;
    g->pool = programs > 0 ? calloc((size_t)programs, sizeof(LiveProgram)) : NULL;
    if ((channels > 0 && !g->channels) || (programs > 0 && !g->pool)) {
        liveGuideFree(g);
        return false;
    }
    g->room = channels;
    g->pool_room = programs;
    return true;
}

bool liveGuideAdd(LiveGuide *g, const LiveChannel *c, const LiveProgram *programs, int count)
{
    if (g->count >= g->room || count < 0 || count > g->pool_room - g->pool_count)
        return false;
    LiveChannel *to = &g->channels[g->count++];
    *to = *c;
    to->programs = count ? &g->pool[g->pool_count] : NULL;
    to->program_count = count;
    if (count)
        memcpy(to->programs, programs, (size_t)count * sizeof(LiveProgram));
    g->pool_count += count;
    return true;
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
        snprintf(out, size, "%d:%02d %s", hour % NOON ? hour % NOON : NOON, minute, hour < NOON ? T("AM") : T("PM"));
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

// The cursor at t: 0 when it's now or before, and no further than a day ahead.
static int64_t cursorAt(int64_t t, int64_t now_s)
{
    int64_t last = now_s + LIVE_GUIDE_DAY_S;
    t = t < last ? t : last;
    return t > now_s ? t : 0;
}

int64_t liveStep(const LiveChannel *c, int64_t at, int64_t now_s, int dx)
{
    int64_t t = at > now_s ? at : now_s;
    int here = liveProgramAt(c, t);
    bool on = here >= 0 && c->programs[here].start <= t;  // A program is on at t (not a gap before one).
    if (dx > 0) {
        if (on && here + 1 < c->program_count)
            return cursorAt(c->programs[here + 1].start, now_s);
        if (here >= 0 && !on)
            return cursorAt(c->programs[here].start, now_s);
        // Past the programs known: their hours are read as the view gets there.
        int64_t to = on ? c->programs[here].end : t + LIVE_GRID_STEP_S;
        return to <= now_s + LIVE_GUIDE_DAY_S ? cursorAt(to, now_s) : cursorAt(t, now_s);
    }
    if (on && c->programs[here].start < t && c->programs[here].start > now_s)  // Mid-program (a finger left it): its start.
        return cursorAt(c->programs[here].start, now_s);
    int before = here >= 0 ? here - 1 : c->program_count - 1;  // The program before the one at t.
    if (before >= 0 && c->programs[before].end > t - LIVE_GRID_STEP_S)
        return cursorAt(c->programs[before].start, now_s);
    return cursorAt(t - LIVE_GRID_STEP_S, now_s);
}

int64_t liveShift(int64_t at, int64_t now_s, int64_t delta_s)
{
    return cursorAt((at > now_s ? at : now_s) + delta_s, now_s);
}

bool liveWindowFor(const LiveGuide *g, int64_t now_s, int64_t view_from, int64_t *from, int64_t *to)
{
    int64_t last = now_s + LIVE_GUIDE_DAY_S;
    int64_t view_to = view_from + LIVE_GRID_SPAN_S < last ? view_from + LIVE_GRID_SPAN_S : last;
    view_from = view_from > now_s ? view_from : now_s;
    if (g->to && g->from <= view_from && g->to >= view_to)
        return false;
    *from = view_from - LIVE_WINDOW_BEFORE_S > now_s ? view_from - LIVE_WINDOW_BEFORE_S : now_s;
    *to = *from + LIVE_WINDOW_S > view_to ? *from + LIVE_WINDOW_S : view_to;
    return true;
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
