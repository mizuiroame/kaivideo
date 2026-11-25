#include "libnfo.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "nfo.h"

#define FILES_FIRST 16
#define NS_PER_MIN 60000000000ll

static const char *const PICTURE_EXTENSIONS[] = {".jpg", ".jpeg", ".png"};
#define PICTURE_EXTENSION_COUNT 3

static bool endsWith(const char *name, const char *ending)
{
    size_t n = strlen(name), e = strlen(ending);
    return n > e && !strcasecmp(name + n - e, ending);
}

void libFolderNote(LibFolderFiles *files, const char *name)
{
    bool keep = endsWith(name, ".nfo");
    for (int i = 0; !keep && i < PICTURE_EXTENSION_COUNT; i++)
        keep = endsWith(name, PICTURE_EXTENSIONS[i]);
    if (!keep)
        return;
    if (files->count == files->capacity) {
        size_t grown = files->capacity ? files->capacity * 2 : FILES_FIRST;
        char **names = realloc(files->names, grown * sizeof(char *));
        if (!names)
            return;
        files->names = names;
        files->capacity = grown;
    }
    char *copy = strdup(name);
    if (copy)
        files->names[files->count++] = copy;
}

void libFolderFree(LibFolderFiles *files)
{
    for (size_t i = 0; i < files->count; i++)
        free(files->names[i]);
    free(files->names);
    memset(files, 0, sizeof(*files));
}

bool libDirectlyIn(const char *path, const char *folder)
{
    size_t n = strlen(folder);
    if (strncmp(path, folder, n))
        return false;
    const char *rest = path + n;
    if (n && folder[n - 1] != '/') {
        if (*rest != '/')
            return false;
        rest++;
    }
    return *rest && !strchr(rest, '/');
}

bool libInSeasonOf(const char *path, const char *folder)
{
    size_t n = strlen(folder);
    if (strncmp(path, folder, n))
        return false;
    const char *rest = path + n + (n && folder[n - 1] != '/' && path[n] == '/');
    const char *slash = strchr(rest, '/');
    if (!slash || slash == rest || strchr(slash + 1, '/'))
        return false;  // Not exactly one folder down.
    if ((size_t)(slash - rest) == 8 && !strncasecmp(rest, "Specials", 8))
        return true;
    for (const char *p = rest; p < slash; p++)
        if (isdigit((unsigned char)*p))
            return true;
    return false;
}

char *libnfoReadCard(const char *location, size_t max_bytes, size_t *size)
{
    FILE *f = fopen(location, "rb");
    if (!f)
        return NULL;
    char *data = malloc(max_bytes + 1);
    size_t n = data ? fread(data, 1, max_bytes, f) : 0;
    fclose(f);
    if (!data)
        return NULL;
    data[n] = '\0';
    *size = n;
    return data;
}

// --- Finding a video's files ---

// The folder's file named stem + suffix + ending (any case), or NULL.
static const char *findFile(const LibFolderFiles *files, const char *stem, size_t stem_length, const char *suffix,
                            const char *ending)
{
    size_t suffix_length = strlen(suffix), ending_length = strlen(ending);
    for (size_t i = 0; i < files->count; i++) {
        const char *name = files->names[i];
        if (strlen(name) == stem_length + suffix_length + ending_length && !strncasecmp(name, stem, stem_length) &&
            !strncasecmp(name + stem_length, suffix, suffix_length) &&
            !strcasecmp(name + stem_length + suffix_length, ending))
            return name;
    }
    return NULL;
}

// A picture stem + suffix in any of the extensions, or NULL.
static const char *findPicture(const LibFolderFiles *files, const char *stem, size_t stem_length, const char *suffix)
{
    for (int i = 0; i < PICTURE_EXTENSION_COUNT; i++) {
        const char *name = findFile(files, stem, stem_length, suffix, PICTURE_EXTENSIONS[i]);
        if (name)
            return name;
    }
    return NULL;
}

// The folder's own wide picture: fanart, backdrop or background (in that order), or NULL.
static const char *folderFanart(const LibFolderFiles *files)
{
    static const char *const NAMES[] = {"fanart", "backdrop", "background"};
    for (int i = 0; i < 3; i++) {
        const char *name = findPicture(files, NAMES[i], strlen(NAMES[i]), "");
        if (name)
            return name;
    }
    return NULL;
}

static bool joinIn(char *out, size_t size, const char *prefix, const char *folder, const char *name)
{
    size_t len = strlen(folder);
    int n = snprintf(out, size, "%s%s%s%s", prefix, folder, len && folder[len - 1] == '/' ? "" : "/", name);
    return n > 0 && (size_t)n < size;
}

// Sets *slot to the picture's key ("picture:<location>") if it's empty and there's one.
static void setPicture(char **slot, const char *folder, const char *name)
{
    char key[LIBRARY_PATH_MAX + sizeof(LIBRARY_PICTURE_PREFIX)];
    if (!*slot && name && joinIn(key, sizeof(key), LIBRARY_PICTURE_PREFIX, folder, name))
        *slot = strdup(key);
}

static void setText(char **slot, const char *text)
{
    if (!text[0])
        return;
    char *copy = strdup(text);
    if (!copy)
        return;
    free(*slot);
    *slot = copy;
}

// Reads folder/name into info. False if it can't be read or isn't a film's, series' or episode's.
static bool readNfo(const char *folder, const char *name, LibraryReadFile read, int *budget, NfoInfo *info)
{
    char location[LIBRARY_PATH_MAX];
    if (!name || !read || *budget <= 0 || !joinIn(location, sizeof(location), "", folder, name))
        return false;
    (*budget)--;
    size_t size = 0;
    char *text = read(location, NFO_MAX_BYTES, &size);
    bool ok = text && nfoParse(text, size, info);
    free(text);
    return ok;
}

