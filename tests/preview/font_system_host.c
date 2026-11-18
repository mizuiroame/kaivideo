// Host stand-in for source/font_system.c. The console provides CJK fonts at
// run time; for previews, KAIVIDEO_CJK_FONT may name a local TTF/TTC/OTF
// (it is read here and never added to the repository).
#include <stdio.h>
#include <stdlib.h>

#include "font_system.h"

static unsigned char *g_data;

int fontSystemLoad(FontBlob *out, int max)
{
    const char *path = getenv("KAIVIDEO_CJK_FONT");
    if (!path || max < 1)
        return 0;
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    g_data = size > 0 ? malloc((size_t)size) : NULL;
    size_t got = g_data ? fread(g_data, 1, (size_t)size, f) : 0;
    fclose(f);
    if (got != (size_t)size) {
        free(g_data);
        g_data = NULL;
        return 0;
    }
    out[0] = (FontBlob){g_data, (size_t)size};
    return 1;
}

void fontSystemUnload(void)
{
    free(g_data);
    g_data = NULL;
}
