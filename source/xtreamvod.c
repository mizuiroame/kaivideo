#include "xtreamvod.h"

#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <jansson.h>

#define POSTER_MAX (IPTV_URL_MAX + 8)

// --- JSON ---

static long long jsonNumber(const json_t *object, const char *key)
{
    const json_t *v = json_object_get(object, key);
    if (json_is_integer(v))
        return (long long)json_integer_value(v);
    if (json_is_real(v))
        return (long long)json_real_value(v);
    if (json_is_string(v))
        return atoll(json_string_value(v));
    return 0;
}

// A string member trimmed, as a copy; NULL if it's missing or empty.
static char *jsonText(const json_t *object, const char *key, size_t max)
{
    const json_t *v = json_object_get(object, key);
    if (!json_is_string(v))
        return NULL;
    const char *s = json_string_value(v);
    size_t length = strlen(s);
    while (length > 0 && isspace((unsigned char)*s)) {
        s++;
        length--;
    }
    while (length > 0 && isspace((unsigned char)s[length - 1]))
        length--;
    if (length == 0)
        return NULL;
    if (length > max)
        length = max;
    char *out = malloc(length + 1);
    if (out) {
        memcpy(out, s, length);
        out[length] = '\0';
    }
    return out;
}

// A picture's address: only http(s), else NULL.
static char *jsonPicture(const json_t *object, const char *key)
{
    char *url = jsonText(object, key, IPTV_URL_MAX - 1);
    if (url && strncasecmp(url, "http://", 7) && strncasecmp(url, "https://", 8)) {
        free(url);
        url = NULL;
    }
    return url;
}

// backdrop_path: a list of addresses (or one), the first http(s) one; else NULL.
static char *jsonBackdrop(const json_t *object)
{
    const json_t *v = json_object_get(object, "backdrop_path");
    if (json_is_string(v))
        return jsonPicture(object, "backdrop_path");
    for (size_t i = 0; json_is_array(v) && i < json_array_size(v); i++) {
        json_t *holder = json_pack("{sO}", "u", json_array_get(v, i));
        char *url = holder ? jsonPicture(holder, "u") : NULL;
        json_decref(holder);
        if (url)
            return url;
    }
    return NULL;
}

// A year from a date ("2024-03-01"), 0 if it doesn't start with one.
static int jsonYear(const json_t *object, const char *key)
{
    const json_t *v = json_object_get(object, key);
    const char *s = json_is_string(v) ? json_string_value(v) : "";
    int year = 0, digits = 0;
    while (digits < 4 && s[digits] >= '0' && s[digits] <= '9')
        year = year * 10 + (s[digits++] - '0');
    return digits == 4 && year >= 1880 && year <= 2100 ? year : 0;
}

// An extension: letters and digits only, else "mp4".
static void jsonExtension(const json_t *object, const char *key, char out[8])
{
    char *ext = jsonText(object, key, 7);
    bool ok = ext != NULL;
    for (const char *p = ext; ok && *p; p++)
        ok = isalnum((unsigned char)*p);
    snprintf(out, 8, "%s", ok ? ext : "mp4");
    free(ext);
}

// "Title (2019)": the year out of the name.
static int takeYear(char *name)
{
    size_t n = strlen(name);
    if (n < 7 || name[n - 1] != ')' || name[n - 6] != '(')
        return 0;
    for (size_t i = n - 5; i < n - 1; i++)
        if (!isdigit((unsigned char)name[i]))
            return 0;
    int year = atoi(name + n - 5);
    if (year < 1880 || year > 2100)
        return 0;
    size_t end = n - 6;
    while (end > 0 && name[end - 1] == ' ')
        end--;
    if (end == 0)
        return 0;  // Nothing but the year: kept as the name.
    name[end] = '\0';
    return year;
}

// --- The catalog ---

static void freeFilms(XtreamCatalog *c)
{
    for (int i = 0; i < c->film_count; i++) {
        free(c->films[i].name);
        free(c->films[i].poster);
        xtreamFilmInfoFree(&c->films[i].info);
    }
    free(c->films);
    c->films = NULL;
    c->film_count = 0;
}

