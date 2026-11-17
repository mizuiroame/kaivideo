#include "resume.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void resumeLoad(ResumeList *r, const char *file)
{
    r->count = 0;
    FILE *f = fopen(file, "r");
    if (!f)
        return;
    char line[RESUME_PATH_MAX + 64];
    while (r->count < RESUME_MAX && fgets(line, sizeof(line), f)) {
        if (!strchr(line, '\n') && !feof(f)) {  // Too long: skip the whole line.
            int ch;
            while ((ch = fgetc(f)) != EOF && ch != '\n') {
            }
            continue;
        }
        line[strcspn(line, "\r\n")] = '\0';
        // "position<TAB>duration<TAB>path", in milliseconds.
        char *tab1 = strchr(line, '\t'), *tab2 = tab1 ? strchr(tab1 + 1, '\t') : NULL;
        if (!tab2 || !tab2[1] || strlen(tab2 + 1) >= RESUME_PATH_MAX)
            continue;
        ResumeEntry *e = &r->items[r->count];
        e->position_ns = strtoll(line, NULL, 10) * 1000000;
        e->duration_ns = strtoll(tab1 + 1, NULL, 10) * 1000000;
        snprintf(e->path, sizeof(e->path), "%s", tab2 + 1);
        if (e->position_ns > 0)
            r->count++;
    }
    fclose(f);
}

bool resumeSave(const ResumeList *r, const char *file)
{
    FILE *f = fopen(file, "w");
    if (!f)
        return false;
    for (size_t i = 0; i < r->count; i++)
        fprintf(f, "%" PRId64 "\t%" PRId64 "\t%s\n", r->items[i].position_ns / 1000000,
                r->items[i].duration_ns / 1000000, r->items[i].path);
    return fclose(f) == 0;
}

static int indexOf(const ResumeList *r, const char *path)
{
    for (size_t i = 0; i < r->count; i++)
        if (strcmp(r->items[i].path, path) == 0)
            return (int)i;
    return -1;
}

bool resumeFind(const ResumeList *r, const char *path, int64_t *position_ns, int64_t *duration_ns)
{
    int i = indexOf(r, path);
    if (i < 0)
        return false;
    *position_ns = r->items[i].position_ns;
    if (duration_ns)
        *duration_ns = r->items[i].duration_ns;
    return true;
}

void resumeRemember(ResumeList *r, const char *path, int64_t position_ns, int64_t duration_ns)
{
    int i = indexOf(r, path);
    if (i >= 0) {  // Out of its place: it goes back at the top, or nowhere.
        memmove(&r->items[i], &r->items[i + 1], (r->count - (size_t)i - 1) * sizeof(ResumeEntry));
        r->count--;
    }
    bool near_end = duration_ns > 0 && position_ns > duration_ns - RESUME_END_MARGIN_NS;
    if (position_ns < RESUME_MIN_NS || near_end || strlen(path) >= RESUME_PATH_MAX || strpbrk(path, "\r\n"))
        return;
    if (r->count == RESUME_MAX)
        r->count--;  // The oldest goes.
    memmove(&r->items[1], &r->items[0], r->count * sizeof(ResumeEntry));
    r->count++;
    ResumeEntry *e = &r->items[0];
    snprintf(e->path, sizeof(e->path), "%s", path);
    e->position_ns = position_ns;
    e->duration_ns = duration_ns;
}
