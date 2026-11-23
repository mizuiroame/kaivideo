// Notices: short messages in bubbles at the top of the screen, as a phone
// shows them. A new one slides in on top and pushes the others down; up to
// NOTICE_STACK are on screen, each for a while that depends on its length,
// and past that the oldest leaves (sliding out to the side). Pure timing,
// tested on the PC; each app draws the bubbles. The same file is in
// KaiPlayer, KaiRadio and KaiVideo.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define NOTICE_TEXT_MAX 512
#define NOTICE_STACK 3                     // On screen at once; a fourth pushes the oldest out.
#define NOTICE_SLIDE_NS 220000000ull       // Sliding in, and out.
#define NOTICE_MOVE_NS 200000000ull        // Moving down one row when a new one comes.
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
    uint64_t id;           // Never 0; a tap names a bubble by it.
    uint64_t start_ns;     // When it came.
    uint64_t length_ns;    // How long it stays, sliding included.
    bool pushed_out;       // Leaving early: a newer one took its place.
    float row;             // Where it is drawn, in rows from the top (moves toward its place).
} Notice;

typedef struct {
    Notice items[NOTICE_STACK + 1];  // Newest first; one more while the oldest leaves.
    int count;
    uint64_t last_id;
    uint64_t last_ns;                // The last noticeVisible's time, for the rows' movement.
} Notices;

// One bubble to draw: slide 0 to 1 (in coming in from above; out leaving to the side), row from the top.
typedef struct {
    const Notice *notice;
    float slide;
    bool leaving;          // Going (its time is over, or it was pushed out): slides to the side.
    float row;
} NoticeView;

// How long text stays on screen.
uint64_t noticeLength(const char *text);

// Adds a notice on top. The same text as the newest only makes it stay longer
// (a repeated failure doesn't pile up bubbles).
void noticePush(Notices *n, NoticeKind kind, const char *text, uint64_t now_ns);

// The bubbles to draw now, newest first, into out; how many. Drops the ones whose time is over.
int noticeVisible(Notices *n, uint64_t now_ns, NoticeView out[NOTICE_STACK + 1]);

// The one with that id leaves now (a tap on it).
void noticeDismiss(Notices *n, uint64_t id, uint64_t now_ns);