void xtreamEpisodesFree(XtreamEpisode *episodes, int count)
{
    for (int i = 0; i < count; i++) {
        free(episodes[i].title);
        free(episodes[i].plot);
    }
    free(episodes);
}

static void freeShows(XtreamCatalog *c)
{
    for (int i = 0; i < c->show_count; i++) {
        XtreamShow *s = &c->shows[i];
        free(s->name);
        free(s->cover);
        free(s->plot);
        free(s->genre);
        free(s->backdrop);
        xtreamEpisodesFree(s->episodes, s->episode_count);
    }
    free(c->shows);
    c->shows = NULL;
    c->show_count = 0;
}

void xtreamCatalogFree(XtreamCatalog *c)
{
    freeFilms(c);
    freeShows(c);
    free(c->film_groups);
    free(c->show_groups);
    c->film_groups = c->show_groups = NULL;
    c->film_group_count = c->show_group_count = 0;
}

// --- Categories ---

// A string or a whole number, as text ("" if neither).
static void jsonIdOf(const json_t *v, char *out, size_t size)
{
    if (json_is_string(v))
        snprintf(out, size, "%s", json_string_value(v));
    else if (json_is_integer(v))
        snprintf(out, size, "%lld", (long long)json_integer_value(v));
    else
        out[0] = '\0';
}

static void jsonId(const json_t *object, const char *key, char *out, size_t size)
{
    jsonIdOf(json_object_get(object, key), out, size);
}

bool xtreamParseVodCategories(const char *json, XtreamCatalog *c, bool shows)
{
    json_t *root = json ? json_loads(json, 0, NULL) : NULL;
    if (!json_is_array(root)) {
        json_decref(root);
        return false;
    }
    IptvGroup **groups = shows ? &c->show_groups : &c->film_groups;
    int *count = shows ? &c->show_group_count : &c->film_group_count;
    free(*groups);
    *count = 0;
    size_t total = json_array_size(root) < IPTV_GROUPS_MAX ? json_array_size(root) : IPTV_GROUPS_MAX;
    *groups = total ? calloc(total, sizeof(IptvGroup)) : NULL;
    for (size_t i = 0; *groups && i < json_array_size(root) && *count < (int)total; i++) {
        const json_t *item = json_array_get(root, i);
        IptvGroup *g = &(*groups)[*count];
        jsonId(item, "category_id", g->id, sizeof(g->id));
        char *name = jsonText(item, "category_name", IPTV_GROUP_MAX - 1);
        if (g->id[0] && name) {
            snprintf(g->name, sizeof(g->name), "%s", name);
            g->count = 0;
            (*count)++;
        } else {
            memset(g, 0, sizeof(*g));
        }
        free(name);
    }
    json_decref(root);
    return true;
}

// The category an item names (category_id, or the first of category_ids), counted: -1 if none of groups.
static int groupOf(const json_t *item, IptvGroup *groups, int count)
{
    char id[IPTV_ID_MAX];
    jsonId(item, "category_id", id, sizeof(id));
    if (!id[0])
        jsonIdOf(json_array_get(json_object_get(item, "category_ids"), 0), id, sizeof(id));
    for (int g = 0; id[0] && g < count; g++)
        if (!strcmp(groups[g].id, id)) {
            groups[g].count++;
            return g;
        }
    return -1;
}

// Drops the categories nothing is in, so that every one offered shows something. moved: each old
// index's new one.
static void dropEmptyGroups(IptvGroup *groups, int *count, int moved[IPTV_GROUPS_MAX])
{
    int kept = 0;
    for (int g = 0; g < *count; g++) {
        moved[g] = groups[g].count > 0 ? kept : -1;
        if (groups[g].count > 0)
            groups[kept++] = groups[g];
    }
    *count = kept;
}

