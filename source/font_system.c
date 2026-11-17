#include "font_system.h"

#include <switch.h>

// Japanese first: its font covers kana and most kanji. Simplified Chinese
// before traditional, then Korean.
static const PlSharedFontType FALLBACK_TYPES[] = {
    PlSharedFontType_Standard,
    PlSharedFontType_ChineseSimplified,
    PlSharedFontType_ExtChineseSimplified,
    PlSharedFontType_ChineseTraditional,
    PlSharedFontType_KO,
};

static bool g_has_pl;

int fontSystemLoad(FontBlob *out, int max)
{
    g_has_pl = R_SUCCEEDED(plInitialize(PlServiceType_User));
    if (!g_has_pl)
        return 0;
    int count = 0;
    for (size_t i = 0; i < sizeof(FALLBACK_TYPES) / sizeof(FALLBACK_TYPES[0]) && count < max; i++) {
        PlFontData font;
        if (R_SUCCEEDED(plGetSharedFontByType(&font, FALLBACK_TYPES[i])) && font.address && font.size > 0)
            out[count++] = (FontBlob){font.address, font.size};
    }
    return count;
}

void fontSystemUnload(void)
{
    if (g_has_pl)
        plExit();
    g_has_pl = false;
}
