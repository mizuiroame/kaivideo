// Small string helpers shared by the UI and the station lists.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define UTIL_PATH_MAX 769

// Case-insensitive ASCII comparison, for sorting names.
int utilCompareNames(const char *a, const char *b);
// Reads one UTF-8 code point and advances *s. Bad bytes read as '?'.
// Never reads past a terminating NUL.
unsigned utilNextCodePoint(const char **s);
// Formats a time of day: "21:47" or "9:47 PM".
void utilFormatClock(char *out, size_t size, int hour, int minute, bool use_24h);
// Formats seconds as m:ss or h:mm:ss.
void utilFormatTime(char *out, size_t size, double seconds);
// Percent-encodes text for a URL's query. False if it doesn't fit.
bool utilUrlEscape(const char *text, char *out, size_t size);
// True if path's file name ends in "." ext (any case).
bool utilHasExtension(const char *path, const char *ext);
// The part of path after its last '/'.
const char *utilFileName(const char *path);
// dir + "/" + name into out. False if it doesn't fit.
bool utilJoinPath(char *out, size_t size, const char *dir, const char *name);
