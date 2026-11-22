#include "notice.h"

#include <stdio.h>
#include <string.h>

#define FREE_CHARS 40  // Read within the minimum time.

uint64_t noticeLength(const char *text)
{
    size_t chars = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++)
        chars += (*p & 0xC0) != 0x80;  // Characters, not UTF-8 bytes.
    uint64_t length = NOTICE_MIN_NS;
    if (chars > FREE_CHARS)
        length += (uint64_t)(chars - FREE_CHARS) * NOTICE_PER_CHAR_NS;
    return length > NOTICE_MAX_NS ? NOTICE_MAX_NS : length;
}

static void drop(Notices *n, int index)
{
    memmove(&n->items[index], &n->items[index + 1], (size_t)(n->count - index - 1) * sizeof(Notice));
    n->count--;
}

void noticePush(Notices *n, NoticeKind kind, const char *text, uint64_t now_ns)
{
    if (!text || !text[0])
        return;
    Notice *last = n->count ? &n->items[n->count - 1] : NULL;
    if (last && last->kind == kind && strcmp(last->text, text) == 0) {
        if (last->start_ns)  // Shown: from now on, as long again (it doesn't slide in twice).
            last->length_ns = now_ns - last->start_ns + noticeLength(text);
        return;
    }
    if (n->count == NOTICE_QUEUE)
        drop(n, n->count > 1 ? 1 : 0);  // The oldest waiting, not the one being read.
    Notice *it = &n->items[n->count++];
    snprintf(it->text, sizeof(it->text), "%s", text);
    it->kind = kind;
    it->start_ns = 0;
    it->length_ns = noticeLength(it->text);
}

const Notice *noticeShown(Notices *n, uint64_t now_ns, float *slide)
{
    *slide = 0.0f;
    while (n->count && n->items[0].start_ns && now_ns >= n->items[0].start_ns + n->items[0].length_ns)
        drop(n, 0);
    if (!n->count)
        return NULL;
    Notice *it = &n->items[0];
    if (!it->start_ns)
        it->start_ns = now_ns ? now_ns : 1;
    uint64_t in = now_ns - it->start_ns, left = it->start_ns + it->length_ns - now_ns;
    if (in < NOTICE_SLIDE_NS)
        *slide = (float)in / (float)NOTICE_SLIDE_NS;
    else if (left < NOTICE_SLIDE_NS)
        *slide = (float)left / (float)NOTICE_SLIDE_NS;
    else
        *slide = 1.0f;
    return it;
}

void noticeDismiss(Notices *n, uint64_t now_ns)
{
    if (!n->count || !n->items[0].start_ns)
        return;
    Notice *it = &n->items[0];
    if (now_ns < it->start_ns)
        now_ns = it->start_ns;  // A clock behind its start: as if at it (never a length below zero).
    uint64_t end = now_ns + NOTICE_SLIDE_NS;  // It slides out from here.
    if (end < it->start_ns + it->length_ns)
        it->length_ns = end - it->start_ns;
}
