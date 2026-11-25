// Loads the IPTV list set in Settings, and its guide, on a thread of its
// own: an M3U playlist, or an Xtream account's live channels (and its
// films and series, without their episodes: xtreamvod.h); then the
// XMLTV guide, read as it downloads. One load at a time; a new one replaces it.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "iptv.h"
#include "xmltv.h"
#include "xtreamvod.h"

#define IPTV_GUIDE_BEFORE_S (2 * 3600)  // Programs kept from this long ago,
#define IPTV_GUIDE_AFTER_S (24 * 3600)  // to this far ahead,
#define IPTV_GUIDE_DESC_S (6 * 3600)    // with their descriptions to this far ahead.

typedef enum {
    IPTV_LOAD_IDLE,
    IPTV_LOAD_LIST,     // Asking for the channels.
    IPTV_LOAD_GUIDE,    // The channels came; reading the guide.
    IPTV_LOAD_DONE,     // Both (the guide may have failed: guide_error).
    IPTV_LOAD_FAILED,   // The channels couldn't be had: error.
} IptvLoadState;

typedef struct {
    IptvLoadState state;
    unsigned serial;            // Changes with each load.
    char error[256];            // Why the channels couldn't be had, in the language in use.
    char guide_error[256];      // Why the guide couldn't be read; "" if it was (or there's none).
    int guide_programs;         // Programs the guide had for the list's channels.
    bool has_guide;             // There's a guide to read.
} IptvLoadStatus;

// What a channel is on: its programs, by start.
typedef struct {
    LiveProgram (*programs)[XMLTV_PER_CHANNEL];  // One row per channel of the list.
    char **descs;  // Program j of channel i's description at [i * XMLTV_PER_CHANNEL + j], or NULL.
    int *counts;
    int count;
} IptvGuide;

void iptvGuideFree(IptvGuide *g);

void iptvLoadStart(const IptvAccount *a, int64_t now_s);
void iptvLoadStatus(IptvLoadStatus *out);
// Moves a finished load's list, guide and films and series to the caller (who frees them).
// False if there's none new since the last take.
bool iptvLoadTake(IptvList *list, IptvGuide *guide, XtreamCatalog *catalog);
// Stops a load under way, and waits for it.
void iptvLoadExit(void);
