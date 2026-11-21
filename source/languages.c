#include "languages.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lang.h"
#include "util.h"

// data/lang_*.txt, in the app.
#include "lang_de_txt.h"
#include "lang_es_txt.h"
#include "lang_fr_txt.h"
#include "lang_it_txt.h"
#include "lang_ja_txt.h"
#include "lang_ko_txt.h"
#include "lang_pt_txt.h"
#include "lang_zh_txt.h"

#define FILE_MAX (1024 * 1024)  // A language file larger than this isn't one.
#define PATH_MAX_LEN 600
#define NAME_KEY "language"

typedef struct {
    char id[LANGUAGE_ID_MAX];      // "en", "es", or a file's name.
    char name[LANG_NAME_MAX];
    char path[PATH_MAX_LEN];       // A file's, "" for those in the app.
    const uint8_t *text;           // One in the app: its text.
    size_t size;
} Language;

// The languages in the app, in the order Settings shows them (after English).
static const struct {
    const char *id;
    const uint8_t *text;
    size_t size;
} BUILT_IN[] = {
    {"es", lang_es_txt, lang_es_txt_size}, {"fr", lang_fr_txt, lang_fr_txt_size},
    {"it", lang_it_txt, lang_it_txt_size}, {"pt", lang_pt_txt, lang_pt_txt_size},
    {"de", lang_de_txt, lang_de_txt_size}, {"ja", lang_ja_txt, lang_ja_txt_size},
    {"zh", lang_zh_txt, lang_zh_txt_size}, {"ko", lang_ko_txt, lang_ko_txt_size},
};

static Language g_list[LANGUAGES_MAX];
static int g_count;

// The "language = name" line of a language file's text, into name. False if it has none.
static bool nameIn(const char *text, size_t size, char *name, size_t name_size)
{
    const char *end = text + size;
    for (const char *line = text; line < end;) {
        const char *eol = memchr(line, '\n', (size_t)(end - line));
        size_t len = eol ? (size_t)(eol - line) : (size_t)(end - line);
        if (len > strlen(NAME_KEY) && !strncmp(line, NAME_KEY, strlen(NAME_KEY))) {
            const char *eq = memchr(line, '=', len);
            if (!eq)
                return false;
            const char *v = eq + 1;
            while (v < line + len && *v == ' ')
                v++;
            size_t n = (size_t)(line + len - v);
            while (n && (v[n - 1] == '\r' || v[n - 1] == ' '))
                n--;
            snprintf(name, name_size, "%.*s", (int)n, v);
            return n > 0;
        }
        line = eol ? eol + 1 : end;
    }
    return false;
}

// A file's whole text (free it), or NULL.
static char *readFile(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    char *text = f ? malloc(FILE_MAX) : NULL;
    *size = text ? fread(text, 1, FILE_MAX, f) : 0;
    if (f)
        fclose(f);
    if (text && (*size == 0 || *size == FILE_MAX)) {
        free(text);
        text = NULL;
    }
    return text;
}

static void add(const char *id, const char *name, const char *path, const uint8_t *text, size_t size)
{
    if (g_count >= LANGUAGES_MAX)
        return;
    Language *l = &g_list[g_count++];
    snprintf(l->id, sizeof(l->id), "%s", id);
    snprintf(l->name, sizeof(l->name), "%s", name);
    snprintf(l->path, sizeof(l->path), "%s", path);
    l->text = text;
    l->size = size;
}

int languagesFind(const char *folder)
{
    g_count = 0;
    add("en", "English", "", NULL, 0);
    char name[LANG_NAME_MAX];
    for (size_t i = 0; i < sizeof(BUILT_IN) / sizeof(BUILT_IN[0]); i++)
        if (nameIn((const char *)BUILT_IN[i].text, BUILT_IN[i].size, name, sizeof(name)))
            add(BUILT_IN[i].id, name, "", BUILT_IN[i].text, BUILT_IN[i].size);
    DIR *dir = folder ? opendir(folder) : NULL;
    struct dirent *e;
    while (dir && (e = readdir(dir)) != NULL) {
        char path[PATH_MAX_LEN];
        size_t size;
        if (!utilHasExtension(e->d_name, "txt") || strlen(e->d_name) >= LANGUAGE_ID_MAX ||
            !utilJoinPath(path, sizeof(path), folder, e->d_name))
            continue;
        char *text = readFile(path, &size);
        if (text && nameIn(text, size, name, sizeof(name)))
            add(e->d_name, name, path, NULL, 0);  // Its id is its file's name: "fr.txt".
        free(text);
    }
    if (dir)
        closedir(dir);
    return g_count;
}

int languagesCount(void)
{
    return g_count;
}

const char *languageId(int index)
{
    return index >= 0 && index < g_count ? g_list[index].id : "en";
}

const char *languageName(int index)
{
    return index >= 0 && index < g_count ? g_list[index].name : "English";
}

int languageIndex(const char *id)
{
    for (int i = 0; i < g_count; i++)
        if (!strcmp(g_list[i].id, id))
            return i;
    return 0;
}

bool languageUse(const char *id)
{
    int i = languageIndex(id);
    if (i == 0 || i >= g_count) {
        langUseEnglish();
        return i == 0 && !strcmp(id, "en");
    }
    const Language *l = &g_list[i];
    if (l->text)  // One in the app.
        return langLoadText((const char *)l->text, l->size) || (langUseEnglish(), false);
    size_t size;
    char *text = readFile(l->path, &size);
    bool ok = text && langLoadText(text, size);
    free(text);
    if (!ok)
        langUseEnglish();
    return ok;
}