bool xtreamParseFilms(const char *json, XtreamCatalog *c)
{
    json_t *root = json ? json_loads(json, 0, NULL) : NULL;
    if (!json_is_array(root)) {
        json_decref(root);
        return false;
    }
    freeFilms(c);
    for (int g = 0; g < c->film_group_count; g++)
        c->film_groups[g].count = 0;
    size_t total = json_array_size(root);
    total = total < XTREAM_FILMS_MAX ? total : XTREAM_FILMS_MAX;
    c->films = total ? calloc(total, sizeof(XtreamFilm)) : NULL;
    for (size_t i = 0; c->films && i < json_array_size(root) && c->film_count < (int)total; i++) {
        const json_t *item = json_array_get(root, i);
        long long id = jsonNumber(item, "stream_id");
        char *name = jsonText(item, "name", IPTV_NAME_MAX - 1);
        if (id <= 0 || !name) {
            free(name);
            continue;
        }
        XtreamFilm *f = &c->films[c->film_count++];
        f->id = id;
        f->name = name;
        f->year = takeYear(name);
        jsonExtension(item, "container_extension", f->ext);
        f->poster = jsonPicture(item, "stream_icon");
        f->added = jsonNumber(item, "added");
        f->group = groupOf(item, c->film_groups, c->film_group_count);
    }
    json_decref(root);
    int moved[IPTV_GROUPS_MAX];
    dropEmptyGroups(c->film_groups, &c->film_group_count, moved);
    for (int i = 0; i < c->film_count; i++)
        c->films[i].group = c->films[i].group >= 0 ? moved[c->films[i].group] : -1;
    return true;
}

bool xtreamParseShows(const char *json, XtreamCatalog *c)
{
    json_t *root = json ? json_loads(json, 0, NULL) : NULL;
    if (!json_is_array(root)) {
        json_decref(root);
        return false;
    }
    freeShows(c);
    for (int g = 0; g < c->show_group_count; g++)
        c->show_groups[g].count = 0;
    size_t total = json_array_size(root);
    total = total < XTREAM_SHOWS_MAX ? total : XTREAM_SHOWS_MAX;
    c->shows = total ? calloc(total, sizeof(XtreamShow)) : NULL;
    for (size_t i = 0; c->shows && i < json_array_size(root) && c->show_count < (int)total; i++) {
        const json_t *item = json_array_get(root, i);
        long long id = jsonNumber(item, "series_id");
        char *name = jsonText(item, "name", IPTV_NAME_MAX - 1);
        if (id <= 0 || !name) {
            free(name);
            continue;
        }
        XtreamShow *s = &c->shows[c->show_count++];
        s->id = id;
        s->name = name;
        s->cover = jsonPicture(item, "cover");
        s->plot = jsonText(item, "plot", 2000);
        s->genre = jsonText(item, "genre", 200);
        s->backdrop = jsonBackdrop(item);
        s->year = jsonYear(item, "releaseDate");
        if (!s->year)
            s->year = jsonYear(item, "release_date");
        s->added = jsonNumber(item, "last_modified");
        s->group = groupOf(item, c->show_groups, c->show_group_count);
    }
    json_decref(root);
    int moved[IPTV_GROUPS_MAX];
    dropEmptyGroups(c->show_groups, &c->show_group_count, moved);
    for (int i = 0; i < c->show_count; i++)
        c->shows[i].group = c->shows[i].group >= 0 ? moved[c->shows[i].group] : -1;
    return true;
}

static int byEpisode(const void *a, const void *b)
{
    const XtreamEpisode *x = a, *y = b;
    if (x->season != y->season)
        return x->season < y->season ? -1 : 1;
    return x->episode < y->episode ? -1 : x->episode > y->episode;
}

static void addEpisodes(const json_t *list, int season_key, XtreamEpisode *out, int *count)
{
    for (size_t i = 0; json_is_array(list) && i < json_array_size(list) && *count < XTREAM_EPISODES_MAX; i++) {
        const json_t *item = json_array_get(list, i);
        long long id = jsonNumber(item, "id");
        if (id <= 0)
            continue;
        XtreamEpisode *e = &out[(*count)++];
        e->id = id;
        long long season = jsonNumber(item, "season");
        e->season = season > 0 && season < 1000 ? (int)season : season_key > 0 ? season_key : 1;
        long long number = jsonNumber(item, "episode_num");
        e->episode = number > 0 && number < 100000 ? (int)number : (int)i + 1;
        e->title = jsonText(item, "title", NAMING_MAX - 1);
        jsonExtension(item, "container_extension", e->ext);
        const json_t *info = json_object_get(item, "info");
        e->plot = jsonText(info, "plot", 2000);
        long long seconds = jsonNumber(info, "duration_secs");
        e->runtime_s = seconds > 0 && seconds < 100000 ? seconds : 0;
    }
}

