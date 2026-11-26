#include "dvr.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jansson.h>

#include "jellyfin.h"
#include "lang.h"

#define SECONDS_PER_MINUTE 60
#define TICKS_PER_SECOND 10000000  // The server's times are in 100 ns ticks.

static const char *textOf(const json_t *object, const char *key)
{
    const char *value = json_string_value(json_object_get(object, key));
    return value ? value : "";
}

static void copy(char *out, size_t size, const char *text)
{
    snprintf(out, size, "%s", text);
}

// An id that fits and is safe in an address (letters, digits, - and _), or "".
static void copyId(char *out, const char *id)
{
    bool ok = strlen(id) < DVR_ID_MAX;
    for (const char *c = id; ok && *c; c++)
        ok = isalnum((unsigned char)*c) || *c == '-' || *c == '_';
    copy(out, DVR_ID_MAX, ok ? id : "");
}

// The answer's items, or NULL if it can't be read (root to json_decref).
static const json_t *itemsOf(const char *json, json_t **root)
{
    *root = json ? json_loads(json, 0, NULL) : NULL;
    const json_t *items = json_object_get(*root, "Items");
    return json_is_array(items) ? items : NULL;
}

static int byStart(const void *pa, const void *pb)
{
    const DvrTimer *a = pa, *b = pb;
    return a->start < b->start ? -1 : a->start > b->start;
}

int dvrParseTimers(const char *json, DvrTimer *out, int max)
{
    json_t *root;
    const json_t *items = itemsOf(json, &root);
    if (!items) {
        json_decref(root);
        return -1;
    }
    int count = 0;
    size_t i;
    const json_t *item;
    json_array_foreach(items, i, item) {
        const char *status = textOf(item, "Status");
        if (count >= max || !textOf(item, "Id")[0] || !strcmp(status, "Cancelled") || !strcmp(status, "Completed"))
            continue;
        DvrTimer *t = &out[count++];
        memset(t, 0, sizeof(*t));
        copyId(t->id, textOf(item, "Id"));
        copyId(t->program_id, textOf(item, "ProgramId"));
        copyId(t->series_id, textOf(item, "SeriesTimerId"));
        copyId(t->channel_id, textOf(item, "ChannelId"));
        copy(t->name, sizeof(t->name), textOf(item, "Name"));
        copy(t->channel, sizeof(t->channel), textOf(item, "ChannelName"));
        t->start = jfParseDate(textOf(item, "StartDate"));
        t->end = jfParseDate(textOf(item, "EndDate"));
        t->state = !strcmp(status, "InProgress")        ? DVR_RECORDING
                   : !strcmp(status, "ConflictedNotOk") ? DVR_CONFLICT
                   : !strcmp(status, "Error")           ? DVR_FAILED
                                                        : DVR_SCHEDULED;
        if (!t->id[0])
            count--;  // An id too long to cancel it by.
    }
    json_decref(root);
    qsort(out, (size_t)count, sizeof(DvrTimer), byStart);
    return count;
}

static int byName(const void *pa, const void *pb)
{
    const DvrSeries *a = pa, *b = pb;
    return strcmp(a->name, b->name);
}

int dvrParseSeries(const char *json, DvrSeries *out, int max)
{
    json_t *root;
    const json_t *items = itemsOf(json, &root);
    if (!items) {
        json_decref(root);
        return -1;
    }
    int count = 0;
    size_t i;
    const json_t *item;
    json_array_foreach(items, i, item) {
        const char *id = textOf(item, "Id");
        if (count >= max || !id[0] || strlen(id) >= DVR_ID_MAX)
            continue;
        DvrSeries *s = &out[count++];
        memset(s, 0, sizeof(*s));
        copyId(s->id, id);
        if (!s->id[0]) {
            count--;
            continue;
        }
        copy(s->name, sizeof(s->name), textOf(item, "Name"));
        copy(s->channel, sizeof(s->channel), textOf(item, "ChannelName"));
        s->any_channel = json_is_true(json_object_get(item, "RecordAnyChannel"));
        s->new_only = json_is_true(json_object_get(item, "RecordNewOnly"));
    }
    json_decref(root);
    qsort(out, (size_t)count, sizeof(DvrSeries), byName);
    return count;
}

static int latestFirst(const void *pa, const void *pb)
{
    const DvrRecording *a = pa, *b = pb;
    return a->start > b->start ? -1 : a->start < b->start;
}

