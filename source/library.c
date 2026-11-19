#include "library.h"

#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "browser.h"
#include "util.h"

void libraryInit(Library *lib, const char *root)
{
    memset(lib, 0, sizeof(*lib));
    snprintf(lib->root, sizeof(lib->root), "%.*s", (int)sizeof(lib->root) - 1, root);
}

static void freeBuilt(Library *lib)
{
    for (size_t i = 0; i < lib->show_count; i++)
        free(lib->shows[i].episodes);
    free(lib->shows);
    free(lib->films);
    free(lib->recent);
    for (int i = 0; i < lib->collection_count; i++) {
        free(lib->collections[i].refs);
        lib->collections[i].refs = NULL;
        lib->collections[i].count = 0;
    }
    lib->shows = NULL;
    lib->films = NULL;
    lib->recent = NULL;
    lib->show_count = lib->film_count = lib->recent_count = 0;
}

void libraryFree(Library *lib)
{
    freeBuilt(lib);
    for (size_t i = 0; i < lib->count; i++) {
        free(lib->items[i].path);
        free(lib->items[i].overview);
        free(lib->items[i].series_art);
    }
    free(lib->items);
    lib->items = NULL;
    lib->count = lib->capacity = 0;
}

bool libraryAdd(Library *lib, const char *path, int64_t size, int64_t added)
{
    if (lib->count == lib->capacity) {
        size_t grown = lib->capacity ? lib->capacity * 2 : 256;
        LibraryItem *items = realloc(lib->items, grown * sizeof(LibraryItem));
        if (!items)
            return false;
        lib->items = items;
        lib->capacity = grown;
    }
    char *copy = strdup(path);
    if (!copy)
        return false;
    LibraryItem *item = &lib->items[lib->count++];
    memset(item, 0, sizeof(*item));
    item->path = copy;
    item->size = size;
    item->added = added;
    item->show = -1;
    item->collection = -1;
    return true;
}

bool libraryAddRemote(Library *lib, const char *path, const VideoName *name, int64_t size, int64_t added,
                      const LibraryRemote *remote)
{
    if (!libraryAdd(lib, path, size, added))
        return false;
    LibraryItem *item = &lib->items[lib->count - 1];
    item->named = true;
    item->name = *name;
    item->runtime_ns = remote->runtime_ns;
    item->blocked = remote->blocked;
    item->overview = remote->overview && remote->overview[0] ? strdup(remote->overview) : NULL;
    item->series_art = remote->series_art && remote->series_art[0] ? strdup(remote->series_art) : NULL;
    item->favorite = remote->favorite;
    int collection = remote->in_collection - 1;
    item->collection = collection >= 0 && collection < lib->collection_count ? collection : -1;
    return true;
}

int libraryAddCollection(Library *lib, const char *name, CollectionKind kind)
{
    if (!name || !name[0] || lib->collection_count >= LIBRARY_COLLECTIONS)
        return -1;
    LibraryCollection *c = &lib->collections[lib->collection_count];
    snprintf(c->name, sizeof(c->name), "%s", name);
    c->kind = kind;
    return lib->collection_count++;
}

// Series by name (as built), then films by title, of those that pass.
static size_t gatherRefs(const Library *lib, LibraryRef *out, size_t max,
                         bool (*passes)(const Library *, LibraryRef, int), int arg)
{
    size_t n = 0;
    for (size_t i = 0; i < lib->show_count && n < max; i++)
        if (passes(lib, (LibraryRef){true, (int)i}, arg))
            out[n++] = (LibraryRef){true, (int)i};
    for (size_t i = 0; i < lib->film_count && n < max; i++)
        if (passes(lib, (LibraryRef){false, lib->films[i]}, arg))
            out[n++] = (LibraryRef){false, lib->films[i]};
    return n;
}

static bool inCollection(const Library *lib, LibraryRef r, int collection)
{
    return (r.is_show ? lib->shows[r.index].collection : lib->items[r.index].collection) == collection;
}

static bool isFavorite(const Library *lib, LibraryRef r, int unused)
{
    (void)unused;
    return r.is_show ? lib->shows[r.index].favorite : lib->items[r.index].favorite;
}

