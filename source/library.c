#include "library.h"

#include <limits.h>
#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "browser.h"
#include "libnfo.h"
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
        free(lib->items[i].art);
        free(lib->items[i].genres);
        free(lib->items[i].series_overview);
        free(lib->items[i].backdrop);
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
    item->group = -1;
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
    item->art = remote->art && remote->art[0] ? strdup(remote->art) : NULL;
    item->genres = remote->genres && remote->genres[0] ? strdup(remote->genres) : NULL;
    item->series_overview =
        remote->series_overview && remote->series_overview[0] ? strdup(remote->series_overview) : NULL;
    item->backdrop = remote->backdrop && remote->backdrop[0] ? strdup(remote->backdrop) : NULL;
    item->favorite = remote->favorite;
    int collection = remote->in_collection - 1;
    item->collection = collection >= 0 && collection < lib->collection_count ? collection : -1;
    item->group = remote->in_group > 0 ? remote->in_group - 1 : -1;
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

size_t libraryGroupRefs(const Library *lib, bool shows, int group, int *out, size_t max)
{
    size_t n = 0;
    for (size_t i = 0; shows && i < lib->show_count && n < max; i++)
        if (lib->shows[i].group == group)
            out[n++] = (int)i;
    for (size_t i = 0; !shows && i < lib->film_count && n < max; i++)
        if (lib->items[lib->films[i]].group == group)
            out[n++] = lib->films[i];
    return n;
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
        show->group = lib->items[episodes[i].item].group;
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

// --- A folder's picture ---

#define PICTURE_NAME_MAX 64

bool libraryIsFolderPicture(const char *name, int *rank)
{
    static const char *const STEMS[] = {"poster", "folder", "cover"};
    static const char *const EXTENSIONS[] = {".jpg", ".jpeg", ".png"};
    for (int i = 0; i < 3; i++) {
        size_t n = strlen(STEMS[i]);
        if (strncasecmp(name, STEMS[i], n))
            continue;
        for (int k = 0; k < 3; k++)
            if (!strcasecmp(name + n, EXTENSIONS[k])) {
                if (rank)
                    *rank = i;
                return true;
            }
    }
    return false;
}

// The best picture among a folder's names so far: kept in best (empty for none).
static void considerPicture(char *best, size_t size, int *best_rank, const char *name)
{
    int rank;
    size_t n = strlen(name);  // "poster.jpeg" at most, when it matches.
    if (n < size && libraryIsFolderPicture(name, &rank) && (!best[0] || rank < *best_rank)) {
        memcpy(best, name, n + 1);
        *best_rank = rank;
    }
}

// A folder's picture (name, "" for none) for the videos found from start on: a film or an
// episode right in the folder gets it; an episode deeper (a season's folder) gets it as its series'.
static void applyPicture(Library *lib, size_t start, const char *folder, const char *name)
{
    char key[LIBRARY_PATH_MAX + sizeof(LIBRARY_PICTURE_PREFIX)];
    size_t len = strlen(folder);
    if (!name[0] || snprintf(key, sizeof(key), "%s%s%s%s", LIBRARY_PICTURE_PREFIX, folder,
                             len && folder[len - 1] == '/' ? "" : "/", name) >= (int)sizeof(key))
        return;
    // A film gets it only when it's the folder's one video: in a folder of several, it's the folder's picture.
    bool one_video = lib->count - start == 1;
    for (size_t i = start; i < lib->count; i++) {
        LibraryItem *item = &lib->items[i];
        bool here = libDirectlyIn(item->path, folder);
        VideoName name;  // Names are read when the library is built (or from an .nfo already); here, for this.
        if (item->named)
            name = item->name;
        else
            namingParse(item->path, lib->root, &name);
        if (name.is_episode && !item->series_art && (here || libInSeasonOf(item->path, folder)))
            item->series_art = strdup(key);
        else if (here && one_video && !name.is_episode && !item->art)
            item->art = strdup(key);
    }
}

// After a folder's files were listed: its .nfo files and pictures, for the videos found in it from start on.
static void finishFolder(Library *lib, size_t start, const char *folder, int depth, const char *picture,
                         LibFolderFiles *files, LibraryReadFile read, int *budget)
{
    libnfoApply(lib, start, folder, depth == 0, files, read, budget);  // A video's own pictures first.
    if (depth > 0)  // The library's own folder's picture isn't any one video's.
        applyPicture(lib, start, folder, picture);
    libFolderFree(files);
}

// --- Scanning the card ---

static bool joinPath(char *out, size_t size, const char *folder, const char *name)
{
    size_t len = strlen(folder);
    int n = snprintf(out, size, "%s%s%s", folder, len && folder[len - 1] == '/' ? "" : "/", name);
    return n > 0 && (size_t)n < size;
}

static bool scanFolder(Library *lib, const char *folder, int depth, int *budget, const volatile bool *cancel)
{
    DIR *dir = opendir(folder);
    if (!dir)
        return false;
    bool at_card_root = strcmp(folder, BROWSER_ROOT) == 0;
    struct dirent *e;
    char path[LIBRARY_PATH_MAX], picture[PICTURE_NAME_MAX] = "";
    int picture_rank = 0;
    size_t start = lib->count;
    LibFolderFiles files = {0};
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
                scanFolder(lib, path, depth + 1, budget, cancel);
        } else if (browserIsVideo(e->d_name)) {
            libraryAdd(lib, path, (int64_t)st.st_size, (int64_t)st.st_mtime);
        } else {
            considerPicture(picture, sizeof(picture), &picture_rank, e->d_name);
            libFolderNote(&files, e->d_name);
        }
    }
    closedir(dir);
    finishFolder(lib, start, folder, depth, picture, &files, libnfoReadCard, budget);
    return true;
}

