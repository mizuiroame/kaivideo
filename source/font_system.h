// Fallback fonts from the console, for characters the embedded Noto Sans
// lacks: Japanese kanji and kana, Chinese and Korean. The console provides
// them at run time (as hbmenu uses them); they are not part of this project.
#pragma once

#include <stddef.h>

typedef struct {
    const unsigned char *data;
    size_t size;
} FontBlob;

// Fills up to max fonts, in fallback order. Returns how many.
int fontSystemLoad(FontBlob *out, int max);
void fontSystemUnload(void);
