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

// It leaves from now (sliding out), unless it was leaving sooner already.
static void leaveAt(Notice *it, uint64_t now_ns)
{
    if (now_ns < it->start_ns)
        now_ns = it->start_ns;  // A clock behind its start: as if at it (never a length below zero).
    uint64_t end = now_ns + NOTICE_SLIDE_NS;
    if (end < it->start_ns + it->length_ns)
        it->length_ns = end - it->start_ns;
}

void noticePush(Notices *n, NoticeKind kind, const char *text, uint64_t now_ns)
{
    if (!text || !text[0])
        return;
    Notice *newest = n->count ? &n->items[0] : NULL;
    if (newest && !newest->pushed_out && newest->kind == kind && strcmp(newest->text, text) == 0) {
        uint64_t until = now_ns - newest->start_ns + noticeLength(text);  // As long again from now.
        if (now_ns >= newest->start_ns && until > newest->length_ns)
            newest->length_ns = until;
        return;
    }
    if (n->count == NOTICE_STACK + 1)
        drop(n, n->count - 1);  // The one already leaving goes at once.
    memmove(&n->items[1], &n->items[0], (size_t)n->count * sizeof(Notice));
    n->count++;
    Notice *it = &n->items[0];
    memset(it, 0, sizeof(*it));
    snprintf(it->text, sizeof(it->text), "%s", text);
    it->kind = kind;
    it->id = ++n->last_id;
    it->start_ns = now_ns ? now_ns : 1;
    it->length_ns = noticeLength(it->text);
    it->row = 0.0f;
    int shown = 0;  // Past NOTICE_STACK still staying, the oldest is pushed out.
    for (int i = 0; i < n->count; i++) {
        if (n->items[i].pushed_out)
            continue;
        if (++shown > NOTICE_STACK) {
            n->items[i].pushed_out = true;
            leaveAt(&n->items[i], now_ns);
        }
    }
}

int noticeVisible(Notices *n, uint64_t now_ns, NoticeView out[NOTICE_STACK + 1])
{
    for (int i = n->count - 1; i >= 0; i--)
        if (now_ns >= n->items[i].start_ns + n->items[i].length_ns)
            drop(n, i);
    float step = n->last_ns && now_ns > n->last_ns ? (float)(now_ns - n->last_ns) / (float)NOTICE_MOVE_NS
                                                         : (float)NOTICE_STACK;  // First seen: in place at once.
    n->last_ns = now_ns;
    int row = 0;
    for (int i = 0; i < n->count; i++) {
        Notice *it = &n->items[i];
        float target = (float)row;
        if (!it->pushed_out)
            row++;  // One leaving keeps its place until it's gone; the rest close up under it.
        if (it->row < target)
            it->row = it->row + step < target ? it->row + step : target;
        else if (it->row > target)
            it->row = it->row - step > target ? it->row - step : target;
        uint64_t in = now_ns > it->start_ns ? now_ns - it->start_ns : 0;
        uint64_t end = it->start_ns + it->length_ns, left = end > now_ns ? end - now_ns : 0;
        NoticeView *v = &out[i];
        v->notice = it;
        v->row = it->row;
        v->leaving = left < NOTICE_SLIDE_NS;
        v->slide = v->leaving ? (float)left / (float)NOTICE_SLIDE_NS
                   : in < NOTICE_SLIDE_NS ? (float)in / (float)NOTICE_SLIDE_NS
                                          : 1.0f;
    }
    return n->count;
}

void noticeDismiss(Notices *n, uint64_t id, uint64_t now_ns)
{
    for (int i = 0; i < n->count; i++)
        if (n->items[i].id == id)
            leaveAt(&n->items[i], now_ns);
}