bool libraryScan(Library *lib, const volatile bool *cancel)
{
    char root[LIBRARY_PATH_MAX];
    snprintf(root, sizeof(root), "%s", lib->root);
    libraryFree(lib);
    libraryInit(lib, root);
    int budget = INT_MAX;  // The card's files are quick to read.
    bool ok = scanFolder(lib, root, 0, &budget, cancel) && !(cancel && *cancel);
    if (!(cancel && *cancel))  // Stopped: thrown away, so not worth sorting.
        libraryBuild(lib);
    return ok;
}

typedef struct {
    bool (*list)(const char *, DavEntry **, size_t *, char *, size_t);
    LibraryReadFile read;
    const volatile bool *cancel;
    int folders;  // Asked for so far.
    int nfo_budget;  // .nfo files it may still read.
} NetworkScan;

static bool scanNetworkFolder(Library *lib, NetworkScan *s, const char *folder, int depth)
{
    DavEntry *entries = NULL;
    size_t count = 0;
    char why[160];
    if (++s->folders > LIBRARY_NETWORK_FOLDERS || !s->list(folder, &entries, &count, why, sizeof(why)))
        return false;
    char path[LIBRARY_PATH_MAX], picture[PICTURE_NAME_MAX] = "";
    int picture_rank = 0;
    size_t start = lib->count;
    LibFolderFiles files = {0};
    for (size_t i = 0; i < count && !(s->cancel && *s->cancel); i++) {
        const DavEntry *e = &entries[i];
        if (e->name[0] == '.' || !joinPath(path, sizeof(path), folder, e->name))
            continue;
        if (e->is_dir && depth < LIBRARY_DEPTH) {
            scanNetworkFolder(lib, s, path, depth + 1);
        } else if (!e->is_dir && browserIsVideo(e->name) && lib->count < LIBRARY_NETWORK_VIDEOS) {
            libraryAdd(lib, path, e->size, e->modified);
        } else if (!e->is_dir) {
            considerPicture(picture, sizeof(picture), &picture_rank, e->name);
            libFolderNote(&files, e->name);
        }
    }
    free(entries);
    if (s->cancel && *s->cancel)
        libFolderFree(&files);  // Thrown away: nothing more to read.
    else
        finishFolder(lib, start, folder, depth, picture, &files, s->read, &s->nfo_budget);
    return true;
}

