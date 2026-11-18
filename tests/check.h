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
void testHome(void);
void testLibView(void);
void testSettings(void);
void testThumbs(void);