size_t libraryCollectionRefs(const Library *lib, int collection, LibraryRef *out, size_t max)
{
    if (collection < 0 || collection >= lib->collection_count)
        return 0;
    return gatherRefs(lib, out, max, inCollection, collection);
}

size_t libraryFavorites(const Library *lib, LibraryRef *out, size_t max)
{
    return gatherRefs(lib, out, max, isFavorite, 0);
}

void librarySetFavorite(Library *lib, LibraryRef ref, bool favorite)
{
    if (!ref.is_show) {
        if (ref.index >= 0 && (size_t)ref.index < lib->count)
            lib->items[ref.index].favorite = favorite;
        return;
    }
    if (ref.index < 0 || (size_t)ref.index >= lib->show_count)
        return;
    LibraryShow *show = &lib->shows[ref.index];
    show->favorite = favorite;
    for (int i = 0; i < show->count; i++)
        lib->items[show->episodes[i]].favorite = favorite;
}

// --- Building ---

typedef struct {
    int item;
    char key[NAMING_MAX];
} Keyed;

static const Library *g_sorting;  // qsort has no context: the library being sorted.
static pthread_mutex_t g_sorting_lock = PTHREAD_MUTEX_INITIALIZER;  // Libraries are built in several threads.

static int compareEpisodes(const void *pa, const void *pb)
{
    const Keyed *a = pa, *b = pb;
    int c = strcmp(a->key, b->key);
    if (c)
        return c;
    const VideoName *na = &g_sorting->items[a->item].name, *nb = &g_sorting->items[b->item].name;
    if (na->season != nb->season)
        return na->season - nb->season;
    if (na->episode != nb->episode)
        return na->episode - nb->episode;
    return strcmp(g_sorting->items[a->item].path, g_sorting->items[b->item].path);
}

static int compareFilms(const void *pa, const void *pb)
{
    const LibraryItem *a = &g_sorting->items[*(const int *)pa], *b = &g_sorting->items[*(const int *)pb];
    int c = utilCompareNames(a->name.title, b->name.title);
    return c ? c : a->name.year - b->name.year;
}

static int64_t refAdded(const Library *lib, LibraryRef r)
{
    return r.is_show ? lib->shows[r.index].added : lib->items[r.index].added;
}

static int compareRecent(const void *pa, const void *pb)
{
    int64_t a = refAdded(g_sorting, *(const LibraryRef *)pa), b = refAdded(g_sorting, *(const LibraryRef *)pb);
    return a > b ? -1 : a < b ? 1 : 0;
}

// The spelling of the series that most of its episodes use.
static void commonName(const Library *lib, const LibraryShow *show, char *out, size_t size)
{
    int best = 0, best_count = 0;
    for (int i = 0; i < show->count; i++) {
        const char *name = lib->items[show->episodes[i]].name.show;
        int same = 0;
        for (int j = 0; j < show->count; j++)
            same += strcmp(name, lib->items[show->episodes[j]].name.show) == 0;
        if (same > best_count) {
            best = i;
            best_count = same;
        }
    }
    snprintf(out, size, "%s", show->count ? lib->items[show->episodes[best]].name.show : "");
}

// Groups the episodes (whose show names give the same key) into series.
static void buildShows(Library *lib, Keyed *episodes, size_t count)
{
    qsort(episodes, count, sizeof(Keyed), compareEpisodes);
    lib->shows = calloc(count ? count : 1, sizeof(LibraryShow));
    if (!lib->shows)
        return;
    for (size_t i = 0; i < count;) {
        size_t end = i;
        while (end < count && strcmp(episodes[end].key, episodes[i].key) == 0)
            end++;
        LibraryShow *show = &lib->shows[lib->show_count];
        show->episodes = malloc((end - i) * sizeof(int));
        if (!show->episodes)
            break;
        show->collection = lib->items[episodes[i].item].collection;
        int last_season = -1;
        for (size_t j = i; j < end; j++) {
            LibraryItem *item = &lib->items[episodes[j].item];
            item->show = (int)lib->show_count;
            show->episodes[show->count++] = episodes[j].item;
            if (item->name.season != last_season) {
                show->seasons++;
                last_season = item->name.season;
            }
            if (item->added > show->added)
                show->added = item->added;
            show->favorite = show->favorite || item->favorite;
        }
        commonName(lib, show, show->name, sizeof(show->name));
        lib->show_count++;
        i = end;
    }
}