bool libraryScanNetwork(Library *lib, bool (*list)(const char *, DavEntry **, size_t *, char *, size_t),
                        LibraryReadFile read, const volatile bool *cancel)
{
    char root[LIBRARY_PATH_MAX];
    snprintf(root, sizeof(root), "%s", lib->root);
    libraryFree(lib);
    libraryInit(lib, root);
    NetworkScan s = {list, read, cancel, 0, LIBRARY_NETWORK_NFOS};
    bool ok = scanNetworkFolder(lib, &s, root, 0) && !(cancel && *cancel);
    if (!(cancel && *cancel))
        libraryBuild(lib);
    return ok;
}

// --- The cache ---

#define CACHE_LINE_MAX 8192  // An .nfo's line: two plots of NFO_PLOT_MAX, names and a path fit.
#define NFO_FIELDS 12        // "nfo", the video, then what it says (more, from a later version, are skipped).
#define NS_PER_MIN 60000000000ll

static bool hasBreak(const char *text)
{
    return text && strpbrk(text, "\t\r\n");
}

// What an .nfo said of the video, on one line (none if it said nothing).
static void saveNfo(FILE *f, const LibraryItem *it)
{
    const VideoName *n = &it->name;
    bool said = it->named || it->overview || it->genres || it->series_overview || it->runtime_ns;
    if (!said || hasBreak(n->title) || hasBreak(n->show) || hasBreak(it->genres) || hasBreak(it->overview) ||
        hasBreak(it->series_overview))
        return;
    fprintf(f, "nfo\t%s\t%d\t%d\t%d\t%d\t%lld\t%s\t%s\t%s\t%s\t%s\n", it->path, it->named ? n->is_episode : -1,
            n->season, n->episode, n->year, (long long)(it->runtime_ns / NS_PER_MIN), it->named ? n->title : "",
            it->named ? n->show : "", it->genres ? it->genres : "", it->overview ? it->overview : "",
            it->series_overview ? it->series_overview : "");
}

// "<tag>\t<video>\t<picture's location>" if art is a picture in a folder ("picture:<location>").
static void savePicture(FILE *f, const char *tag, const char *path, const char *art)
{
    const size_t prefix = sizeof(LIBRARY_PICTURE_PREFIX) - 1;
    if (art && !strncmp(art, LIBRARY_PICTURE_PREFIX, prefix) && !strpbrk(art, "\r\n"))
        fprintf(f, "%s\t%s\t%s\n", tag, path, art + prefix);
}

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
    // Folders' pictures and what .nfo files said, after the videos (older versions skip these lines).
    for (size_t i = 0; i < lib->count; i++) {
        const LibraryItem *it = &lib->items[i];
        if (strpbrk(it->path, "\r\n\t"))
            continue;
        savePicture(f, "picture", it->path, it->art);
        savePicture(f, "series_picture", it->path, it->series_art);
        savePicture(f, "backdrop", it->path, it->backdrop);
        saveNfo(f, it);
    }
    return fclose(f) == 0;
}

// The item with this path, looked for first where *hint says (the lines after the videos come in their
// order), then everywhere. -1 if none.
static int findFrom(const Library *lib, const char *path, size_t *hint)
{
    for (size_t k = 0; k < 2; k++) {
        size_t i = *hint + k;
        if (i < lib->count && !strcmp(lib->items[i].path, path)) {
            *hint = i;
            return (int)i;
        }
    }
    int found = libraryFind(lib, path);
    if (found >= 0)
        *hint = (size_t)found;
    return found;
}

