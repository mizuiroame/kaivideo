// Subtitles: reading SubRip, cleaning tags, ASS lines, and finding the file.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "check.h"
#include "subs.h"

#define S 1000000000ll
#define MS 1000000ll

static bool textAt(const SubTrack *t, int64_t pos, const char *expected)
{
    char out[256];
    bool on = subsAt(t, pos, out, sizeof(out));
    return expected ? on && strcmp(out, expected) == 0 : !on;
}

static void testSrt(void)
{
    SubTrack t = {0};
    const char *srt = "\xEF\xBB\xBF" "1\r\n00:00:01,000 --> 00:00:02,500\r\nHello <i>there</i>\r\n\r\n"
                      "2\r\n00:00:03,000 --> 00:00:04,000\r\nTwo\r\nlines\r\n\r\n"
                      "00:00:05.000 --> 00:00:06.000\n{\\an8}No number, dots\n\n"
                      "4\nnot a time --> nope\nSkipped\n\n"
                      "5\n01:00:00,000 --> 01:00:01,000\n  spaced  \n";
    CHECK(subsParseSrt(&t, srt, strlen(srt)));
    CHECK(t.count == 4);
    CHECK(textAt(&t, 1 * S, "Hello there") && textAt(&t, 2 * S + 499 * MS, "Hello there"));
    CHECK(textAt(&t, 2 * S + 500 * MS, NULL));  // The end is exclusive.
    CHECK(textAt(&t, 3 * S + 500 * MS, "Two\nlines"));
    CHECK(textAt(&t, 5 * S, "No number, dots"));
    CHECK(textAt(&t, 3600 * S, "spaced"));
    CHECK(textAt(&t, 0, NULL) && textAt(&t, 7 * S, NULL));
    subsFree(&t);
    CHECK(!subsParseSrt(&t, "nothing here", 12) && t.count == 0);
    // Times that would overflow, or are negative, aren't times.
    const char *odd = "1\n99999999999:00:00,000 --> 99999999999:00:01,000\nx\n\n"
                      "2\n00:-1:00,000 --> 00:00:01,000\ny\n\n";
    CHECK(!subsParseSrt(&t, odd, strlen(odd)) && t.count == 0);
    subsFree(&t);
}

static void testAdd(void)
{
    SubTrack t = {0};
    CHECK(subsAdd(&t, 5 * S, 6 * S, "B"));
    CHECK(subsAdd(&t, 1 * S, 2 * S, "A"));
    CHECK(subsAdd(&t, 5 * S, 6 * S, "B"));  // Read again after a seek: kept once.
    CHECK(t.count == 2 && strcmp(t.cues[0].text, "A") == 0);
    CHECK(subsAdd(&t, 5 * S + 500 * MS, 7 * S, "C"));  // Overlapping: both show.
    CHECK(textAt(&t, 5 * S + 600 * MS, "B\nC"));
    CHECK(!subsAdd(&t, 8 * S, 8 * S, "empty time") && !subsAdd(&t, 8 * S, 9 * S, "{\\i1}") && t.count == 3);
    subsFree(&t);
}

static void testClean(void)
{
    char a[] = "{\\an8}{\\i1}Up\\Nhere{\\i0}\\hnow  ";
    subsClean(a);
    CHECK(strcmp(a, "Up\nhere now") == 0);
    char b[] = "<font color=\"#ff0\">Yellow</font>\r\n\r\n";
    subsClean(b);
    CHECK(strcmp(b, "Yellow") == 0);
    CHECK(strcmp(subsAssText("0,0,Default,,0,0,0,,Text, with a comma"), "Text, with a comma") == 0);
    CHECK(strcmp(subsAssText("plain"), "plain") == 0);
}

static void writeFile(const char *path)
{
    FILE *f = fopen(path, "w");
    if (f)
        fclose(f);
}

