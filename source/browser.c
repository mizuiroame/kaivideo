#include "browser.h"

#include "scroll.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "util.h"

bool browserIsVideo(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot && (!strcasecmp(dot, ".mp4") || !strcasecmp(dot, ".m4v") || !strcasecmp(dot, ".mkv") ||
                   !strcasecmp(dot, ".webm") || !strcasecmp(dot, ".ts"));
}

// Folders the console and the PC keep for themselves.
static bool hidden(const char *name, bool at_root)
{
    if (name[0] == '.')
        return true;
    return at_root && (!strcmp(name, "Nintendo") || !strcmp(name, "System Volume Information"));
}

static int compareEntries(const void *pa, const void *pb)
{
    const BrowserEntry *a = pa, *b = pb;
    if (a->is_dir != b->is_dir)
        return a->is_dir ? -1 : 1;
    return utilCompareNames(a->name, b->name);
}

static bool join(char *out, size_t size, const char *folder, const char *name)
{
    size_t len = strlen(folder);
    int n = snprintf(out, size, "%s%s%s", folder, len && folder[len - 1] == '/' ? "" : "/", name);
    return n > 0 && (size_t)n < size;
}

static bool grow(Browser *b, size_t *capacity)
{
    if (b->count < *capacity)
        return true;
    size_t grown = *capacity ? *capacity * 2 : 64;
    BrowserEntry *items = realloc(b->items, grown * sizeof(BrowserEntry));
    if (!items)
        return false;
    b->items = items;
    *capacity = grown;
    return true;
}

static bool openNetwork(Browser *b, const char *path)
{
    DavEntry *entries = NULL;
    size_t count = 0, capacity = 0;
    if (!b->network || !b->network(path, &entries, &count, b->problem, sizeof(b->problem)))
        return false;
    for (size_t i = 0; i < count; i++) {
        const DavEntry *e = &entries[i];
        if (hidden(e->name, false) || (!e->is_dir && !browserIsVideo(e->name)) || !grow(b, &capacity))
            continue;
        BrowserEntry *item = &b->items[b->count++];
        snprintf(item->name, sizeof(item->name), "%s", e->name);
        item->is_dir = e->is_dir;
        item->is_network = false;
        item->size = e->size;
    }
    free(entries);
    if (b->count > 1)
        qsort(b->items, b->count, sizeof(BrowserEntry), compareEntries);
    return true;
}

bool browserOpen(Browser *b, const char *path)
{
    browserFree(b);
    snprintf(b->path, sizeof(b->path), "%s", path);
    b->problem[0] = '\0';
    if (davIsLocation(path))
        return openNetwork(b, path);
    DIR *dir = opendir(path);
    if (!dir)
        return false;
    bool at_root = strcmp(path, BROWSER_ROOT) == 0;
    size_t capacity = 0;
    struct dirent *e;
    char full[BROWSER_PATH_MAX];
    while ((e = readdir(dir)) != NULL) {
        if (hidden(e->d_name, at_root) || strlen(e->d_name) >= BROWSER_NAME_MAX)
            continue;
        bool is_dir = e->d_type == DT_DIR;
        struct stat st;
        if (e->d_type == DT_UNKNOWN && join(full, sizeof(full), path, e->d_name) && stat(full, &st) == 0)
            is_dir = S_ISDIR(st.st_mode);  // The file system didn't say: ask.
        if (!is_dir && !browserIsVideo(e->d_name))
            continue;
        if (!grow(b, &capacity))
            break;
        BrowserEntry *item = &b->items[b->count++];
        snprintf(item->name, sizeof(item->name), "%s", e->d_name);
        item->is_dir = is_dir;
        item->is_network = false;
        item->size = -1;
        if (!is_dir && join(full, sizeof(full), path, e->d_name) && stat(full, &st) == 0)
            item->size = (int64_t)st.st_size;
    }
    closedir(dir);
    if (b->count > 1)
        qsort(b->items, b->count, sizeof(BrowserEntry), compareEntries);
    if (at_root && b->network && grow(b, &capacity)) {  // First, above the card's folders.
        memmove(b->items + 1, b->items, b->count * sizeof(BrowserEntry));
        b->count++;
        BrowserEntry *item = &b->items[0];
        snprintf(item->name, sizeof(item->name), "%s", b->network_name);
        item->is_dir = item->is_network = true;
        item->size = -1;
    }
    return true;
}

