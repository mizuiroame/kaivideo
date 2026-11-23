#include "iptvhome.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int clampInt(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

void iptvMarksInit(IptvMarks *m)
{
    m->recent_count = 0;
    m->favorite_count = 0;
}

static int findName(const char (*names)[IPTV_NAME_MAX], int count, const char *name)
{
    for (int i = 0; i < count; i++)
        if (!strcmp(names[i], name))
            return i;
    return -1;
}

// A name as kept: no tabs or line ends, which would break the file's lines.
static bool keepName(char *out, const char *name)
{
    snprintf(out, IPTV_NAME_MAX, "%s", name);
    for (char *p = out; *p; p++)
        if (*p == '\t' || *p == '\n' || *p == '\r')
            *p = ' ';
    return out[0] != '\0';
}

void iptvMarksRead(IptvMarks *m, const char *text)
{
    iptvMarksInit(m);
    const char *p = text;
    while (p && *p) {
        const char *end = strchr(p, '\n');
        size_t length = end ? (size_t)(end - p) : strlen(p);
        const char *tab = memchr(p, '\t', length);
        if (tab) {
            char name[IPTV_NAME_MAX];
            size_t name_length = length - (size_t)(tab + 1 - p);
            while (name_length > 0 && tab[name_length] == '\r')
                name_length--;
            snprintf(name, sizeof(name), "%.*s", (int)name_length, tab + 1);
            size_t key = (size_t)(tab - p);
            if (key == 6 && !strncmp(p, "recent", 6) && m->recent_count < IPTVHOME_RECENT_MAX && name[0] &&
                findName(m->recent, m->recent_count, name) < 0)
                keepName(m->recent[m->recent_count++], name);
            else if (key == 8 && !strncmp(p, "favorite", 8) && m->favorite_count < IPTVHOME_FAVORITES_MAX &&
                     name[0] && findName(m->favorites, m->favorite_count, name) < 0)
                keepName(m->favorites[m->favorite_count++], name);
        }
        p = end ? end + 1 : NULL;
    }
}

bool iptvMarksWrite(const IptvMarks *m, char *out, size_t size)
{
    size_t at = 0;
    if (size == 0)
        return false;
    out[0] = '\0';
    for (int i = 0; i < m->recent_count + m->favorite_count; i++) {
        bool recent = i < m->recent_count;
        const char *name = recent ? m->recent[i] : m->favorites[i - m->recent_count];
        int n = snprintf(out + at, size - at, "%s\t%s\n", recent ? "recent" : "favorite", name);
        if (n < 0 || (size_t)n >= size - at)
            return false;
        at += (size_t)n;
    }
    return true;
}

void iptvMarksWatched(IptvMarks *m, const char *name)
{
    char kept[IPTV_NAME_MAX];
    if (!keepName(kept, name))
        return;
    int at = findName(m->recent, m->recent_count, kept);
    if (at < 0)
        at = m->recent_count < IPTVHOME_RECENT_MAX ? m->recent_count++ : IPTVHOME_RECENT_MAX - 1;
    memmove(m->recent[1], m->recent[0], (size_t)at * IPTV_NAME_MAX);
    memcpy(m->recent[0], kept, IPTV_NAME_MAX);
}

bool iptvMarksIsFavorite(const IptvMarks *m, const char *name)
{
    char kept[IPTV_NAME_MAX];
    return keepName(kept, name) && findName(m->favorites, m->favorite_count, kept) >= 0;
}

bool iptvMarksToggleFavorite(IptvMarks *m, const char *name)
{
    char kept[IPTV_NAME_MAX];
    if (!keepName(kept, name))
        return false;
    int at = findName(m->favorites, m->favorite_count, kept);
    if (at >= 0) {
        memmove(m->favorites[at], m->favorites[at + 1], (size_t)(m->favorite_count - at - 1) * IPTV_NAME_MAX);
        m->favorite_count--;
        return false;
    }
    if (m->favorite_count >= IPTVHOME_FAVORITES_MAX)
        return false;
    memcpy(m->favorites[m->favorite_count++], kept, IPTV_NAME_MAX);
    return true;
}

