#include "libscan.h"

#include <string.h>

#include <switch.h>

#define STACK_SIZE 0x20000
#define PRIORITY 0x2C

static Thread g_thread;
static bool g_started;           // The thread exists (running or done, not yet joined).
static volatile bool g_done;
static volatile bool g_cancel;
static bool g_ok;
static Library g_result;

static void scanThread(void *arg)
{
    (void)arg;
    g_ok = libraryScan(&g_result, &g_cancel);
    g_done = true;
}

static void join(void)
{
    if (!g_started)
        return;
    threadWaitForExit(&g_thread);
    threadClose(&g_thread);
    g_started = false;
}

bool libscanStart(const char *root)
{
    if (g_started)
        return false;
    libraryInit(&g_result, root);
    g_done = g_cancel = false;
    if (R_FAILED(threadCreate(&g_thread, scanThread, NULL, NULL, STACK_SIZE, PRIORITY, -2)))
        return false;
    if (R_FAILED(threadStart(&g_thread))) {
        threadClose(&g_thread);
        return false;
    }
    g_started = true;
    return true;
}

bool libscanBusy(size_t *found)
{
    if (found)
        *found = g_result.count;  // Read while it grows: only a count to show.
    return g_started && !g_done;
}

bool libscanTake(Library *out, bool *ok)
{
    if (!g_started || !g_done)
        return false;
    join();
    *out = g_result;
    *ok = g_ok;
    memset(&g_result, 0, sizeof(g_result));
    return true;
}

void libscanStop(void)
{
    g_cancel = true;
    join();
    libraryFree(&g_result);
}