static void buildLocked(Library *lib)
{
    freeBuilt(lib);
    g_sorting = lib;
    Keyed *episodes = malloc((lib->count ? lib->count : 1) * sizeof(Keyed));
    lib->films = malloc((lib->count ? lib->count : 1) * sizeof(int));
    if (!episodes || !lib->films) {
        free(episodes);
        return;
    }
    size_t episode_count = 0;
    for (size_t i = 0; i < lib->count; i++) {
        LibraryItem *item = &lib->items[i];
        if (!item->named)
            namingParse(item->path, lib->root, &item->name);
        item->show = -1;
        if (item->name.is_episode) {
            episodes[episode_count].item = (int)i;
            namingKey(item->name.show, episodes[episode_count].key, NAMING_MAX);
            episode_count++;
        } else {
            lib->films[lib->film_count++] = (int)i;
        }
    }
    buildShows(lib, episodes, episode_count);
    free(episodes);
    qsort(lib->films, lib->film_count, sizeof(int), compareFilms);

    lib->recent = malloc((lib->film_count + lib->show_count + 1) * sizeof(LibraryRef));
    if (!lib->recent)
        return;
    for (size_t i = 0; i < lib->show_count; i++)
        lib->recent[lib->recent_count++] = (LibraryRef){true, (int)i};
    for (size_t i = 0; i < lib->film_count; i++)
        lib->recent[lib->recent_count++] = (LibraryRef){false, lib->films[i]};
    // Stable for equal times (qsort isn't): newest first, then series and films by name.
    for (size_t i = 1; i < lib->recent_count; i++) {
        LibraryRef r = lib->recent[i];
        size_t j = i;
        while (j > 0 && compareRecent(&lib->recent[j - 1], &r) > 0) {
            lib->recent[j] = lib->recent[j - 1];
            j--;
        }
        lib->recent[j] = r;
    }
    for (int c = 0; c < lib->collection_count; c++) {
        LibraryCollection *collection = &lib->collections[c];
        collection->refs = malloc((lib->film_count + lib->show_count + 1) * sizeof(LibraryRef));
        collection->count = collection->refs ? libraryCollectionRefs(lib, c, collection->refs,
                                                                     lib->film_count + lib->show_count)
                                             : 0;
    }
}

void libraryBuild(Library *lib)
{
    pthread_mutex_lock(&g_sorting_lock);
    buildLocked(lib);
    pthread_mutex_unlock(&g_sorting_lock);
}

// --- Scanning the card ---

static bool joinPath(char *out, size_t size, const char *folder, const char *name)
{
    size_t len = strlen(folder);
    int n = snprintf(out, size, "%s%s%s", folder, len && folder[len - 1] == '/' ? "" : "/", name);
    return n > 0 && (size_t)n < size;
}

static bool scanFolder(Library *lib, const char *folder, int depth, const volatile bool *cancel)
{
    DIR *dir = opendir(folder);
    if (!dir)
        return false;
    bool at_card_root = strcmp(folder, BROWSER_ROOT) == 0;
    struct dirent *e;
    char path[LIBRARY_PATH_MAX];
    while ((e = readdir(dir)) != NULL && !(cancel && *cancel)) {
        if (e->d_name[0] == '.' || (at_card_root && strcmp(e->d_name, "Nintendo") == 0) ||
            !joinPath(path, sizeof(path), folder, e->d_name))
            continue;
        struct stat st;
        bool known = e->d_type != DT_UNKNOWN;
        bool is_dir = e->d_type == DT_DIR;
        if (!known || !is_dir) {
            if (stat(path, &st) != 0)
                continue;
            is_dir = S_ISDIR(st.st_mode);
        }
        if (is_dir) {
            if (depth < LIBRARY_DEPTH)
                scanFolder(lib, path, depth + 1, cancel);
        } else if (browserIsVideo(e->d_name)) {
            libraryAdd(lib, path, (int64_t)st.st_size, (int64_t)st.st_mtime);
        }
    }
    closedir(dir);
    return true;
}

