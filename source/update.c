#include "update.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define CARD "sdmc:/"
#define BREWSTORE "sdmc:/brewstore/apps/"
#define NRO_EXT ".nro"
#define VERSION_PART_MAX 100000L  // A version number past this isn't a real one.

UpdatePlace updatePlaceOf(const char *nro_path)
{
    if (!nro_path || strncmp(nro_path, CARD, sizeof(CARD) - 1) != 0 || strstr(nro_path, "/..") ||
        strstr(nro_path, "/./"))
        return UPDATE_PLACE_UNKNOWN;
    size_t length = strlen(nro_path), ext = sizeof(NRO_EXT) - 1;
    if (length <= ext || strcasecmp(nro_path + length - ext, NRO_EXT) != 0)
        return UPDATE_PLACE_UNKNOWN;
    return strncmp(nro_path, BREWSTORE, sizeof(BREWSTORE) - 1) == 0 ? UPDATE_PLACE_BREWSTORE : UPDATE_PLACE_LOOSE;
}

bool updateDue(int64_t last_check, int64_t now)
{
    return last_check <= 0 || now < last_check || now - last_check >= UPDATE_EVERY_S;
}

// "1.13.0" (after an optional 'v') into its three numbers. False for anything else.
static bool parseVersion(const char *text, long out[3])
{
    if (!text)
        return false;
    if (*text == 'v' || *text == 'V')
        text++;
    for (int i = 0; i < 3; i++) {
        if (!isdigit((unsigned char)*text))
            return false;
        char *end;
        long n = strtol(text, &end, 10);
        if (n < 0 || n > VERSION_PART_MAX || end - text > 6)
            return false;
        out[i] = n;
        if (i < 2 && *end != '.')
            return false;
        text = end + (i < 2);
    }
    return *text == '\0';  // "-rc1" and the like: not a release offered.
}

bool updateIsNewer(const char *tag, const char *current)
{
    long a[3], b[3];
    if (!parseVersion(tag, a) || !parseVersion(current, b))
        return false;
    for (int i = 0; i < 3; i++) {
        if (a[i] != b[i])
            return a[i] > b[i];
    }
    return false;
}

bool updateDigest(const char *digest, char out[UPDATE_HEX_SIZE])
{
    static const char PREFIX[] = "sha256:";
    if (!digest || strncmp(digest, PREFIX, sizeof(PREFIX) - 1) != 0)
        return false;
    const char *hex = digest + sizeof(PREFIX) - 1;
    if (strlen(hex) != UPDATE_HEX_SIZE - 1)
        return false;
    for (int i = 0; i < UPDATE_HEX_SIZE - 1; i++) {
        if (!isxdigit((unsigned char)hex[i]))
            return false;
        out[i] = (char)tolower((unsigned char)hex[i]);
    }
    out[UPDATE_HEX_SIZE - 1] = '\0';
    return true;
}

bool updateUrlOk(const char *url, const char *repo, const char *tag, const char *name)
{
    if (!url || !repo || !tag || !name)
        return false;
    char expected[UPDATE_URL_MAX];
    int n = snprintf(expected, sizeof(expected), "https://github.com/%s/%s/releases/download/%s/%s", UPDATE_OWNER,
                     repo, tag, name);
    return n > 0 && (size_t)n < sizeof(expected) && strcmp(url, expected) == 0;
}

static bool withEnding(const char *path, const char *ending, char *out, size_t size)
{
    int n = snprintf(out, size, "%s%s", path, ending);
    return n > 0 && (size_t)n < size;
}

bool updateNewPath(const char *nro_path, char *out, size_t size)
{
    return withEnding(nro_path, ".new", out, size);
}

bool updateOldPath(const char *nro_path, char *out, size_t size)
{
    return withEnding(nro_path, ".old", out, size);
}

static bool exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f)
        fclose(f);
    return f != NULL;
}

bool updateSwap(const char *nro_path, const char *new_path)
{
    char old[UPDATE_PATH_MAX];
    if (!updateOldPath(nro_path, old, sizeof(old)) || !exists(new_path))
        return false;
    remove(old);  // Left by a swap that stopped part way.
    if (rename(nro_path, old) != 0)
        return false;
    if (rename(new_path, nro_path) != 0) {
        rename(old, nro_path);  // Put back.
        return false;
    }
    remove(old);
    return true;
}