// A cache line "picture\t<video>\t<picture>" (or series_picture, backdrop): the video's art, if the video
// was read and the picture is in the library's folder. True if it was such a line.
static bool readPictureLine(Library *lib, char *line, size_t *hint)
{
    static const struct {
        const char *tag;
        size_t length;
    } TAGS[] = {{"picture\t", 8}, {"series_picture\t", 15}, {"backdrop\t", 9}};
    int kind = -1;
    for (int k = 0; k < 3 && kind < 0; k++)
        if (!strncmp(line, TAGS[k].tag, TAGS[k].length))
            kind = k;
    if (kind < 0)
        return false;
    char *video = line + TAGS[kind].length, *tab = strchr(video, '\t');
    if (!tab)
        return true;
    *tab = '\0';
    const char *picture = tab + 1;
    size_t root = strlen(lib->root);
    char key[LIBRARY_PATH_MAX + sizeof(LIBRARY_PICTURE_PREFIX)];
    int item = findFrom(lib, video, hint);
    if (item < 0 || !root || strncmp(picture, lib->root, root) ||
        snprintf(key, sizeof(key), "%s%s", LIBRARY_PICTURE_PREFIX, picture) >= (int)sizeof(key))
        return true;
    LibraryItem *it = &lib->items[item];
    char **slot = kind == 0 ? &it->art : kind == 1 ? &it->series_art : &it->backdrop;
    if (!*slot)
        *slot = strdup(key);
    return true;
}

// Splits line at its tabs (empty fields kept): how many, at most max.
static int splitTabs(char *line, char **fields, int max)
{
    int n = 0;
    for (char *p = line; n < max;) {
        fields[n++] = p;
        char *tab = strchr(p, '\t');
        if (!tab)
            break;
        *tab = '\0';
        p = tab + 1;
    }
    return n;
}

// A whole number from min to max, or fallback.
static int readInt(const char *text, int min, int max, int fallback)
{
    char *end;
    long v = strtol(text, &end, 10);
    return end != text && *end == '\0' && v >= min && v <= max ? (int)v : fallback;
}

static void keepText(char **slot, const char *text)
{
    if (text[0] && !*slot)
        *slot = strdup(text);
}

// A cache line "nfo\t<video>\t..." (saveNfo's): what its .nfo said. True if it was such a line.
static bool readNfoLine(Library *lib, char *line, size_t *hint)
{
    if (strncmp(line, "nfo\t", 4))
        return false;
    char *f[NFO_FIELDS];
    if (splitTabs(line, f, NFO_FIELDS) < NFO_FIELDS)
        return true;  // Broken.
    int item = findFrom(lib, f[1], hint);
    if (item < 0)
        return true;
    LibraryItem *it = &lib->items[item];
    int episode = readInt(f[2], -1, 1, -1);
    if (episode >= 0) {  // Its name as the .nfo gave it (-1: the file's name stands).
        it->named = true;
        memset(&it->name, 0, sizeof(it->name));
        it->name.is_episode = episode == 1;
        it->name.season = readInt(f[3], 0, 9999, 0);
        it->name.episode = readInt(f[4], 0, 99999, 0);
        it->name.year = readInt(f[5], 0, 9999, 0);
        snprintf(it->name.title, sizeof(it->name.title), "%s", f[7]);
        snprintf(it->name.show, sizeof(it->name.show), "%s", f[8]);
    }
    int minutes = readInt(f[6], 0, 100000, 0);
    if (minutes && !it->runtime_ns)
        it->runtime_ns = minutes * NS_PER_MIN;
    keepText(&it->genres, f[9]);
    keepText(&it->overview, f[10]);
    keepText(&it->series_overview, f[11]);
    return true;
}

bool libraryLoad(Library *lib, const char *file)
{
    libraryFree(lib);
    FILE *f = fopen(file, "r");
    if (!f)
        return false;
    char *line = malloc(CACHE_LINE_MAX);
    bool ok = line && fgets(line, CACHE_LINE_MAX, f) && strncmp(line, "root=", 5) == 0;
    if (ok) {
        line[strcspn(line, "\r\n")] = '\0';
        libraryInit(lib, line + 5);
        size_t hint = 0;
        while (fgets(line, CACHE_LINE_MAX, f)) {
            line[strcspn(line, "\r\n")] = '\0';
            if (readPictureLine(lib, line, &hint) || readNfoLine(lib, line, &hint))
                continue;
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
    free(line);
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
