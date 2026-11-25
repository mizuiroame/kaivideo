// VobSub .idx/.sub files (made up here, byte by byte).
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "vobsub.h"

static const char IDX[] =
    "# VobSub index file, v7 (do not modify this line!)\r\n"
    "size: 720x480\r\n"
    "palette: 000000, ffffff, 808080, 000000, 000000, 000000, 000000, 000000, "
    "000000, 000000, 000000, 000000, 000000, 000000, 000000, 000000\r\n"
    "\r\n"
    "id: en, index: 0\r\n"
    "timestamp: 00:00:05:500, filepos: 000000000\r\n"
    "timestamp: 00:00:01:000, filepos: 000000800\r\n"
    "\r\n"
    "id: es, index: 1\r\n"
    "delay: 00:00:02:000\r\n"
    "timestamp: 00:01:00:250, filepos: 000001000\r\n"
    "timestamp: 0x:00:00:000, filepos: 000001000\r\n"
    "id: fr, index: 2\r\n";

static void testIndex(void)
{
    VobsubIndex x;
    CHECK(vobsubParseIndex(IDX, sizeof(IDX) - 1, &x));
    CHECK(x.count == 2);  // French has no picture.
    CHECK(!strncmp(x.header, "# VobSub", 8) && strstr(x.header, "palette:") && !strstr(x.header, "id:"));
    CHECK(!strcmp(x.languages[0].code, "en") && x.languages[0].stream == 0 && x.languages[0].count == 2);
    CHECK(x.languages[0].entries[0].start_ns == 1000000000ll && x.languages[0].entries[0].filepos == 0x800);  // By time.
    CHECK(x.languages[0].entries[1].start_ns == 5500000000ll);
    CHECK(!strcmp(x.languages[1].code, "es") && x.languages[1].stream == 1 && x.languages[1].count == 1);
    CHECK(x.languages[1].entries[0].start_ns == 62250000000ll);  // The delay counts.
    vobsubIndexFree(&x);
    CHECK(!vobsubParseIndex("hello", 5, &x) && !x.header);
    CHECK(!vobsubParseIndex("id: en, index: 40\ntimestamp: 00:00:01:000, filepos: 0\n", 52, &x));
    CHECK(!vobsubParseIndex(NULL, 0, &x));
}

// An MPEG-2 pack header, then a private stream 1 packet of stream with data.
static size_t packet(uint8_t *out, int stream, const uint8_t *data, size_t n)
{
    static const uint8_t PACK[14] = {0, 0, 1, 0xBA, 0x44, 0, 4, 0, 4, 1, 1, 0x89, 0xC3, 0xF8};
    memcpy(out, PACK, sizeof(PACK));
    uint8_t *p = out + sizeof(PACK);
    size_t length = 3 + 5 + 1 + n;  // Flags, header length, a time, the stream byte, the data.
    uint8_t head[] = {0, 0, 1, 0xBD, (uint8_t)(length >> 8), (uint8_t)length, 0x81, 0x80, 5, 0x21, 0, 1, 0, 1,
                      (uint8_t)(0x20 + stream)};
    memcpy(p, head, sizeof(head));
    memcpy(p + sizeof(head), data, n);
    return sizeof(PACK) + sizeof(head) + n;
}

static void testPictures(void)
{
    uint8_t picture[300], sub[2048], out[1024];
    for (size_t i = 0; i < sizeof(picture); i++)
        picture[i] = (uint8_t)(i * 7);
    picture[0] = sizeof(picture) >> 8;  // Its size, first.
    picture[1] = sizeof(picture) & 0xFF;
    uint8_t other[20] = {0, 20};
    size_t n = 0;
    n += packet(sub + n, 0, picture, 200);    // The picture's first part,
    n += packet(sub + n, 1, other, 20);       // another language's picture between,
    n += packet(sub + n, 0, picture + 200, 100);  // and the rest.
    sub[n++] = 0;
    sub[n++] = 0;
    sub[n++] = 1;
    sub[n++] = 0xB9;
    CHECK(vobsubPicture(sub, n, 0, 0, out, sizeof(out)) == sizeof(picture) && !memcmp(out, picture, sizeof(picture)));
    CHECK(vobsubPicture(sub, n, 0, 1, out, sizeof(out)) == 20 && out[1] == 20);
    CHECK(vobsubPicture(sub, n, 0, 2, out, sizeof(out)) == 0);  // Not there.
    CHECK(vobsubPicture(sub, n, 0, 0, out, 100) == 0);           // No room for it.
    CHECK(vobsubPicture(sub, n - 120, 0, 0, out, sizeof(out)) == 0);  // Cut short.
    CHECK(vobsubPicture(sub, n, n + 5, 0, out, sizeof(out)) == 0);
    CHECK(vobsubPicture(sub, n, 0, 40, out, sizeof(out)) == 0);
    // Where the index says isn't quite a packet's start: a little further on is.
    uint8_t shifted[2100] = {0xFF, 0xFF, 0xFF};
    memcpy(shifted + 3, sub, n);
    CHECK(vobsubPicture(shifted, n + 3, 0, 1, out, sizeof(out)) == 20);
    uint8_t noise[8000];
    memset(noise, 0xAA, sizeof(noise));
    CHECK(vobsubPicture(noise, sizeof(noise), 0, 0, out, sizeof(out)) == 0);
}

static void testNames(void)
{
    char out[64];
    CHECK(vobsubIdxFor("sdmc:/Films/Kite Hill (2015).mkv", out, sizeof(out)) && !strcmp(out, "sdmc:/Films/Kite Hill (2015).idx"));
    CHECK(vobsubIdxFor("dav:/A.b/clip", out, sizeof(out)) && !strcmp(out, "dav:/A.b/clip.idx"));
    CHECK(vobsubSubFor("dav:/A/clip.idx", out, sizeof(out)) && !strcmp(out, "dav:/A/clip.sub"));
    CHECK(vobsubSubFor("sdmc:/CLIP.IDX", out, sizeof(out)) && !strcmp(out, "sdmc:/CLIP.SUB"));
    CHECK(!vobsubSubFor("sdmc:/clip.srt", out, sizeof(out)));
    CHECK(!vobsubIdxFor("sdmc:/a-very-long-name-that-does-not-fit-here-at-all.mkv", out, 16));
}

void testVobsub(void)
{
    testIndex();
    testPictures();
    testNames();
}