bool libraryScan(Library *lib, const volatile bool *cancel)
{
    char root[LIBRARY_PATH_MAX];
    snprintf(root, sizeof(root), "%s", lib->root);
    libraryFree(lib);
    libraryInit(lib, root);
    bool ok = scanFolder(lib, root, 0, cancel) && !(cancel && *cancel);
    if (!(cancel && *cancel))  // Stopped: thrown away, so not worth sorting.
        libraryBuild(lib);
    return ok;
}

// --- The cache ---

bool librarySave(const Library *lib, const char *file)
{
    FILE *f = fopen(file, "w");
    if (!f)
        return false;
    fprintf(f, "root=%s\n", lib->root);
    for (size_t i = 0; i < lib->count; i++)
        if (!strpbrk(lib->items[i].path, "\r\n"))
            fprintf(f, "%lld\t%lld\t%s\n", (long long)lib->items[i].size, (long long)lib->items[i].added,
                    lib->items[i].path);
    return fclose(f) == 0;
}

bool libraryLoad(Library *lib, const char *file)
{
    libraryFree(lib);
    FILE *f = fopen(file, "r");
    if (!f)
        return false;
    char line[LIBRARY_PATH_MAX + 64];
    bool ok = fgets(line, sizeof(line), f) && strncmp(line, "root=", 5) == 0;
    if (ok) {
        line[strcspn(line, "\r\n")] = '\0';
        libraryInit(lib, line + 5);
        while (fgets(line, sizeof(line), f)) {
            line[strcspn(line, "\r\n")] = '\0';
            char *end;
            long long size = strtoll(line, &end, 10);
            if (*end != '\t')
                continue;
            long long added = strtoll(end + 1, &end, 10);
            size_t root_len = strlen(lib->root);
            const char *path = end + 1;
            if (*end != '\t' || strncmp(path, lib->root, root_len) != 0 ||
                root_len == 0 || (path[root_len] != '/' && lib->root[root_len - 1] != '/'))
                continue;  // Broken, or not in the library's folder.
            libraryAdd(lib, end + 1, size, added);
        }
        libraryBuild(lib);
    }
    fclose(f);
    return ok;
}

// --- Looking up ---

int libraryFind(const Library *lib, const char *path)
{
    for (size_t i = 0; i < lib->count; i++)
        if (strcmp(lib->items[i].path, path) == 0)
            return (int)i;
    return -1;
}

int libraryNextEpisode(const Library *lib, int item)
{
    if (item < 0 || (size_t)item >= lib->count || lib->items[item].show < 0)
        return -1;
    const LibraryShow *show = &lib->shows[lib->items[item].show];
    for (int i = 0; i + 1 < show->count; i++)
        if (show->episodes[i] == item)
            return show->episodes[i + 1];
    return -1;
}

static bool holds(const char *name, const char *query_key)
{
    char key[NAMING_MAX];
    namingKey(name, key, sizeof(key));
    return strstr(key, query_key) != NULL;
}

size_t librarySearch(const Library *lib, const char *query, LibraryRef *out, size_t max)
{
    char key[NAMING_MAX];
    namingKey(query, key, sizeof(key));
    if (!key[0])
        return 0;
    size_t n = 0;
    for (size_t i = 0; i < lib->show_count && n < max; i++)
        if (holds(lib->shows[i].name, key))
            out[n++] = (LibraryRef){true, (int)i};
    for (size_t i = 0; i < lib->film_count && n < max; i++)
        if (holds(lib->items[lib->films[i]].name.title, key))
            out[n++] = (LibraryRef){false, lib->films[i]};
    for (size_t s = 0; s < lib->show_count && n < max; s++)
        for (int e = 0; e < lib->shows[s].count && n < max; e++) {
            int item = lib->shows[s].episodes[e];
            if (lib->items[item].name.title[0] && holds(lib->items[item].name.title, key))
                out[n++] = (LibraryRef){false, item};
        }
    return n;
}
