// Notices' timing (notice.h). The same file is in KaiPlayer, KaiRadio and KaiVideo.
#include <string.h>

#include "check.h"
#include "notice.h"

#define S 1000000000ull

static void testLength(void)
{
    CHECK(noticeLength("Saved.") == NOTICE_MIN_NS);
    char longer[200];
    memset(longer, 'a', sizeof(longer) - 1);
    longer[sizeof(longer) - 1] = '\0';
    CHECK(noticeLength(longer) == NOTICE_MAX_NS);
    // Characters, not bytes: 50 accented letters take as long as 50 plain ones.
    char accents[101] = "", plain[51] = "";
    for (int i = 0; i < 50; i++) {
        strcat(accents, "\xC3\xA9");
        strcat(plain, "e");
    }
    CHECK(noticeLength(accents) == noticeLength(plain) && noticeLength(plain) > NOTICE_MIN_NS);
}

static void testStack(void)
{
    Notices n = {0};
    NoticeView v[NOTICE_STACK + 1];
    CHECK(noticeVisible(&n, 10 * S, v) == 0);
    noticePush(&n, NOTICE_INFO, "First", 10 * S);
    CHECK(noticeVisible(&n, 10 * S, v) == 1 && v[0].slide == 0.0f && !v[0].leaving);  // Starts sliding in.
    CHECK(noticeVisible(&n, 10 * S + NOTICE_SLIDE_NS / 2, v) == 1 && v[0].slide > 0.4f && v[0].slide < 0.6f);
    CHECK(noticeVisible(&n, 11 * S, v) == 1 && v[0].slide == 1.0f && v[0].row == 0.0f);

    // A second one at once: on top, the first moves down a row (not in one jump).
    noticePush(&n, NOTICE_ERROR, "Second", 11 * S);
    CHECK(noticeVisible(&n, 11 * S + NOTICE_MOVE_NS / 2, v) == 2);
    CHECK(!strcmp(v[0].notice->text, "Second") && v[0].notice->kind == NOTICE_ERROR && v[0].row == 0.0f);
    CHECK(!strcmp(v[1].notice->text, "First") && v[1].row > 0.3f && v[1].row < 0.7f && v[1].slide == 1.0f);
    CHECK(noticeVisible(&n, 12 * S, v) == 2 && v[1].row == 1.0f);

    // Each leaves at its own time: the first (from 10 s), sliding out to the side.
    CHECK(noticeVisible(&n, 10 * S + NOTICE_MIN_NS - NOTICE_SLIDE_NS / 2, v) == 2 && v[1].leaving &&
          v[1].slide > 0.4f && v[1].slide < 0.6f && !v[0].leaving);
    CHECK(noticeVisible(&n, 10 * S + NOTICE_MIN_NS, v) == 1 && !strcmp(v[0].notice->text, "Second"));
    CHECK(noticeVisible(&n, 30 * S, v) == 0 && n.count == 0);
}

static void testOverflow(void)
{
    Notices n = {0};
    NoticeView v[NOTICE_STACK + 1];
    noticePush(&n, NOTICE_INFO, "a", 10 * S);
    noticePush(&n, NOTICE_INFO, "b", 10 * S);
    noticePush(&n, NOTICE_INFO, "c", 10 * S);
    CHECK(noticeVisible(&n, 11 * S, v) == 3 && v[2].row == 2.0f && !v[2].leaving);
    // A fourth: on top, and the oldest is pushed out (sliding away), not cut off.
    noticePush(&n, NOTICE_INFO, "d", 11 * S);
    int count = noticeVisible(&n, 11 * S + NOTICE_SLIDE_NS / 2, v);
    CHECK(count == 4 && !strcmp(v[0].notice->text, "d") && !strcmp(v[3].notice->text, "a") && v[3].leaving);
    CHECK(noticeVisible(&n, 11 * S + NOTICE_SLIDE_NS, v) == 3 && !strcmp(v[2].notice->text, "b"));
    // A burst never shows more than the stack, plus the one leaving.
    for (int i = 0; i < 10; i++) {
        char text[8] = {(char)('e' + i), 0};
        noticePush(&n, NOTICE_INFO, text, 12 * S);
    }
    CHECK(n.count <= NOTICE_STACK + 1 && !strcmp(n.items[0].text, "n"));
    int staying = 0;
    for (int i = 0; i < n.count; i++)
        staying += !n.items[i].pushed_out;
    CHECK(staying == NOTICE_STACK);
    noticePush(&n, NOTICE_INFO, "", 12 * S);
    noticePush(&n, NOTICE_INFO, NULL, 12 * S);
    CHECK(!strcmp(n.items[0].text, "n"));
}

static void testRepeatsAndDismiss(void)
{
    Notices n = {0};
    NoticeView v[NOTICE_STACK + 1];
    noticePush(&n, NOTICE_ERROR, "Can't reach it.", 10 * S);
    noticeVisible(&n, 10 * S, v);
    noticePush(&n, NOTICE_ERROR, "Can't reach it.", 13 * S);  // The same again: stays longer, no second bubble.
    CHECK(n.count == 1);
    CHECK(noticeVisible(&n, 15 * S, v) == 1 && v[0].slide == 1.0f);
    noticePush(&n, NOTICE_INFO, "Other", 15 * S);
    uint64_t first = n.items[1].id;
    CHECK(first && first != n.items[0].id);
    noticeDismiss(&n, first, 16 * S);  // A tap on the lower one: only it goes.
    CHECK(noticeVisible(&n, 16 * S + NOTICE_SLIDE_NS / 2, v) == 2 && v[1].leaving && !v[0].leaving);
    CHECK(noticeVisible(&n, 16 * S + NOTICE_SLIDE_NS, v) == 1 && !strcmp(v[0].notice->text, "Other"));
    noticeDismiss(&n, 999, 16 * S);  // No such bubble: nothing.
    CHECK(n.count == 1);

    Notices early = {0};
    noticePush(&early, NOTICE_INFO, "Early", 40 * S);
    noticeDismiss(&early, early.items[0].id, 1);  // A time before its start: it leaves, no wrap-around.
    CHECK(early.items[0].length_ns == NOTICE_SLIDE_NS);
    CHECK(noticeVisible(&early, 40 * S + NOTICE_SLIDE_NS, v) == 0);
}

void testNotice(void)
{
    testLength();
    testStack();
    testOverflow();
    testRepeatsAndDismiss();
}