static void testFind(void)
{
    const char *dir = TMP_ROOT "/subs";
    mkdir(dir, 0777);
    writeFile(TMP_ROOT "/subs/Movie.mkv");
    writeFile(TMP_ROOT "/subs/Movie.es.srt");
    writeFile(TMP_ROOT "/subs/Movie.en.srt");
    writeFile(TMP_ROOT "/subs/Other.srt");
    char out[512];
    CHECK(subsFindFor(TMP_ROOT "/subs/Movie.mkv", out, sizeof(out)) &&
          strcmp(out, TMP_ROOT "/subs/Movie.en.srt") == 0);  // The first by name.
    writeFile(TMP_ROOT "/subs/Movie.srt");
    CHECK(subsFindFor(TMP_ROOT "/subs/Movie.mkv", out, sizeof(out)) &&
          strcmp(out, TMP_ROOT "/subs/Movie.srt") == 0);  // The exact name wins.
    CHECK(!subsFindFor(TMP_ROOT "/subs/Nothing.mp4", out, sizeof(out)));
    CHECK(!subsFindFor(TMP_ROOT "/subs/Mov.mkv", out, sizeof(out)));  // "Movie.srt" isn't for "Mov".
}

static void testSrtChoice(void)
{
    char base[64], best[64] = "";
    int rank = -1;
    subsVideoBase("dav:/Films/Paper.Boats.2024.mkv", base, sizeof(base));
    CHECK(!strcmp(base, "Paper.Boats.2024"));
    subsConsiderSrt(base, "Paper.Boats.2024.mkv", best, sizeof(best), &rank);
    subsConsiderSrt(base, "Paper.Boats.2024.fr.srt", best, sizeof(best), &rank);
    CHECK(rank == 1 && !strcmp(best, "Paper.Boats.2024.fr.srt"));
    subsConsiderSrt(base, "Paper.Boats.2024.es.SRT", best, sizeof(best), &rank);
    CHECK(rank == 1 && !strcmp(best, "Paper.Boats.2024.es.SRT"));  // The first by name.
    subsConsiderSrt(base, "Paper.Boats.2024.srt", best, sizeof(best), &rank);
    CHECK(rank == 0 && !strcmp(best, "Paper.Boats.2024.srt"));
    subsConsiderSrt(base, "Paper.Boats.2024.de.srt", best, sizeof(best), &rank);
    CHECK(!strcmp(best, "Paper.Boats.2024.srt"));  // The exact one stays.
    char other[64] = "";
    int none = -1;
    subsConsiderSrt(base, "Paper.Boats.2024x.srt", other, sizeof(other), &none);
    subsConsiderSrt(base, "Kites.srt", other, sizeof(other), &none);
    CHECK(none == -1 && !other[0]);
}

static void testLanguages(void)
{
    CHECK(strcmp(subsLanguageName("spa"), "Spanish") == 0);
    CHECK(strcmp(subsLanguageName("ES"), "Spanish") == 0);
    CHECK(strcmp(subsLanguageName("deu"), "German") == 0 && strcmp(subsLanguageName("ger"), "German") == 0);
    CHECK(strcmp(subsLanguageName("may"), "Malay") == 0);
    CHECK(strcmp(subsLanguageName("xyz"), "xyz") == 0);  // Unknown: as it is.
    CHECK(strcmp(subsLanguageName("e"), "e") == 0);      // Not a prefix match.
    CHECK(subsLanguageName("") == NULL && subsLanguageName(NULL) == NULL);

    char name[64];
    subsTrackName(name, sizeof(name), "Commentary", "eng", 2);
    CHECK(strcmp(name, "Commentary") == 0);
    subsTrackName(name, sizeof(name), "", "jpn", 2);
    CHECK(strcmp(name, "Japanese") == 0);
    subsTrackName(name, sizeof(name), NULL, "und", 3);
    CHECK(strcmp(name, "Track 3") == 0);
    subsTrackName(name, sizeof(name), NULL, NULL, 1);
    CHECK(strcmp(name, "Track 1") == 0);
}

void testSubs(void)
{
    testSrtChoice();
    testLanguages();
    testSrt();
    testAdd();
    testClean();
    testFind();
}
