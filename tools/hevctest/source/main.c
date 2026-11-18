// KaiVideo HEVC test: can the console's CPU decode HEVC fast enough to play it?
// For the HEVC videos in sdmc:/kaivideo-test/hevc and sdmc:/movies, decodes a
// stretch with FFmpeg using more and more threads, and reports the speed
// against the video's own. Uses only the CPU and the card: it doesn't touch
// the hardware video decoder. Results also go to sdmc:/kaivideo-hevctest.txt.
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include <switch.h>

#include "bench.h"

#define LOG_PATH "sdmc:/kaivideo-hevctest.txt"
#define MAX_FILES 5
#define PATH_MAX_LEN 512
#define RUN_SECONDS 12.0
#define RUN_FRAMES 720
#define COMFORT_MARGIN 1.25   // Spare speed wanted for sound, drawing and busy scenes.

static const char *const FOLDERS[] = {"sdmc:/kaivideo-test/hevc", "sdmc:/movies"};

static FILE *g_log;
static PadState g_pad;
static char g_files[MAX_FILES][PATH_MAX_LEN];
static BenchInfo g_infos[MAX_FILES];
static int g_file_count;

static void say(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    if (g_log) {
        va_start(args, fmt);
        vfprintf(g_log, fmt, args);
        va_end(args);
        fflush(g_log);
    }
    consoleUpdate(NULL);
}

static uint64_t nowNs(void)
{
    return armTicksToNs(armGetSystemTick());
}

static bool plusPressed(void *data)
{
    (void)data;
    padUpdate(&g_pad);
    return (padGetButtonsDown(&g_pad) & HidNpadButton_Plus) != 0;
}

static bool isVideo(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot && (!strcasecmp(dot, ".mp4") || !strcasecmp(dot, ".mkv") || !strcasecmp(dot, ".webm"));
}

static void addIfHevc(const char *path)
{
    BenchInfo info;
    char error[96];
    if (g_file_count == MAX_FILES || !benchProbe(path, &info, error, sizeof(error)) || !info.is_hevc)
        return;
    snprintf(g_files[g_file_count], PATH_MAX_LEN, "%s", path);
    g_infos[g_file_count++] = info;
}

// The folder's videos, and those of the folders right inside it.
static void scan(const char *folder, int depth)
{
    DIR *dir = opendir(folder);
    if (!dir)
        return;
    struct dirent *e;
    char path[PATH_MAX_LEN];
    while ((e = readdir(dir)) != NULL && g_file_count < MAX_FILES) {
        if (e->d_name[0] == '.' || snprintf(path, sizeof(path), "%s/%s", folder, e->d_name) >= (int)sizeof(path))
            continue;
        struct stat st;
        if (stat(path, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth > 0)
                scan(path, depth - 1);
        } else if (isVideo(e->d_name)) {
            addIfHevc(path);
        }
    }
    closedir(dir);
}

static int coreCount(void)
{
    u64 mask = 0;
    if (R_FAILED(svcGetInfo(&mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0)))
        return 1;
    return __builtin_popcountll(mask);
}

// False if the test was stopped.
static bool testFile(const char *path, const BenchInfo *info, const int *threads, int thread_steps)
{
    say("\n== %s\n   %dx%d, %d-bit %s, %.2f fps, %.0f min\n", path, info->width, info->height, info->bits,
        info->profile, info->fps, info->duration_s / 60);
    for (int i = 0; i < thread_steps; i++) {
        BenchResult r;
        char error[96] = "";
        say("   %d thread%s: ", threads[i], threads[i] == 1 ? "" : "s");
        bool ok = benchRun(path, threads[i], RUN_SECONDS, RUN_FRAMES, nowNs, plusPressed, NULL, &r, error, sizeof(error));
        if (!ok) {
            say("FAILED: %s\n", error);
            continue;
        }
        if (r.cancelled) {
            say("stopped\n");
            return false;
        }
        double speed = info->fps > 0 ? r.average_fps / info->fps : 0;
        say("%.1f fps (%.2fx), slowest %.1f, convert %.1f ms, %s, %d frames%s\n", r.average_fps, speed, r.slowest_fps,
            r.convert_ms, r.frame_threads ? "frame threads" : "slice threads", r.frames,
            speed >= COMFORT_MARGIN && r.slowest_fps >= info->fps ? "  OK" : "");
    }
    return true;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&g_pad);
    g_log = fopen(LOG_PATH, "w");
    int cores = coreCount();
    say("KaiVideo HEVC test: software decoding with FFmpeg. %d CPU cores for this app.\n", cores);
    say("OK = at least %.2fx the video's speed, and never below it. + stops.\n", COMFORT_MARGIN);
    say("Looking for HEVC videos...\n");
    for (size_t i = 0; i < sizeof(FOLDERS) / sizeof(FOLDERS[0]); i++)
        scan(FOLDERS[i], 1);

    int threads[4] = {1, 2, 3, cores};
    int steps = cores > 3 ? 4 : cores;
    bool go_on = true;
    if (g_file_count == 0)
        say("\nNo HEVC videos found.\n");
    for (int i = 0; i < g_file_count && go_on; i++)
        go_on = testFile(g_files[i], &g_infos[i], threads, steps);

    say("\nDone. Results are in " LOG_PATH ". Press + to exit.\n");
    while (appletMainLoop()) {
        if (plusPressed(NULL))
            break;
        consoleUpdate(NULL);
        svcSleepThread(50000000ull);
    }
    if (g_log)
        fclose(g_log);
    consoleExit(NULL);
    return 0;
}