void browserFree(Browser *b)
{
    free(b->items);
    b->items = NULL;
    b->count = 0;
    b->cursor = 0;
    b->free = false;
    b->first = b->offset = 0;
}

bool browserEnter(Browser *b)
{
    if (b->cursor >= b->count || !b->items[b->cursor].is_dir)
        return false;
    char path[BROWSER_PATH_MAX], previous[BROWSER_PATH_MAX], name[BROWSER_NAME_MAX];
    if (b->items[b->cursor].is_network)
        snprintf(path, sizeof(path), "%s", DAV_ROOT);
    else if (!join(path, sizeof(path), b->path, b->items[b->cursor].name))
        return false;
    snprintf(previous, sizeof(previous), "%s", b->path);
    snprintf(name, sizeof(name), "%s", b->items[b->cursor].name);
    if (browserOpen(b, path))
        return true;
    char problem[BROWSER_PROBLEM_MAX];  // Unreadable: stay where it was, saying why.
    snprintf(problem, sizeof(problem), "%s", b->problem);
    browserOpen(b, previous);
    browserSelect(b, name);
    snprintf(b->problem, sizeof(b->problem), "%s", problem);
    return false;
}

bool browserUp(Browser *b)
{
    if (strcmp(b->path, BROWSER_ROOT) == 0)
        return false;
    if (strcmp(b->path, DAV_ROOT) == 0) {  // Back to the card's top, on the network folder.
        browserOpen(b, BROWSER_ROOT);
        b->cursor = 0;
        return true;
    }
    char parent[BROWSER_PATH_MAX], child[BROWSER_NAME_MAX];
    snprintf(parent, sizeof(parent), "%s", b->path);
    char *slash = strrchr(parent, '/');
    if (!slash)
        return false;
    snprintf(child, sizeof(child), "%s", slash + 1);
    if (slash > parent && slash[-1] == ':')
        slash[1] = '\0';  // "sdmc:/Videos" -> "sdmc:/".
    else
        *slash = '\0';
    browserOpen(b, parent);
    browserSelect(b, child);
    return true;
}

bool browserSelectedPath(const Browser *b, char *out, size_t size)
{
    return browserEntryPath(b, b->cursor, out, size);
}

bool browserEntryPath(const Browser *b, size_t i, char *out, size_t size)
{
    if (i < b->count && b->items[i].is_network) {
        snprintf(out, size, "%s", DAV_ROOT);
        return true;
    }
    return i < b->count && join(out, size, b->path, b->items[i].name);
}

void browserSelect(Browser *b, const char *name)
{
    for (size_t i = 0; i < b->count; i++)
        if (strcmp(b->items[i].name, name) == 0) {
            b->cursor = i;
            b->free = false;  // Shown in the middle of the list.
            return;
        }
}

int browserFirst(const Browser *b, int rows)
{
    if (b->free)
        return b->first;
    int count = (int)b->count, cursor = (int)b->cursor;
    if (count <= rows || cursor < rows / 2)
        return 0;
    int first = cursor - rows / 2;
    return first + rows > count ? count - rows : first;
}

bool browserScrollPx(Browser *b, int px, int row_h, int rows)
{
    Scroll s = {(int)b->cursor, browserFirst(b, rows), b->free ? b->offset : 0};
    bool moved = scrollPx(&s, px, row_h, rows, 1, (int)b->count);
    b->free = true;
    b->first = s.first;
    b->offset = s.offset;
    b->cursor = (size_t)s.cursor;
    return moved;
}
