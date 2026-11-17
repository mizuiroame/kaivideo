// Where each video was left, to go on from there: one
// "position<TAB>duration<TAB>path" line (milliseconds) per video in
// sdmc:/config/kaivideo/resume.txt, the most recent first, at most
// RESUME_MAX of them. Pure logic over a file.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RESUME_MAX 200
#define RESUME_PATH_MAX 512
#define RESUME_MIN_NS 30000000000ll        // Less than this in, it starts over.
#define RESUME_END_MARGIN_NS 30000000000ll // Less than this left, it's watched: it starts over.

typedef struct {
    char path[RESUME_PATH_MAX];
    int64_t position_ns;
    int64_t duration_ns;  // 0 if unknown; for the list's progress bar.
} ResumeEntry;

typedef struct {
    ResumeEntry items[RESUME_MAX];
    size_t count;
} ResumeList;

// Reads file into r (empty if there's none).
void resumeLoad(ResumeList *r, const char *file);
bool resumeSave(const ResumeList *r, const char *file);

// Where path was left, or false to start from the beginning.
bool resumeFind(const ResumeList *r, const char *path, int64_t *position_ns, int64_t *duration_ns);
// Remembers that path was left at position (of duration), the most recent
// first; or forgets it if it's near the start or the end.
void resumeRemember(ResumeList *r, const char *path, int64_t position_ns, int64_t duration_ns);
