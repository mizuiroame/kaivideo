// Notices: short messages in a bubble at the top of the screen, as a phone
// shows them. One at a time, each for a while that depends on its length,
// sliding down in and up out; the next waits its turn. Pure timing, tested on
// the PC; each app draws the bubble. The same file is in KaiPlayer, KaiRadio
// and KaiVideo.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define NOTICE_TEXT_MAX 512
#define NOTICE_QUEUE 4                     // Shown and waiting; past it the oldest waiting goes.
#define NOTICE_SLIDE_NS 220000000ull       // Sliding in, and out.
#define NOTICE_MIN_NS 3500000000ull        // On screen at least this long,
#define NOTICE_MAX_NS 9000000000ull        // at most this long,
#define NOTICE_PER_CHAR_NS 55000000ull     // and this much longer per character after the first 40.

typedef enum {
    NOTICE_INFO,   // Something to know.
    NOTICE_DONE,   // Something done.
    NOTICE_ERROR,  // Something that failed.
} NoticeKind;

typedef struct {
    char text[NOTICE_TEXT_MAX];
    NoticeKind kind;
    uint64_t start_ns;     // When it began showing; 0 while it waits.
    uint64_t length_ns;    // How long it shows, sliding included.
} Notice;

typedef struct {
    Notice items[NOTICE_QUEUE];  // items[0] is the one shown (or next).
    int count;
} Notices;

// How long text stays on screen.
uint64_t noticeLength(const char *text);

// Adds a notice. The same text as the one shown or waiting last only makes it
// stay longer (a repeated failure doesn't pile up bubbles).
void noticePush(Notices *n, NoticeKind kind, const char *text, uint64_t now_ns);

// The notice to draw now, or NULL; *slide is 0 (out of sight above) to 1
// (fully in). Drops the ones whose time is over and starts the next.
const Notice *noticeShown(Notices *n, uint64_t now_ns, float *slide);

// The one shown leaves now (a tap on it).
void noticeDismiss(Notices *n, uint64_t now_ns);
