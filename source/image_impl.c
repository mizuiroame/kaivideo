// Compiles the single-file image and font libraries once. Their licenses are
// in third_party/ (public domain or MIT).
#include "image_config.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
