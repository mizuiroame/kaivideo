// Questions about the network asked from Settings, on a thread of their own:
// testing a server's address step by step, and finding servers on the network.
// One at a time.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "jellyfin.h"

#define JF_PROBE_BROADCAST "255.255.255.255"
#define JF_PROBE_FOUND_MAX 4
#define JF_PROBE_MESSAGE_MAX (JF_VERDICT_MAX + JF_SERVER_MAX + JF_NAME_MAX + 64)

typedef struct {
    bool finding;                     // A search, not a test.
    bool ok;                          // The server answered, or one was found.
    char message[JF_PROBE_MESSAGE_MAX];
    char found[JF_SERVER_MAX];        // A search: the first server found.
    char detail[96];                  // A search: what was asked, for the log.
} JfProbeResult;

// Asks server for /System/Info/Public. False if a question is under way.
bool jfProbeStartTest(const char *server);
// Asks the network (target: JF_PROBE_BROADCAST, or one address) who runs Jellyfin.
bool jfProbeStartFind(const char *target);
bool jfProbeRunning(void);
// Which kind of server the next test or search is for (Jellyfin at first).
void jfProbeSetKind(ServerKind kind);
// The answer, once, when it's there.
bool jfProbeTake(JfProbeResult *out);
