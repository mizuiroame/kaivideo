// Mizuiro's mark for the About page, as KaiReader's: the 水色 seal, "Made by" and
// the MIZUIRO wordmark (data/brand_*.png, made at the screen's pixels from
// switch2homebrew-submit/tools/brand.py's pictures).
#pragma once

// Draws the mark with its top left at (x, y), layout pixels. Returns its height
// (0 if its pictures can't be read).
int uiDrawBrand(int x, int y);
// The mark with its bottom right at (right, bottom).
void uiDrawBrandAt(int right, int bottom);
// The app's icon (data/about_icon.png: its icon without the seal, rounded), ICON layout pixels square at (x, y).
void uiDrawAppIcon(int x, int y);