bool xtreamParseEpisodes(const char *json, XtreamEpisode **out, int *count)
{
    *out = NULL;
    *count = 0;
    json_t *root = json ? json_loads(json, 0, NULL) : NULL;
    const json_t *episodes = json_object_get(root, "episodes");
    if (!json_is_object(episodes) && !json_is_array(episodes)) {
        json_decref(root);
        return false;
    }
    XtreamEpisode *list = calloc(XTREAM_EPISODES_MAX, sizeof(XtreamEpisode));
    if (!list) {
        json_decref(root);
        return false;
    }
    if (json_is_object(episodes)) {  // {"1": [...], "2": [...]}: by season.
        const char *key;
        const json_t *season;
        json_object_foreach((json_t *)episodes, key, season) addEpisodes(season, atoi(key), list, count);
    } else {  // Some servers: [[...], [...]].
        for (size_t s = 0; s < json_array_size(episodes); s++)
            addEpisodes(json_array_get(episodes, s), (int)s + 1, list, count);
    }
    json_decref(root);
    qsort(list, (size_t)*count, sizeof(*list), byEpisode);
    *out = list;
    return true;
}

// "01:30:00" or "90:00" (some servers send no duration_secs): seconds, 0 if it isn't one.
static int64_t clockSeconds(const char *text)
{
    int64_t total = 0;
    int parts = 0;
    for (const char *p = text; p && *p; parts++) {
        if (!isdigit((unsigned char)*p) || parts >= 3)
            return 0;
        char *end;
        long part = strtol(p, &end, 10);
        total = total * 60 + part;
        if (*end == ':')
            end++;
        else if (*end)
            return 0;
        p = end;
    }
    return parts >= 2 ? total : 0;
}

bool xtreamParseFilmInfo(const char *json, XtreamFilmInfo *out)
{
    memset(out, 0, sizeof(*out));
    json_t *root = json ? json_loads(json, 0, NULL) : NULL;
    const json_t *info = json_object_get(root, "info");
    bool ok = json_is_object(info) || (json_is_array(info) && json_is_object(json_object_get(root, "movie_data")));
    if (json_is_object(info)) {
        out->plot = jsonText(info, "plot", 2000);
        if (!out->plot)
            out->plot = jsonText(info, "description", 2000);
        out->genre = jsonText(info, "genre", 200);
        out->backdrop = jsonBackdrop(info);
        long long seconds = jsonNumber(info, "duration_secs");
        if (seconds <= 0) {
            const json_t *clock = json_object_get(info, "duration");
            seconds = json_is_string(clock) ? clockSeconds(json_string_value(clock)) : 0;
        }
        out->runtime_s = seconds > 0 && seconds < 100000 ? seconds : 0;
    }
    json_decref(root);
    return ok;
}

void xtreamFilmInfoFree(XtreamFilmInfo *info)
{
    free(info->plot);
    free(info->genre);
    free(info->backdrop);
    memset(info, 0, sizeof(*info));
}

XtreamFilm *xtreamFindFilm(XtreamCatalog *c, long long film_id)
{
    for (int i = 0; i < c->film_count; i++)
        if (c->films[i].id == film_id)
            return &c->films[i];
    return NULL;
}

void xtreamFilmSetInfo(XtreamCatalog *c, long long film_id, XtreamFilmInfo *info)
{
    XtreamFilm *f = xtreamFindFilm(c, film_id);
    if (!f) {
        if (info)
            xtreamFilmInfoFree(info);
        return;
    }
    xtreamFilmInfoFree(&f->info);
    if (info) {
        f->info = *info;
        memset(info, 0, sizeof(*info));
    }
    f->info_state = info ? XTREAM_INFO_READ : XTREAM_INFO_FAILED;
}

