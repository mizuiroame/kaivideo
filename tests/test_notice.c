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

static void testQueue(void)
{
    Notices n = {0};
    float slide;
    CHECK(noticeShown(&n, 10 * S, &slide) == NULL && slide == 0.0f);
    noticePush(&n, NOTICE_INFO, "First", 10 * S);
    noticePush(&n, NOTICE_ERROR, "Second", 10 * S);
    const Notice *shown = noticeShown(&n, 10 * S, &slide);
    CHECK(shown && strcmp(shown->text, "First") == 0 && slide == 0.0f);  // Starts sliding in.
    shown = noticeShown(&n, 10 * S + NOTICE_SLIDE_NS / 2, &slide);
    CHECK(slide > 0.4f && slide < 0.6f);
    shown = noticeShown(&n, 11 * S, &slide);
    CHECK(slide == 1.0f && strcmp(shown->text, "First") == 0);
    shown = noticeShown(&n, 10 * S + NOTICE_MIN_NS - NOTICE_SLIDE_NS / 2, &slide);
    CHECK(slide > 0.4f && slide < 0.6f);  // Sliding out.
    shown = noticeShown(&n, 10 * S + NOTICE_MIN_NS, &slide);
    CHECK(shown && strcmp(shown->text, "Second") == 0 && shown->kind == NOTICE_ERROR && slide == 0.0f);
    CHECK(noticeShown(&n, 30 * S, &slide) == NULL && n.count == 0);
}

static void testRepeatsAndDismiss(void)
{
    Notices n = {0};
    float slide;
    noticePush(&n, NOTICE_ERROR, "Can't reach it.", 10 * S);
    noticeShown(&n, 10 * S, &slide);
    noticePush(&n, NOTICE_ERROR, "Can't reach it.", 13 * S);  // The same again: stays longer, no second bubble.
    CHECK(n.count == 1);
    CHECK(noticeShown(&n, 15 * S, &slide) != NULL && slide == 1.0f);
    noticeDismiss(&n, 15 * S);
    CHECK(noticeShown(&n, 15 * S + NOTICE_SLIDE_NS / 2, &slide) != NULL && slide < 0.6f);
    CHECK(noticeShown(&n, 15 * S + NOTICE_SLIDE_NS, &slide) == NULL);

    noticePush(&n, NOTICE_INFO, "Early", 40 * S);
    noticeShown(&n, 40 * S, &slide);
    noticeDismiss(&n, 1);  // A time before its start: it leaves, and its length doesn't wrap around.
    CHECK(n.items[0].length_ns == NOTICE_SLIDE_NS);
    CHECK(noticeShown(&n, 40 * S + NOTICE_SLIDE_NS, &slide) == NULL);

    for (int i = 0; i < 10; i++) {  // A burst keeps the one shown and the newest.
        char text[16];
        text[0] = (char)('a' + i);
        text[1] = '\0';
        noticePush(&n, NOTICE_INFO, text, 20 * S);
        if (i == 0)
            noticeShown(&n, 20 * S, &slide);
    }
    CHECK(n.count == NOTICE_QUEUE && strcmp(n.items[0].text, "a") == 0 && strcmp(n.items[NOTICE_QUEUE - 1].text, "j") == 0);
    noticePush(&n, NOTICE_INFO, "", 20 * S);
    noticePush(&n, NOTICE_INFO, NULL, 20 * S);
    CHECK(n.count == NOTICE_QUEUE);
}

void testNotice(void)
{
    testLength();
    testQueue();
    testRepeatsAndDismiss();
}