int dvrParseRecordings(const char *json, DvrRecording *out, int max)
{
    json_t *root;
    const json_t *items = itemsOf(json, &root);
    if (!items) {
        json_decref(root);
        return -1;
    }
    int count = 0;
    size_t i;
    const json_t *item;
    json_array_foreach(items, i, item) {
        const char *id = textOf(item, "Id");
        if (count >= max || !id[0] || strlen(id) >= DVR_ID_MAX)
            continue;
        DvrRecording *r = &out[count++];
        memset(r, 0, sizeof(*r));
        copyId(r->id, id);
        if (!r->id[0]) {
            count--;
            continue;
        }
        // Jellyfin 10.11 keeps a recorded episode as a library episode: its series' name, and the file's
        // as its own ("Show S01E425"); no channel or times, only when it was made and how long it is.
        const char *series = textOf(item, "SeriesName");
        copy(r->name, sizeof(r->name), series[0] ? series : textOf(item, "Name"));
        copy(r->episode, sizeof(r->episode), textOf(item, "EpisodeTitle"));
        copy(r->channel, sizeof(r->channel), textOf(item, "ChannelName"));
        r->start = jfParseDate(textOf(item, "StartDate"));
        r->end = jfParseDate(textOf(item, "EndDate"));
        if (!r->start)
            r->start = jfParseDate(textOf(item, "DateCreated"));
        json_int_t ticks = json_integer_value(json_object_get(item, "RunTimeTicks"));
        if (!r->end && r->start && ticks > 0)
            r->end = r->start + (int64_t)(ticks / TICKS_PER_SECOND);
        r->recording = !strcmp(textOf(item, "Status"), "InProgress");
        // Without the server saying, it may be deleted: the server refuses if not (and that's said).
        const json_t *can = json_object_get(item, "CanDelete");
        r->can_delete = can ? json_is_true(can) : true;
    }
    json_decref(root);
    qsort(out, (size_t)count, sizeof(DvrRecording), latestFirst);
    return count;
}

const DvrTimer *dvrTimerFor(const DvrLists *d, const char *program_id)
{
    for (int i = 0; program_id[0] && i < d->timer_count; i++)
        if (!strcmp(d->timers[i].program_id, program_id))
            return &d->timers[i];
    return NULL;
}

void dvrMarkGuide(LiveGuide *g, const DvrTimer *timers, int count)
{
    for (int i = 0; i < g->pool_count; i++) {
        LiveProgram *p = &g->pool[i];
        p->marks &= (unsigned char)~(LIVE_TIMER | LIVE_SERIES_TIMER);
        for (int j = 0; p->id[0] && j < count; j++)
            if (!strcmp(timers[j].program_id, p->id))
                p->marks |= (unsigned char)(LIVE_TIMER | (timers[j].series_id[0] ? LIVE_SERIES_TIMER : 0));
    }
}

void dvrKnow(DvrKnown *k, const char *program_id, bool scheduled, int64_t until)
{
    if (!program_id[0] || strlen(program_id) >= LIVE_PROGRAM_ID_MAX)
        return;
    int at = 0;  // Its own entry, or else the one that ends first (an empty one ends at 0).
    for (int i = 0; i < DVR_KNOWN_MAX; i++) {
        if (!strcmp(k->entries[i].program_id, program_id)) {
            at = i;
            break;
        }
        if (k->entries[i].until < k->entries[at].until)
            at = i;
    }
    copy(k->entries[at].program_id, sizeof(k->entries[at].program_id), program_id);
    k->entries[at].scheduled = scheduled;
    k->entries[at].until = until;
}

int dvrKnownOf(const DvrKnown *k, const char *program_id, int64_t now_s)
{
    for (int i = 0; program_id[0] && i < DVR_KNOWN_MAX; i++)
        if (k->entries[i].until > now_s && !strcmp(k->entries[i].program_id, program_id))
            return k->entries[i].scheduled;
    return -1;
}

void dvrMarkKnown(LiveGuide *g, const DvrKnown *k, int64_t now_s)
{
    for (int i = 0; i < g->pool_count; i++) {
        int known = dvrKnownOf(k, g->pool[i].id, now_s);
        if (known == 1)
            g->pool[i].marks |= LIVE_TIMER;
        else if (known == 0)
            g->pool[i].marks &= (unsigned char)~(LIVE_TIMER | LIVE_SERIES_TIMER);
    }
}

int dvrChoices(const LiveProgram *p, int64_t now_s, DvrAction out[DVR_DO_COUNT])
{
    int n = 0;
    if (!p || !p->id[0] || p->end <= now_s)
        return 0;
    out[n++] = (p->marks & LIVE_TIMER) ? DVR_DO_CANCEL : DVR_DO_RECORD;
    if (p->marks & LIVE_SERIES_TIMER)
        out[n++] = DVR_DO_CANCEL_SERIES;
    else if (p->marks & LIVE_IS_SERIES)
        out[n++] = DVR_DO_RECORD_SERIES;
    return n;
}

const char *dvrActionLabel(DvrAction a)
{
    switch (a) {
    case DVR_DO_RECORD: return T("Record this program");
    case DVR_DO_RECORD_SERIES: return T("Record the series");
    case DVR_DO_CANCEL: return T("Don't record this program");
    case DVR_DO_CANCEL_SERIES: return T("Stop recording the series");
    default: return "";
    }
}

bool dvrTimerBody(const char *defaults_json, const char *program_id, char *out, size_t size)
{
    json_t *root = defaults_json ? json_loads(defaults_json, 0, NULL) : NULL;
    bool ok = json_is_object(root) && program_id[0] && !strcmp(textOf(root, "ProgramId"), program_id);
    char *text = ok ? json_dumps(root, JSON_COMPACT) : NULL;
    ok = text && strlen(text) < size;
    if (ok)
        copy(out, size, text);
    free(text);
    json_decref(root);
    return ok;
}

void dvrRecordingLocation(const DvrRecording *r, char *out, size_t size)
{
    jfLocationMake(out, size, r->id, r->id);  // A recording has one version: its source has its id.
}

int dvrMinutes(int64_t start, int64_t end)
{
    return start && end > start ? (int)((end - start + SECONDS_PER_MINUTE / 2) / SECONDS_PER_MINUTE) : 0;
}