void xtreamCatalogKeepInfo(XtreamCatalog *to, XtreamCatalog *from)
{
    for (int i = 0; i < from->film_count; i++) {  // Few were read: one look through to for each.
        XtreamFilm *old = &from->films[i];
        if (old->info_state != XTREAM_INFO_READ)
            continue;
        XtreamFilm *f = xtreamFindFilm(to, old->id);
        if (f && f->info_state == XTREAM_INFO_UNREAD) {
            f->info = old->info;
            f->info_state = XTREAM_INFO_READ;
            memset(&old->info, 0, sizeof(old->info));
            old->info_state = XTREAM_INFO_UNREAD;
        }
    }
}

XtreamShow *xtreamFindShow(XtreamCatalog *c, long long show_id)
{
    for (int i = 0; i < c->show_count; i++)
        if (c->shows[i].id == show_id)
            return &c->shows[i];
    return NULL;
}

void xtreamShowSetEpisodes(XtreamCatalog *c, long long show_id, XtreamEpisode *episodes, int count)
{
    XtreamShow *s = xtreamFindShow(c, show_id);
    if (!s) {
        xtreamEpisodesFree(episodes, count);
        return;
    }
    xtreamEpisodesFree(s->episodes, s->episode_count);
    s->episodes = episodes;
    s->episode_count = episodes ? count : 0;
    s->state = episodes && count > 0 ? XTREAM_EPISODES_READ : XTREAM_EPISODES_FAILED;
}

// --- Locations ---

bool xtreamIsLocation(const char *location)
{
    return !strncmp(location, XTREAM_PREFIX, sizeof(XTREAM_PREFIX) - 1);
}

bool xtreamLocation(char *out, size_t size, const char *kind, long long id, const char *ext)
{
    int n = ext && ext[0] ? snprintf(out, size, XTREAM_PREFIX "%s/%lld.%s", kind, id, ext)
                          : snprintf(out, size, XTREAM_PREFIX "%s/%lld", kind, id);
    return n > 0 && (size_t)n < size;
}

bool xtreamParseLocation(const char *location, char *kind, size_t kind_size, long long *id, char *ext,
                         size_t ext_size)
{
    if (!xtreamIsLocation(location))
        return false;
    const char *p = location + sizeof(XTREAM_PREFIX) - 1, *slash = strchr(p, '/');
    if (!slash || slash == p || (size_t)(slash - p) >= kind_size)
        return false;
    snprintf(kind, kind_size, "%.*s", (int)(slash - p), p);
    char *end;
    *id = strtoll(slash + 1, &end, 10);
    if (*id <= 0 || end == slash + 1)
        return false;
    if (*end == '\0') {
        snprintf(ext, ext_size, "%s", "");
        return true;
    }
    if (*end != '.' || !end[1] || strlen(end + 1) >= ext_size)
        return false;
    for (const char *q = end + 1; *q; q++)
        if (!isalnum((unsigned char)*q))
            return false;
    snprintf(ext, ext_size, "%s", end + 1);
    return true;
}

long long xtreamFilmOf(const char *location)
{
    char kind[16], ext[8];
    long long id;
    return xtreamParseLocation(location, kind, sizeof(kind), &id, ext, sizeof(ext)) && !strcmp(kind, "movie")
               ? id
               : -1;
}

long long xtreamStandInShow(const char *location)
{
    char kind[16], ext[8];
    long long id;
    return xtreamParseLocation(location, kind, sizeof(kind), &id, ext, sizeof(ext)) && !strcmp(kind, "show")
               ? id
               : -1;
}

// --- The library ---

static void posterKey(char *out, size_t size, const char *url)
{
    snprintf(out, size, "%s%s", url ? XTREAM_POSTER_PREFIX : "", url ? url : "");
}

static void addFilm(Library *lib, const XtreamFilm *f)
{
    char path[LIBRARY_PATH_MAX], art[POSTER_MAX], backdrop[POSTER_MAX];
    if (!xtreamLocation(path, sizeof(path), "movie", f->id, f->ext))
        return;
    posterKey(backdrop, sizeof(backdrop), f->info.backdrop);
    VideoName name;
    memset(&name, 0, sizeof(name));
    snprintf(name.title, sizeof(name.title), "%s", f->name);
    name.year = f->year;
    posterKey(art, sizeof(art), f->poster);
    LibraryRemote remote = {.art = art, .in_collection = 0, .in_group = f->group + 1, .overview = f->info.plot,
                            .runtime_ns = f->info.runtime_s * 1000000000ll, .genres = f->info.genre,
                            .backdrop = backdrop};
    libraryAddRemote(lib, path, &name, 0, f->added, &remote);
}