// --- What an .nfo says, into the library ---

static void ensureNamed(const Library *lib, LibraryItem *it)
{
    if (!it->named) {
        namingParse(it->path, lib->root, &it->name);
        it->named = true;
    }
}

static bool isEpisode(const Library *lib, const LibraryItem *it)
{
    if (it->named)
        return it->name.is_episode;
    VideoName name;  // Names are read when the library is built; here, for this.
    namingParse(it->path, lib->root, &name);
    return name.is_episode;
}

// A film's or an episode's own .nfo.
static void applyItem(const Library *lib, LibraryItem *it, const NfoInfo *n)
{
    if (n->kind != NFO_MOVIE && n->kind != NFO_EPISODE)
        return;
    ensureNamed(lib, it);
    VideoName *name = &it->name;
    if (n->kind == NFO_MOVIE) {
        if (name->is_episode && !n->title[0])
            return;  // A film's file named like an episode, without a title to call it by.
        if (name->is_episode) {  // It says it's a film: its name decides.
            name->is_episode = false;
            name->show[0] = '\0';
            name->season = name->episode = 0;
        }
        if (n->year)
            name->year = n->year;
        if (n->genres[0])
            setText(&it->genres, n->genres);
    } else if (name->is_episode && n->season >= 0 && n->episode > 0) {
        name->season = n->season;
        name->episode = n->episode;
    }
    if (n->title[0])
        snprintf(name->title, sizeof(name->title), "%s", n->title);
    setText(&it->overview, n->plot);
    if (n->runtime_min)
        it->runtime_ns = n->runtime_min * NS_PER_MIN;
}

// A series' tvshow.nfo (show may be NULL) and wide picture (fanart: a key, or NULL), for its episodes in
// folder and in its season folders.
static void applySeries(Library *lib, size_t start, const char *folder, const NfoInfo *show, const char *fanart)
{
    bool overview_given = false;
    for (size_t i = start; i < lib->count; i++) {
        LibraryItem *it = &lib->items[i];
        if (!(libDirectlyIn(it->path, folder) || libInSeasonOf(it->path, folder)) || !isEpisode(lib, it))
            continue;
        if (fanart && !it->backdrop)
            it->backdrop = strdup(fanart);
        if (!show || it->nfo_series)
            continue;
        ensureNamed(lib, it);
        it->nfo_series = true;
        if (show->title[0])
            snprintf(it->name.show, sizeof(it->name.show), "%s", show->title);
        if (show->year)
            it->name.year = show->year;
        if (show->genres[0])
            setText(&it->genres, show->genres);
        if (show->runtime_min && !it->runtime_ns)
            it->runtime_ns = show->runtime_min * NS_PER_MIN;
        if (!overview_given && show->plot[0]) {  // One episode keeps the series' summary.
            setText(&it->series_overview, show->plot);
            overview_given = true;
        }
    }
}

// The name of path's file without its extension: its start and length.
static const char *stemOf(const char *path, size_t *length)
{
    const char *slash = strrchr(path, '/'), *name = slash ? slash + 1 : path, *dot = strrchr(name, '.');
    *length = dot && dot > name ? (size_t)(dot - name) : strlen(name);
    return name;
}

// A video right in the folder: its .nfo (or the folder's movie.nfo, for its one film) and its pictures.
static void applyOwn(Library *lib, LibraryItem *it, const char *folder, bool alone, const LibFolderFiles *files,
                     LibraryReadFile read, int *budget, NfoInfo *info)
{
    size_t stem_length;
    const char *stem = stemOf(it->path, &stem_length);
    bool episode = isEpisode(lib, it);
    const char *nfo = findFile(files, stem, stem_length, "", ".nfo");
    if (!nfo && alone && !episode)
        nfo = findFile(files, "movie", 5, "", ".nfo");
    if (readNfo(folder, nfo, read, budget, info)) {
        applyItem(lib, it, info);
        episode = it->name.is_episode;
    }
    // An episode's own picture is a still of it (wide); a film's, its poster.
    setPicture(&it->art, folder, findPicture(files, stem, stem_length, episode ? "-thumb" : "-poster"));
    const char *fanart = findPicture(files, stem, stem_length, "-fanart");
    if (!fanart && alone && !episode)
        fanart = folderFanart(files);
    setPicture(&it->backdrop, folder, fanart);
}

void libnfoApply(Library *lib, size_t start, const char *folder, bool top, const LibFolderFiles *files,
                 LibraryReadFile read, int *budget)
{
    if (!files->count || start >= lib->count)
        return;
    NfoInfo *info = malloc(sizeof(NfoInfo));
    if (!info)
        return;
    size_t direct = 0;
    for (size_t i = start; i < lib->count; i++)
        direct += libDirectlyIn(lib->items[i].path, folder);
    for (size_t i = start; i < lib->count; i++)
        if (libDirectlyIn(lib->items[i].path, folder))
            applyOwn(lib, &lib->items[i], folder, direct == 1, files, read, budget, info);
    if (!top) {
        bool has_show = readNfo(folder, findFile(files, "tvshow", 6, "", ".nfo"), read, budget, info) &&
                        info->kind == NFO_TVSHOW;
        char fanart[LIBRARY_PATH_MAX + sizeof(LIBRARY_PICTURE_PREFIX)];
        const char *fanart_name = folderFanart(files);
        bool has_fanart = fanart_name && joinIn(fanart, sizeof(fanart), LIBRARY_PICTURE_PREFIX, folder, fanart_name);
        if (has_show || has_fanart)
            applySeries(lib, start, folder, has_show ? info : NULL, has_fanart ? fanart : NULL);
    }
    free(info);
}
