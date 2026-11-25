// Host tests for KaiVideo's pure logic: make -C tests run
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "browser.h"
#include "check.h"
#include "resume.h"

int g_failures;
int g_checks;

void testResume(void)
{
    static ResumeList r;
    const char *file = TMP_ROOT "/resume.txt";
    resumeLoad(&r, file);  // No file yet.
    CHECK(r.count == 0);

    const int64_t S = 1000000000;
    resumeRemember(&r, "sdmc:/a.mp4", 95 * S, 600 * S);
    resumeRemember(&r, "sdmc:/b.mkv", 10 * S, 600 * S);   // Less than 30 s in: not kept.
    resumeRemember(&r, "sdmc:/c.webm", 590 * S, 600 * S); // Less than 30 s left: watched.
    resumeRemember(&r, "sdmc:/d.mp4", 40 * S, 0);         // Unknown length: kept.
    CHECK(r.count == 2 && strcmp(r.items[0].path, "sdmc:/d.mp4") == 0);  // The most recent first.
    int64_t pos = 0, len = 0;
    CHECK(resumeFind(&r, "sdmc:/a.mp4", &pos, &len) && pos == 95 * S && len == 600 * S);
    CHECK(!resumeFind(&r, "sdmc:/b.mkv", &pos, NULL));

    // Watching it to the end forgets it; going back to one moves it to the top.
    resumeRemember(&r, "sdmc:/a.mp4", 120 * S, 600 * S);
    CHECK(strcmp(r.items[0].path, "sdmc:/a.mp4") == 0 && r.count == 2);
    resumeRemember(&r, "sdmc:/d.mp4", 0, 0);
    CHECK(r.count == 1 && !resumeFind(&r, "sdmc:/d.mp4", &pos, NULL));

    // Kept on the card, with names that have spaces and accents.
    resumeRemember(&r, "sdmc:/Videos/Mi película 2.mkv", 3725 * S + 500000000, 7200 * S);
    CHECK(resumeSave(&r, file));
    static ResumeList back;
    resumeLoad(&back, file);
    CHECK(back.count == 2);
    CHECK(resumeFind(&back, "sdmc:/Videos/Mi película 2.mkv", &pos, &len) && pos == 3725 * S + 500000000 &&
          len == 7200 * S);

    // Broken lines are skipped; at most RESUME_MAX are kept, the oldest go.
    FILE *f = fopen(file, "w");
    if (f) {
        fprintf(f, "garbage\n12000\t\n-5\t100\tsdmc:/neg.mp4\n45000\t90000\tsdmc:/ok.mp4\n");
        fclose(f);
    }
    resumeLoad(&back, file);
    CHECK(back.count == 1 && resumeFind(&back, "sdmc:/ok.mp4", &pos, NULL) && pos == 45 * S);
    for (int i = 0; i < RESUME_MAX + 5; i++) {
        char path[64];
        snprintf(path, sizeof(path), "sdmc:/v%d.mp4", i);
        resumeRemember(&back, path, 60 * S, 0);
    }
    CHECK(back.count == RESUME_MAX && strcmp(back.items[0].path, "sdmc:/v204.mp4") == 0);
    CHECK(!resumeFind(&back, "sdmc:/ok.mp4", &pos, NULL));
    resumeRemember(&back, "sdmc:/bad\nname.mp4", 60 * S, 0);  // A line break would make two lines.
    CHECK(!resumeFind(&back, "sdmc:/bad\nname.mp4", &pos, NULL));
}

void testBrowser(void)
{
    CHECK(browserIsVideo("a.mp4") && browserIsVideo("B.MKV") && browserIsVideo("c.webm") && browserIsVideo("d.m4v"));
    CHECK(!browserIsVideo("e.mp3") && !browserIsVideo("mp4") && !browserIsVideo("f.mp4.txt"));
}

int main(void)
{
    system("rm -rf " TMP_ROOT);
    mkdir(TMP_ROOT, 0777);
    testResume();
    testBrowser();
    testSubs();
    testAppRoot();
    testClock();
    testMenu();
    testNaming();
    testLibrary();
    testHome();
    testShowcase();
    testLibView();
    testSettings();
    testThumbs();
    testWatched();
    testTrackPrefs();
    testPlayback();
    testSwFrame();
    testAssCompose();
    testJellyfin();
    testPlex();
    testKinetic();
    testStats();
    testTrickplay();
    testLiveTv();
    testHlsMaster();
    testDownloads();
    testLang();
    testDav();
    testUpdate();
    testUpdateRelease();
    testNotice();
    testIptv();
    testIptvHome();
    testIptvShowcase();
    testXtreamVod();
    testStremio();
    testCertTrust();
    testPicSubs();
    testVobsub();
    testPicSubsFf();
    testFolderList();
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
