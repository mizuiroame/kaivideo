// Options for stb_image. logo.c and image_impl.c must agree.
#pragma once

// Only the formats station logos come in; fewer parsers means less attack
// surface. ICO files are opened by logo.c, which hands their PNG to stb.
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
// Images come from memory.
#define STBI_NO_STDIO
// Backgrounds can be a camera's photo (background.h); logos have their own,
// smaller limit, checked before they are decoded (logo.h).
#define STBI_MAX_DIMENSIONS 12000
