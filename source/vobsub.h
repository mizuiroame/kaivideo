// VobSub subtitles as two files next to a video: "name.idx" (text: the
// picture size, the palette, and for each language when each picture shows
// and where it is in the .sub) and "name.sub" (the pictures, in MPEG program
// stream packets). This reads the index and takes one picture's bytes out of
// the .sub, for FFmpeg's DVD subtitle decoder. Pure logic.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VOBSUB_LANGUAGES_MAX 8     // Languages kept from an index.
#define VOBSUB_PICTURES_MAX 20000  // Pictures kept per language.
#define VOBSUB_IDX_MAX (2u << 20)  // A bigger .idx isn't read.
#define VOBSUB_SUB_MAX (96u << 20) // Nor a bigger .sub.
#define VOBSUB_PICTURE_MAX 65536   // One picture's bytes (its own size field is 16 bits).

typedef struct {
    int64_t start_ns;
    uint64_t filepos;
} VobsubEntry;

typedef struct {
    char code[8];         // "en", or "" if the index doesn't say.
    int stream;           // Its number in the .sub (0-31).
    VobsubEntry *entries; // By time.
    int count;
} VobsubLanguage;

typedef struct {
    char *header;         // The lines before the first language (size, palette...): the decoder's extradata.
    size_t header_size;
    VobsubLanguage languages[VOBSUB_LANGUAGES_MAX];
    int count;
} VobsubIndex;

// Reads an .idx. False if it isn't one, or has no picture.
bool vobsubParseIndex(const char *text, size_t size, VobsubIndex *out);
void vobsubIndexFree(VobsubIndex *index);

// One picture of the .sub: the packets of stream from filepos on, joined until
// the picture's own size is reached. Its length, or 0 if it can't be had.
size_t vobsubPicture(const uint8_t *sub, size_t size, uint64_t filepos, int stream, uint8_t *out, size_t out_max);

// "name.idx" next to a video ("dav:/A/b.mkv" -> "dav:/A/b.idx"), and the .sub for an .idx. False if too long.
bool vobsubIdxFor(const char *video_path, char *out, size_t size);
bool vobsubSubFor(const char *idx_path, char *out, size_t size);
