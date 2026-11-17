#include "approot.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define MARK_NAME ".moved"
#define COPY_BUFFER (64 * 1024)

bool appRootFolder(const char *nro_path, const char *app, char *out, size_t size)
{
    const char *slash = nro_path ? strrchr(nro_path, '/') : NULL;
    if (!nro_path || strncmp(nro_path, "sdmc:/", 6) != 0 || !slash || strstr(nro_path, "..")) {
        snprintf(out, size, "sdmc:/config/%s", app);
        return false;
    }
    char folder[APPROOT_PATH_MAX];
    size_t len = (size_t)(slash - nro_path);
    if (len >= sizeof(folder))
        len = sizeof(folder) - 1;
    memcpy(folder, nro_path, len);
    folder[len] = '\0';  // "sdmc:" for an NRO at the root.
    // Is the NRO's folder the app's own ("…/kaivideo")? Else it gets one there.
    const char *name = strrchr(folder, '/');
    name = name ? name + 1 : folder;
    int n = strcasecmp(name, app) == 0 ? snprintf(out, size, "%s/config", folder)
                                       : snprintf(out, size, "%s/%s/config", folder, app);
    if (n < 0 || (size_t)n >= size) {
        snprintf(out, size, "sdmc:/config/%s", app);
        return false;
    }
    return true;
}

bool appRootMakeFolder(const char *folder)
{
    char path[APPROOT_PATH_MAX];
    snprintf(path, sizeof(path), "%s", folder);
    for (char *p = strchr(path, '/'); p; p = strchr(p + 1, '/')) {
        if (p > path && p[-1] == ':')
            continue;  // "sdmc:/" itself.
        *p = '\0';
        mkdir(path, 0777);
        *p = '/';
    }
    mkdir(path, 0777);
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static bool copyFile(const char *from, const char *to)
{
    FILE *in = fopen(from, "rb");
    if (!in)
        return false;
    char temp[APPROOT_PATH_MAX + 8];
    snprintf(temp, sizeof(temp), "%s.part", to);
    FILE *out = fopen(temp, "wb");
    bool ok = out != NULL;
    static char buffer[COPY_BUFFER];
    size_t n;
    while (ok && (n = fread(buffer, 1, sizeof(buffer), in)) > 0)
        ok = fwrite(buffer, 1, n, out) == n;
    ok = ok && !ferror(in);
    fclose(in);
    if (out)
        ok = fclose(out) == 0 && ok;
    if (ok)
        ok = rename(temp, to) == 0;
    if (!ok)
        remove(temp);
    return ok;
}

// Copies old's files that folder hasn't, and (depth 1) its subfolders'.
static int copyMissing(const char *old_folder, const char *folder, int depth)
{
    DIR *dir = opendir(old_folder);
    if (!dir)
        return 0;
    int copied = 0;
    struct dirent *e;
    char from[APPROOT_PATH_MAX], to[APPROOT_PATH_MAX];
    while ((e = readdir(dir)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        if (snprintf(from, sizeof(from), "%s/%s", old_folder, e->d_name) >= (int)sizeof(from) ||
            snprintf(to, sizeof(to), "%s/%s", folder, e->d_name) >= (int)sizeof(to))
            continue;
        struct stat st;
        if (stat(from, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth > 0) {
                mkdir(to, 0777);
                copied += copyMissing(from, to, depth - 1);
            }
        } else if (!exists(to) && copyFile(from, to)) {
            copied++;
        }
    }
    closedir(dir);
    return copied;
}

int appRootMigrate(const char *old_folder, const char *folder)
{
    char mark[APPROOT_PATH_MAX];
    if (strcmp(old_folder, folder) == 0 || snprintf(mark, sizeof(mark), "%s/" MARK_NAME, folder) >= (int)sizeof(mark) ||
        exists(mark))
        return 0;
    int copied = copyMissing(old_folder, folder, 1);
    FILE *f = fopen(mark, "w");
    if (f) {
        fprintf(f, "Files copied from %s (still there).\n", old_folder);
        fclose(f);
    }
    return copied;
}