static void addShow(Library *lib, const XtreamShow *s, const char *unread, const char *failed)
{
    char path[LIBRARY_PATH_MAX], art[POSTER_MAX], backdrop[POSTER_MAX];
    posterKey(art, sizeof(art), s->cover);
    posterKey(backdrop, sizeof(backdrop), s->backdrop);
    VideoName name;
    memset(&name, 0, sizeof(name));
    name.is_episode = true;
    name.year = s->year;
    snprintf(name.show, sizeof(name.show), "%s", s->name);
    if (s->state != XTREAM_EPISODES_READ) {  // A stand-in until they're read.
        if (!xtreamLocation(path, sizeof(path), "show", s->id, NULL))
            return;
        name.season = name.episode = 1;
        snprintf(name.title, sizeof(name.title), "%s", s->state == XTREAM_EPISODES_FAILED ? failed : unread);
        LibraryRemote remote = {.series_art = art, .overview = s->plot, .in_group = s->group + 1,
                                .genres = s->genre, .series_overview = s->plot, .backdrop = backdrop};
        libraryAddRemote(lib, path, &name, 0, s->added, &remote);
        return;
    }
    for (int i = 0; i < s->episode_count; i++) {
        const XtreamEpisode *e = &s->episodes[i];
        if (!xtreamLocation(path, sizeof(path), "series", e->id, e->ext))
            continue;
        name.season = e->season;
        name.episode = e->episode;
        snprintf(name.title, sizeof(name.title), "%s", e->title ? e->title : "");
        LibraryRemote remote = {.series_art = art, .overview = e->plot ? e->plot : s->plot,
                                .runtime_ns = e->runtime_s * 1000000000ll, .in_group = s->group + 1,
                                .genres = s->genre, .series_overview = i == 0 ? s->plot : NULL,
                                .backdrop = backdrop};
        libraryAddRemote(lib, path, &name, 0, s->added, &remote);
    }
}

void xtreamFillLibrary(Library *lib, const XtreamCatalog *c, const char *unread, const char *failed)
{
    for (int i = 0; i < c->film_count; i++)
        addFilm(lib, &c->films[i]);
    for (int i = 0; i < c->show_count; i++)
        addShow(lib, &c->shows[i], unread, failed);
}

// --- The account ---

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static XtreamAccount g_account;
static bool g_has_account, g_any_certificate;

void xtreamVodSetAccount(const XtreamAccount *a, bool any_certificate)
{
    pthread_mutex_lock(&g_lock);
    memset(&g_account, 0, sizeof(g_account));
    g_has_account = a != NULL;
    if (a)
        g_account = *a;
    g_any_certificate = any_certificate;
    pthread_mutex_unlock(&g_lock);
}

bool xtreamVodAnyCertificate(void)
{
    pthread_mutex_lock(&g_lock);
    bool any = g_any_certificate;
    pthread_mutex_unlock(&g_lock);
    return any;
}

bool xtreamVodApiUrl(const char *action, const char *extra, char *out, size_t size)
{
    pthread_mutex_lock(&g_lock);
    bool ok = g_has_account && xtreamApiUrl(&g_account, action, extra, out, size);
    pthread_mutex_unlock(&g_lock);
    return ok;
}

bool xtreamVodStreamRequest(const char *location, char *url, size_t url_size, char *authorization,
                            size_t auth_size)
{
    char kind[16], ext[8];
    long long id;
    if (auth_size > 0)
        authorization[0] = '\0';
    if (!xtreamParseLocation(location, kind, sizeof(kind), &id, ext, sizeof(ext)) || !ext[0] ||
        (strcmp(kind, "movie") && strcmp(kind, "series")))
        return false;
    pthread_mutex_lock(&g_lock);
    bool ok = g_has_account && xtreamStreamUrl(&g_account, kind, id, ext, url, url_size);
    pthread_mutex_unlock(&g_lock);
    return ok;
}
