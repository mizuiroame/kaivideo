// Rewraps a video into MKV without decoding it: the same tracks and packets,
// with an index of its keyframes (cues) so jumps land on them. For a server's
// conversion downloaded as MPEG-TS parts: its times are moved so the picture
// starts at 0, and packets a resumed conversion sent twice are left out.
#pragma once

#include <stdbool.h>
#include <stddef.h>

// Told how far it has come (0 to 1) now and then.
typedef void (*RemuxProgress)(void *user, double fraction);

// Each step said as it begins (the app's log), to find where it stops. NULL: quiet.
typedef void (*RemuxLog)(const char *fmt, ...);
void remuxSetLog(RemuxLog log);

// Writes in_path's tracks into out_path (made anew). Stops, deleting
// out_path, when *cancel (may be NULL) becomes true. False, with why in
// error, if it can't be done.
bool remuxToMkv(const char *in_path, const char *out_path, const volatile bool *cancel, RemuxProgress progress,
                void *user, char *error, size_t error_size);
// The same on a thread of its own with a 2 MB stack (FFmpeg's demuxers and
// decoders go deep), waited for.
bool remuxToMkvOnOwnThread(const char *in_path, const char *out_path, const volatile bool *cancel, char *error,
                           size_t error_size);
