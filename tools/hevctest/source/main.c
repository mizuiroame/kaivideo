// KaiVideo HEVC test: can the console's CPU decode HEVC fast enough to play it?
// For the HEVC videos in sdmc:/kaivideo-test/hevc and sdmc:/movies, decodes a
// stretch with FFmpeg using more and more threads, and reports the speed
// against the video's own; then with more threads than cores, and without
// deblocking the frames nothing refers to. Uses only the CPU and the card: it
// doesn't touch the hardware video decoder. Results also go to
// sdmc:/<this app's name>.txt (the Makefile's TARGET), so two builds, made
// with two FFmpeg images, keep their own results.
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include <switch.h>

#include "bench.h"
#include "seekbench.h"

#ifndef BENCH_NAME
#define BENCH_NAME "kaivideo-hevctest"
#endif
#define LOG_PATH "sdmc:/" BENCH_NAME ".txt"
#define MAX_STEPS 6
#define MAX_FILES 5
#define PATH_MAX_LEN 512
#define RUN_SECONDS 12.0
#define RUN_FRAMES 720
#define COMFORT_MARGIN 1.25   // Spare speed wanted for sound, drawing and busy scenes.
#define WAIT_NS 50000000ull
// What KaiVideo's sound side allows when it opens a file (source/audio.c).
#define AUDIO_DECODERS "aac,mp3float,mp3,vorbis,opus,flac,ac3,eac3,dca,truehd,mlp"
#define VIDEO_DECODERS "hevc,h264"

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

typedef struct {
    int threads;
    bool skip_nonref_deblock;
} Step;

// False if the test was stopped.
static bool testFile(const char *path, const BenchInfo *info, const Step *steps, int step_count)
{
    say("\n== %s\n   %dx%d, %d-bit %s, %.2f fps, %.0f min\n", path, info->width, info->height, info->bits,
        info->profile, info->fps, info->duration_s / 60);
    for (int i = 0; i < step_count; i++) {
        BenchResult r;
        char error[96] = "";
        say("   %d thread%s%s: ", steps[i].threads, steps[i].threads == 1 ? "" : "s",
            steps[i].skip_nonref_deblock ? ", no deblocking on frames nothing refers to" : "");
        bool ok = benchRun(path, steps[i].threads, steps[i].skip_nonref_deblock, RUN_SECONDS, RUN_FRAMES, nowNs,
                           plusPressed, NULL, &r, error, sizeof(error));
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

static void reportOpen(const char *path, const char *label, const char *decoders)
{
    SeekOpenResult r;
    char error[96] = "";
    say("   open, %s: ", label);
    if (seekbenchOpen(path, decoders, nowNs, &r, error, sizeof(error)))
        say("headers %.0f ms, stream info %.0f ms, index %.0f ms\n", r.open_ms, r.info_ms, r.index_ms);
    else
        say("FAILED: %s\n", error);
}

// False if the test was stopped.
static bool seekTestFile(const char *path, const BenchInfo *info, const int *threads, int thread_steps)
{
    say("\n== %s\n   %dx%d, %d-bit, %.2f fps, %.0f min\n", path, info->width, info->height, info->bits, info->fps,
        info->duration_s / 60);
    reportOpen(path, "all decoders (the picture today)", NULL);
    reportOpen(path, "sound decoders (the sound today)", AUDIO_DECODERS);
    reportOpen(path, "video decoders only", VIDEO_DECODERS);
    for (int i = 0; i < thread_steps; i++) {
        SeekRunResult r;
        char error[96] = "";
        say("   seek, %d threads: ", threads[i]);
        if (!seekbenchRun(path, threads[i], nowNs, plusPressed, NULL, &r, error, sizeof(error))) {
            say("FAILED: %s\n", error);
            continue;
        }
        if (r.cancelled) {
            say("stopped\n");
            return false;
        }
        say("first picture %.0f ms, target %.0f ms, slowest %.0f ms, %.0f packets (%d seeks)\n", r.first_ms, r.target_ms,
            r.worst_ms, r.packets, r.seeks);
    }
    return true;
}

// True for the seek test (X), false for the decoding one (A).
static bool chooseSeekTest(void)
{
    say("A: decoding speed.  X: opening and seeking.\n");
    while (appletMainLoop()) {
        padUpdate(&g_pad);
        u64 down = padGetButtonsDown(&g_pad);
        if (down & HidNpadButton_X)
            return true;
        if (down & HidNpadButton_A)
            break;
        consoleUpdate(NULL);
        svcSleepThread(WAIT_NS);
    }
    return false;
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
    say("KaiVideo HEVC test (" BENCH_NAME "): software decoding with FFmpeg. %d CPU cores for this app.\n", cores);
    say("OK = at least %.2fx the video's speed, and never below it. + stops.\n", COMFORT_MARGIN);
    bool seek_test = chooseSeekTest();
    say("Looking for HEVC videos...\n");
    for (size_t i = 0; i < sizeof(FOLDERS) / sizeof(FOLDERS[0]); i++)
        scan(FOLDERS[i], 1);

    // 1, 2, 3 and all cores; then half as many threads again as cores (frame
    // threads wait on each other's pictures), and all cores without
    // deblocking the frames nothing refers to.
    Step steps[MAX_STEPS];
    int step_count = 0;
    for (int t = 1; t <= 3 && t < cores; t++)
        steps[step_count++] = (Step){t, false};
    steps[step_count++] = (Step){cores, false};
    if (cores > 1)
        steps[step_count++] = (Step){cores + cores / 2, false};
    steps[step_count++] = (Step){cores, true};
    int seek_threads[2] = {cores < 4 ? cores : 4, cores};
    int seek_steps = cores > 4 ? 2 : 1;
    bool go_on = true;
    if (g_file_count == 0)
        say("\nNo HEVC videos found.\n");
    for (int i = 0; i < g_file_count && go_on; i++)
        go_on = seek_test ? seekTestFile(g_files[i], &g_infos[i], seek_threads, seek_steps)
                          : testFile(g_files[i], &g_infos[i], steps, step_count);

    say("\nDone. Results are in " LOG_PATH ". Press + to exit.\n");
    while (appletMainLoop()) {
        if (plusPressed(NULL))
            break;
        consoleUpdate(NULL);
        svcSleepThread(WAIT_NS);
    }
    if (g_log)
        fclose(g_log);
    consoleExit(NULL);
    return 0;
}
