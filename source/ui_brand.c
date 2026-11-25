#include "ui_brand.h"

#include <stdlib.h>

#include "brand_seal_png.h"
#include "brand_word_png.h"
#include "about_icon_png.h"
#include "font.h"
#include "gfx.h"
#include "image_config.h"
#include "lang.h"
#include "stb_image.h"
#include "ui_style.h"

typedef struct {
    uint32_t *pixels;  // 0xAABBGGRR.
    int w, h;
    bool tried;
} BrandImage;

// Decoded once; kept while the app runs (about 60 KB).
static const BrandImage *decoded(BrandImage *img, const unsigned char *png, size_t size)
{
    if (!img->tried) {
        img->tried = true;
        int comp;
        img->pixels = (uint32_t *)stbi_load_from_memory(png, (int)size, &img->w, &img->h, &comp, 4);
    }
    return img->pixels ? img : NULL;
}

static BrandImage g_seal, g_word, g_icon;

void uiDrawAppIcon(int x, int y)
{
    const BrandImage *i = decoded(&g_icon, about_icon_png, about_icon_png_size);
    if (i)
        gfxDrawImageFb((int)(x * GFX_SCALE + 0.5f), (int)(y * GFX_SCALE + 0.5f), i->w, i->h, i->pixels);
}

void uiDrawBrandAt(int right, int bottom)
{
    const BrandImage *s = decoded(&g_seal, brand_seal_png, brand_seal_png_size);
    const BrandImage *w = decoded(&g_word, brand_word_png, brand_word_png_size);
    if (!s || !w)
        return;
    int text_w = fontTextWidth(FONT_REGULAR, 16, T("Made by")), word_w = (int)(w->w / GFX_SCALE + 0.5f);
    int width = (int)(s->w / GFX_SCALE + 0.5f) + 20 + (text_w > word_w ? text_w : word_w);
    uiDrawBrand(right - width, bottom - (int)(s->h / GFX_SCALE + 0.5f));
}

int uiDrawBrand(int x, int y)
{
    const BrandImage *s = decoded(&g_seal, brand_seal_png, brand_seal_png_size);
    const BrandImage *w = decoded(&g_word, brand_word_png, brand_word_png_size);
    if (!s || !w)
        return 0;
    int seal_h = (int)(s->h / GFX_SCALE + 0.5f), seal_w = (int)(s->w / GFX_SCALE + 0.5f);
    gfxDrawImageFb((int)(x * GFX_SCALE + 0.5f), (int)(y * GFX_SCALE + 0.5f), s->w, s->h, s->pixels);
    int tx = x + seal_w + 20;
    float word_h = w->h / GFX_SCALE;
    // "Made by" and the wordmark as one block, centered on the seal.
    float block = 20 + 6 + word_h, top = y + (seal_h - block) / 2;
    fontDraw(FONT_REGULAR, 16, tx, (int)top, T("Made by"), COLOR_DIM);
    gfxDrawImageFb((int)(tx * GFX_SCALE + 0.5f), (int)((top + 26) * GFX_SCALE + 0.5f), w->w, w->h, w->pixels);
    return seal_h;
}
