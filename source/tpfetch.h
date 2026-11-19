// The trickplay pictures of the video playing from the server, fetched by a
// thread of their own when asked for: a sheet at a time is kept, decoded.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// A video from the server starts: what pictures it has is asked at once.
void tpfetchStart(const char *location);
void tpfetchStop(void);
// The picture of pos_ns (w x h pixels, 0xAABBGGRR), or NULL while its sheet
// isn't here (it's then asked for). The memory stays the same; changed says
// it holds another picture since the last call.
const uint32_t *tpfetchPicture(int64_t pos_ns, int *w, int *h, bool *changed);
