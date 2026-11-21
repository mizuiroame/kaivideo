// Choosing a video on the card: a folder's subfolders and video files, and
// moving between folders. Pure logic over the file system; main.c draws it.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dav.h"

#define BROWSER_PATH_MAX 512
#define BROWSER_NAME_MAX 256
#define BROWSER_ROOT "sdmc:/"

typedef struct {
    char name[BROWSER_NAME_MAX];
    bool is_dir;
    bool is_network;  // The card's top: the network folder's entry, opening DAV_ROOT.
    int64_t size;     // Bytes, for videos; -1 if unknown.
} BrowserEntry;

// Lists a network folder (davList's shape). False with why if it can't.
typedef bool (*BrowserLister)(const char *path, DavEntry **out, size_t *count, char *why, size_t why_size);

#define BROWSER_PROBLEM_MAX 192

typedef struct {
    char path[BROWSER_PATH_MAX];  // "sdmc:/" or "sdmc:/Videos/Series", no trailing slash but the root's.
    BrowserEntry *items;          // Folders first, then videos, each by name.
    size_t count;
    size_t cursor;
    // Where a finger left the list (free): first in view and pixels into it.
    // Not free, the list keeps the cursor in its middle.
    bool free;
    int first, offset;
    // Files sets these: with a lister, the card's top starts with the network
    // folder's entry (named so), and "dav:/" paths are listed through it.
    BrowserLister network;
    char network_name[BROWSER_NAME_MAX];
    char problem[BROWSER_PROBLEM_MAX];  // Why the last folder couldn't be listed; "" if it could.
} Browser;

// True for the files KaiVideo plays: .mp4, .m4v, .mkv and .webm.
bool browserIsVideo(const char *name);

// Lists path. False (and the browser left empty at that path, with problem
// set for a network folder) if it can't be read.
bool browserOpen(Browser *b, const char *path);
void browserFree(Browser *b);

// Opens the folder under the cursor. False if it isn't a folder or can't be read.
bool browserEnter(Browser *b);
// Goes to the parent folder, with the cursor on the folder it came from. False at the root.
bool browserUp(Browser *b);
// The full path of the entry under the cursor. False if there is none.
bool browserSelectedPath(const Browser *b, char *out, size_t size);
// The full path of entry i. False if there is none.
bool browserEntryPath(const Browser *b, size_t i, char *out, size_t size);
// Puts the cursor on the entry with this name, if there is one.
void browserSelect(Browser *b, const char *name);

// The first entry in view, and pixels into it, as the list shows (rows of
// row_h, rows of them in view).
int browserFirst(const Browser *b, int rows);
// A finger moves the list by px (as scrollPx does). False at an end.
bool browserScrollPx(Browser *b, int px, int row_h, int rows);