// --- The rows ---

// The list's channel with that name (as kept), -1 if none.
static int channelNamed(const IptvList *list, const char *name)
{
    for (int i = 0; i < list->count; i++) {
        char kept[IPTV_NAME_MAX];
        if (keepName(kept, list->channels[i].name) && !strcmp(kept, name))
            return i;
    }
    return -1;
}

static void addNamed(IptvHome *h, int *used, const IptvList *list, IptvRowKind kind,
                     const char (*names)[IPTV_NAME_MAX], int count)
{
    IptvHomeRow *row = &h->rows[h->count];
    row->kind = kind;
    row->group = -1;
    row->first = *used;
    row->count = 0;
    for (int i = 0; i < count; i++) {
        int channel = channelNamed(list, names[i]);
        if (channel >= 0)
            h->pool[*used + row->count++] = channel;
    }
    *used += row->count;
    if (row->count > 0)
        h->count++;
}

static void addGroup(IptvHome *h, int *used, const IptvList *list, int group)
{
    IptvHomeRow *row = &h->rows[h->count];
    row->kind = IPTVROW_GROUP;
    row->group = group;
    row->first = *used;
    row->count = 0;
    for (int i = 0; i < list->count && row->count < IPTVHOME_ROW_MAX; i++)
        if (group < 0 || list->channels[i].group == group)
            h->pool[*used + row->count++] = i;
    *used += row->count;
    if (row->count > 0)
        h->count++;
}

void iptvHomeBuild(IptvHome *h, const IptvList *list, const IptvMarks *m)
{
    h->count = 0;
    free(h->pool);
    int groups = list->group_count < IPTV_GROUPS_MAX ? list->group_count : IPTV_GROUPS_MAX;
    size_t room = (size_t)IPTVHOME_RECENT_MAX + IPTVHOME_FAVORITES_MAX + (size_t)(groups + 1) * IPTVHOME_ROW_MAX;
    h->pool = list->count > 0 ? malloc(room * sizeof(int)) : NULL;
    if (!h->pool)
        return;
    int used = 0;
    addNamed(h, &used, list, IPTVROW_RECENT, m->recent, m->recent_count);
    addNamed(h, &used, list, IPTVROW_FAVORITES, m->favorites, m->favorite_count);
    for (int g = 0; g < groups; g++)
        addGroup(h, &used, list, g);
    if (groups == 0)
        addGroup(h, &used, list, -1);
}

void iptvHomeFree(IptvHome *h)
{
    free(h->pool);
    h->pool = NULL;
    h->count = 0;
}

int iptvHomeChannel(const IptvHome *h, int row, int col)
{
    if (row < 0 || row >= h->count || col < 0 || col >= h->rows[row].count)
        return -1;
    return h->pool[h->rows[row].first + col];
}

// --- The cursor ---

static void keepInView(Scroll *s, int count, int visible)
{
    s->offset = 0;
    s->cursor = count > 0 ? clampInt(s->cursor, 0, count - 1) : 0;
    if (s->cursor < s->first)
        s->first = s->cursor;
    if (s->cursor >= s->first + visible)
        s->first = s->cursor - visible + 1;
    s->first = clampInt(s->first, 0, count > visible ? count - visible : 0);
}

void iptvHomeViewReset(IptvHomeView *v)
{
    memset(v, 0, sizeof(*v));
}

void iptvHomeMove(IptvHomeView *v, const IptvHome *h, int dx, int dy, int rows, int cols)
{
    if (h->count == 0) {
        iptvHomeViewReset(v);
        return;
    }
    v->rows.cursor += dy;
    keepInView(&v->rows, h->count, rows);
    Scroll *c = &v->cols[v->rows.cursor];
    int offset = c->offset;
    c->cursor += dx;
    keepInView(c, h->rows[v->rows.cursor].count, cols);
    if (!dx)
        c->offset = offset;  // Moving between rows leaves a row where a finger left it.
}
