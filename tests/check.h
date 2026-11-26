// A minimal check macro shared by the host test files.
#pragma once

#include <stdio.h>

extern int g_failures;
extern int g_checks;

#define CHECK(cond)                                                                  \
    do {                                                                             \
        g_checks++;                                                                  \
        if (!(cond)) {                                                               \
            g_failures++;                                                            \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                            \
    } while (0)

#define TMP_ROOT "/tmp/kaivideo_test"

void testResume(void);
void testBrowser(void);
void testSubs(void);
void testAppRoot(void);
void testClock(void);
void testMenu(void);
void testNaming(void);
void testLibrary(void);
void testNfo(void);
void testHome(void);
void testShowcase(void);
void testLibView(void);
void testSettings(void);
void testThumbs(void);
void testWatched(void);
void testTrackPrefs(void);
void testPlayback(void);
void testSwFrame(void);
void testHdr(void);
void testAssCompose(void);
void testJellyfin(void);
void testPlex(void);
void testStats(void);
void testTrickplay(void);
void testLiveTv(void);
void testDvr(void);
void testKinetic(void);
void testHlsMaster(void);
void testDownloads(void);
void testLang(void);
void testDav(void);
void testUpdate(void);
void testUpdateRelease(void);
void testNotice(void);
void testIptv(void);
void testIptvHome(void);
void testIptvShowcase(void);
void testXtreamVod(void);
void testStremio(void);
void testCertTrust(void);
void testPicSubs(void);
void testVobsub(void);
void testPicSubsFf(void);
void testFolderList(void);
