// Recording Live TV with the Jellyfin server's DVR: R on a program of the guide
// (or its pill) offers to record it or its series, or not to; L (or its pill)
// lists what the server will record, the series it records and what it
// recorded (A plays one, X twice cancels or deletes); ZR in a channel records
// what's on now. The server is asked by a thread, one thing at a time.
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <switch.h>

#include "app.h"
#include "buttons.h"
#include "lang.h"
#include "ui_downloads.h"
#include "ui_library.h"
#include "ui_live.h"

#define ROWS_MAX (DVR_TIMERS_MAX + DVR_SERIES_MAX + DVR_RECORDINGS_MAX + 3)
#define TEXT_MAX 200

typedef enum { JOB_FETCH, JOB_RECORD, JOB_RECORD_SERIES, JOB_CANCEL, JOB_CANCEL_SERIES, JOB_DELETE } JobKind;

// The thing the thread does, and what came of it.
static struct {
    pthread_t thread;
    bool started;            // There's a thread to join.
    bool busy;               // It's still asking (atomic).
    JobKind kind;
    char id[DVR_ID_MAX];     // The program, timer, series or recording.
    bool by_program;         // id is a program's: the timer (or its series) is found by it.
    char name[LIVE_NAME_MAX];
    bool on_now;             // The program is on now (recording starts at once, or stops).
    int64_t until;           // When the program ends (what came of it is kept until then),
    char program[LIVE_PROGRAM_ID_MAX];  // and its id ("" when it isn't about one).
    JfDvrResult result;
    DvrLists *lists;         // What the server has after it, if it came.
    bool fetched;
} g_job;

// What this console last did to programs, as the server took it (dvr.h).
static DvrKnown g_known;

bool appDvrAvailable(const App *app)
{
    return app->config.use_jellyfin && app->config.server_view == SERVER_JELLYFIN && jfSyncSignedIn(SERVER_JELLYFIN);
}

// The timer of a program among lists (for a series: its series' id). "" if none.
static void timerOfProgram(const DvrLists *d, const char *program_id, bool series, char *out)
{
    const DvrTimer *t = dvrTimerFor(d, program_id);
    snprintf(out, DVR_ID_MAX, "%s", !t ? "" : series ? t->series_id : t->id);
}

static void *work(void *arg)
{
    (void)arg;
    DvrLists *d = g_job.lists;
    JfDvrResult r = JF_DVR_DONE;
    char id[DVR_ID_MAX];
    snprintf(id, sizeof(id), "%s", g_job.id);
    if (g_job.by_program) {  // The timer is found by its program in what the server has now.
        int count = jfSyncDvrTimers(NULL, d->timers, DVR_TIMERS_MAX);
        d->timer_count = count > 0 ? count : 0;
        timerOfProgram(d, g_job.id, g_job.kind == JOB_CANCEL_SERIES, id);
        r = count < 0 ? JF_DVR_NO_ANSWER : id[0] ? JF_DVR_DONE : JF_DVR_REFUSED;
    }
    if (r == JF_DVR_DONE) {
        switch (g_job.kind) {
        case JOB_RECORD: r = jfSyncDvrRecord(id, false); break;
        case JOB_RECORD_SERIES: r = jfSyncDvrRecord(id, true); break;
        case JOB_CANCEL: r = jfSyncDvrCancel(id); break;
        case JOB_CANCEL_SERIES: r = jfSyncDvrCancelSeries(id); break;
        case JOB_DELETE: r = jfSyncDvrDelete(id); break;
        default: break;
        }
    }
    g_job.result = r;
    g_job.fetched = jfSyncDvrFetch(d);
    __atomic_store_n(&g_job.busy, false, __ATOMIC_RELEASE);
    return NULL;
}

// Asks the server something. False (said) if it's still busy with the last.
static bool startJob(App *app, JobKind kind, const char *id, bool by_program, const char *name, bool on_now,
                     int64_t until, uint64_t now)
{
    if (g_job.started) {
        if (kind != JOB_FETCH)
            appShowMessage(app, T("Wait a moment: the server is still answering."), now);
        return false;
    }
    if (!g_job.lists)
        g_job.lists = calloc(1, sizeof(DvrLists));
    if (!g_job.lists)
        return false;
    g_job.kind = kind;
    snprintf(g_job.id, sizeof(g_job.id), "%s", id);
    snprintf(g_job.name, sizeof(g_job.name), "%s", name);
    g_job.by_program = by_program;
    g_job.on_now = on_now;
    g_job.until = until;
    bool program = by_program || kind == JOB_RECORD || kind == JOB_RECORD_SERIES;
    snprintf(g_job.program, sizeof(g_job.program), "%s", program ? id : "");
    g_job.busy = true;
    g_job.started = pthread_create(&g_job.thread, NULL, work, NULL) == 0;
    if (!g_job.started)
        g_job.busy = false;
    return g_job.started;
}

// What came of a change, in the notice bubble.
static void tell(App *app, uint64_t now)
{
    char line[TEXT_MAX + LIVE_NAME_MAX];
    const char *fmt = NULL;
    NoticeKind kind = NOTICE_ERROR;
    if (g_job.kind == JOB_FETCH) {  // Said only when the list shows and didn't come.
        if (!g_job.fetched && app->dvr_open)
            appNotify(app, kind, T("The server can't be reached."), now);
        return;
    }
    switch (g_job.result) {
    case JF_DVR_DONE:
        kind = NOTICE_INFO;
        switch (g_job.kind) {
        case JOB_RECORD: fmt = g_job.on_now ? T("Recording %s now.") : T("The server will record %s."); break;
        case JOB_RECORD_SERIES: fmt = T("The server will record every episode of %s."); break;
        case JOB_CANCEL:
            fmt = g_job.on_now ? T("Recording stopped: %s. What was recorded stays in Recordings.")
                               : T("%s won't be recorded.");
            break;
        case JOB_CANCEL_SERIES: fmt = T("The series %s won't be recorded."); break;
        case JOB_DELETE: fmt = T("Recording deleted: %s"); break;
        default: return;
        }
        break;
    case JF_DVR_NOT_ALLOWED:
        fmt = g_job.kind == JOB_DELETE ? T("Your user can't delete recordings on this server.")
                                       : T("Your user can't record on this server (it needs Live TV management).");
        break;
    case JF_DVR_REFUSED:
        fmt = g_job.kind == JOB_RECORD || g_job.kind == JOB_RECORD_SERIES
                  ? T("The server didn't take it. Has its Live TV a folder for recordings?")
                  : T("The server didn't take it: look at its list again.");
        break;
    default: fmt = T("The server can't be reached."); break;
    }
    snprintf(line, sizeof(line), fmt, g_job.name);
    appNotify(app, kind, line, now);
}

static void settle(App *app, int dir);

void appDvrUpdate(App *app, uint64_t now)
{
    if (!g_job.started || __atomic_load_n(&g_job.busy, __ATOMIC_ACQUIRE))
        return;
    pthread_join(g_job.thread, NULL);
    g_job.started = false;
    appLog("  DVR: job %d %s: result %d, lists %s (%d timers)", (int)g_job.kind, g_job.id, (int)g_job.result,
           g_job.fetched ? "read" : "not read", g_job.lists ? g_job.lists->timer_count : -1);
    if (g_job.result == JF_DVR_DONE && g_job.program[0] && g_job.until)
        dvrKnow(&g_known, g_job.program, g_job.kind == JOB_RECORD || g_job.kind == JOB_RECORD_SERIES, g_job.until);
    if (g_job.fetched) {  // The lists swap: the thread's become the app's.
        DvrLists *old = app->dvr;
        app->dvr = g_job.lists;
        g_job.lists = old;
        dvrMarkGuide(&app->live_server, app->dvr->timers, app->dvr->timer_count);
        dvrMarkGuide(&app->live, app->dvr->timers, app->dvr->timer_count);
        settle(app, 1);
    }
    dvrMarkKnown(&app->live_server, &g_known, (int64_t)time(NULL));
    dvrMarkKnown(&app->live, &g_known, (int64_t)time(NULL));
    tell(app, now);
    if (g_job.kind != JOB_FETCH)
        appLiveReadSoon(app);  // Programs a series added are marked too.
}

void appDvrExit(App *app)
{
    if (g_job.started)
        pthread_join(g_job.thread, NULL);
    g_job.started = false;
    free(g_job.lists);
    g_job.lists = NULL;
    free(app->dvr);
    app->dvr = NULL;
}

bool appDvrScheduled(const App *app, const LiveProgram *p)
{
    int known = dvrKnownOf(&g_known, p->id, (int64_t)time(NULL));
    if (known >= 0)
        return known == 1;
    return (p->marks & LIVE_TIMER) || (app->dvr && dvrTimerFor(app->dvr, p->id));
}

// The server is being asked about program_id now.
static bool askingAbout(const char *program_id)
{
    return g_job.started && program_id[0] && !strcmp(g_job.program, program_id);
}

void appDvrMarkGuide(LiveGuide *g)
{
    dvrMarkKnown(g, &g_known, (int64_t)time(NULL));
}

// --- What to do with a program (R) ---

static void openChoices(App *app, const LiveProgram *p, const char *channel, uint64_t now)
{
    int64_t now_s = (int64_t)time(NULL);
    LiveProgram marked = *p;
    if (appDvrScheduled(app, p))
        marked.marks |= LIVE_TIMER;
    app->dvr_choice_count = dvrChoices(&marked, now_s, app->dvr_choices);
    if (app->dvr_choice_count == 0) {
        appShowMessage(app, T("This program can't be recorded."), now);
        return;
    }
    app->dvr_program = marked;
    snprintf(app->dvr_channel, sizeof(app->dvr_channel), "%s", channel);
    app->dvr_choice_cursor = 0;
    app->dvr_choices_open = true;
}

static void choose(App *app, int row, uint64_t now)
{
    if (row < 0 || row >= app->dvr_choice_count)
        return;
    const LiveProgram *p = &app->dvr_program;
    int64_t now_s = (int64_t)time(NULL);
    bool on_now = p->start <= now_s;
    static const JobKind KINDS[DVR_DO_COUNT] = {JOB_RECORD, JOB_RECORD_SERIES, JOB_CANCEL, JOB_CANCEL_SERIES};
    DvrAction a = app->dvr_choices[row];
    bool by_program = a == DVR_DO_CANCEL || a == DVR_DO_CANCEL_SERIES;
    if (startJob(app, KINDS[a], p->id, by_program, p->title, on_now, p->end, now))
        app->dvr_choices_open = false;
}

static void choicesInput(App *app, uint64_t pressed, TouchEvent touch, uint64_t now)
{
    pressed = buttonsRightStickAsDpad(pressed);
    int *cursor = &app->dvr_choice_cursor;
    if ((pressed & BUTTONS_UP) && *cursor > 0)
        (*cursor)--;
    if ((pressed & BUTTONS_DOWN) && *cursor + 1 < app->dvr_choice_count)
        (*cursor)++;
    if (pressed & HidNpadButton_A)
        choose(app, *cursor, now);
    if (pressed & (HidNpadButton_B | HidNpadButton_R))
        app->dvr_choices_open = false;
    if (touch.type == TOUCH_TAP) {
        LibHit hit = uiLibraryTap(touch.x, touch.y);
        if (hit.kind == LIBHIT_LIVE_CHOICE) {
            *cursor = hit.a;
            choose(app, hit.a, now);
        } else if (hit.kind == LIBHIT_CLOSE) {
            app->dvr_choices_open = false;
        }
    }
}

// The program under the guide's cursor, and its channel; NULL if none.
static const LiveProgram *focusedProgram(const App *app, const LiveChannel **channel)
{
    int row = app->view.live.cursor;
    if (row < 0 || row >= app->live.count)
        return NULL;
    const LiveChannel *c = &app->live.channels[row];
    int64_t now_s = (int64_t)time(NULL), at = app->view.live_at > now_s ? app->view.live_at : now_s;
    int focus = liveProgramAt(c, at);
    *channel = c;
    return focus >= 0 ? &c->programs[focus] : NULL;
}

void appDvrDrawChoices(const App *app)
{
    if (!app->dvr_choices_open)
        return;
    const char *labels[DVR_DO_COUNT];
    bool on_now = app->dvr_program.start <= (int64_t)time(NULL);
    for (int i = 0; i < app->dvr_choice_count; i++)  // What's on now and recording: it stops.
        labels[i] = on_now && app->dvr_choices[i] == DVR_DO_CANCEL ? T("Stop recording")
                                                                   : dvrActionLabel(app->dvr_choices[i]);
    char when[48], line[TEXT_MAX];
    liveTimeRange(&app->dvr_program, appUtcOffset(), app->config.clock_24h, when, sizeof(when));
    snprintf(line, sizeof(line), "%s  \xC2\xB7  %s", when, app->dvr_channel);
    uiDrawLiveChoices(app->dvr_program.title, line, labels, app->dvr_choice_count, app->dvr_choice_cursor);
}

// --- The Recordings list (L) ---

typedef enum { SECTION_TIMERS, SECTION_SERIES, SECTION_RECORDED, SECTION_COUNT } Section;

static const char *const SECTION_NAMES[SECTION_COUNT] = {N_("SCHEDULED"), N_("SERIES RECORDED"), N_("RECORDED")};

typedef struct {
    Section section[ROWS_MAX];
    int index[ROWS_MAX];  // In its section's list; -1 for a heading.
    int count;
} Rows;

static void buildRows(const App *app, Rows *r)
{
    r->count = 0;
    if (!app->dvr)
        return;
    const DvrLists *d = app->dvr;
    int counts[SECTION_COUNT] = {d->timer_count, d->series_count, d->recording_count};
    for (int s = 0; s < SECTION_COUNT; s++) {
        if (!counts[s])
            continue;
        r->section[r->count] = (Section)s;
        r->index[r->count++] = -1;
        for (int i = 0; i < counts[s] && r->count < ROWS_MAX; i++) {
            r->section[r->count] = (Section)s;
            r->index[r->count++] = i;
        }
    }
}

// The cursor on an item (the nearest going dir), with its heading in view when it's its section's first.
static void settleRows(Scroll *s, const Rows *r, int dir)
{
    if (r->count == 0) {
        *s = (Scroll){0, 0, 0};
        return;
    }
    int at = s->cursor < 0 ? 0 : s->cursor >= r->count ? r->count - 1 : s->cursor;
    for (int step = 0; step < r->count && r->index[at] < 0; step++) {
        int next = at + (dir < 0 ? -1 : 1);
        if (next < 0 || next >= r->count)
            dir = -dir;
        else
            at = next;
    }
    s->cursor = at;
    int top = at > 0 && r->index[at - 1] < 0 ? at - 1 : at;
    if (top < s->first)
        s->first = top;
    if (at >= s->first + UI_DOWNLOAD_ROWS)
        s->first = at - UI_DOWNLOAD_ROWS + 1;
    int last_first = r->count > UI_DOWNLOAD_ROWS ? r->count - UI_DOWNLOAD_ROWS : 0;
    if (s->first > last_first)
        s->first = last_first;
    s->offset = 0;
}

// The id of a row's item ("" for a heading).
static const char *rowId(const App *app, const Rows *r, int row)
{
    if (row < 0 || row >= r->count || r->index[row] < 0)
        return "";
    const DvrLists *d = app->dvr;
    int i = r->index[row];
    switch (r->section[row]) {
    case SECTION_TIMERS: return d->timers[i].id;
    case SECTION_SERIES: return d->series[i].id;
    default: return d->recordings[i].id;
    }
}

static void settle(App *app, int dir)
{
    static Rows r;
    buildRows(app, &r);
    settleRows(&app->dvr_scroll, &r, dir);
    if (app->dvr_confirm >= 0) {  // The question stays on its item, wherever it went.
        app->dvr_confirm = -1;
        for (int i = 0; i < r.count; i++)
            if (r.index[i] >= 0 && !strcmp(rowId(app, &r, i), app->dvr_confirm_id))
                app->dvr_confirm = i;
    }
}

static void openList(App *app, uint64_t now)
{
    app->dvr_open = true;
    app->dvr_confirm = -1;
    app->dvr_scroll = (Scroll){0, 0, 0};
    settle(app, 1);
    startJob(app, JOB_FETCH, "", false, "", false, 0, now);
}

static void playRecording(App *app, const DvrRecording *rec, uint64_t now)
{
    char location[LIBRARY_PATH_MAX], name[APP_NAME_MAX];
    dvrRecordingLocation(rec, location, sizeof(location));
    if (rec->episode[0])
        snprintf(name, sizeof(name), "%s \xC2\xB7 %s", rec->name, rec->episode);
    else
        snprintf(name, sizeof(name), "%s", rec->name);
    appStartVideo(app, location, name, now);
}

// A on a row: a recording plays; the others say what X does.
static void activate(App *app, const Rows *r, int row, uint64_t now)
{
    if (row < 0 || row >= r->count || r->index[row] < 0)
        return;
    if (r->section[row] == SECTION_RECORDED)
        playRecording(app, &app->dvr->recordings[r->index[row]], now);
    else
        appShowMessage(app, T("The server records it on its own. X twice cancels it."), now);
}

// X on a row: once asks (it turns red), twice cancels or deletes.
static void removeRow(App *app, const Rows *r, int row, uint64_t now)
{
    if (row < 0 || row >= r->count || r->index[row] < 0)
        return;
    const DvrLists *d = app->dvr;
    int i = r->index[row];
    const char *id = rowId(app, r, row);
    if (r->section[row] == SECTION_RECORDED && !d->recordings[i].can_delete) {
        appShowMessage(app, T("Your user can't delete recordings on this server."), now);
        return;
    }
    if (app->dvr_confirm != row || strcmp(app->dvr_confirm_id, id)) {
        app->dvr_confirm = row;
        snprintf(app->dvr_confirm_id, sizeof(app->dvr_confirm_id), "%s", id);
        return;
    }
    app->dvr_confirm = -1;
    switch (r->section[row]) {
    case SECTION_TIMERS: {
        const DvrTimer *t = &d->timers[i];
        if (startJob(app, JOB_CANCEL, id, false, t->name, t->start <= (int64_t)time(NULL), t->end, now))
            snprintf(g_job.program, sizeof(g_job.program), "%s", t->program_id);
        break;
    }
    case SECTION_SERIES: startJob(app, JOB_CANCEL_SERIES, id, false, d->series[i].name, false, 0, now); break;
    default: startJob(app, JOB_DELETE, id, false, d->recordings[i].name, false, 0, now); break;
    }
}

static void listInput(App *app, uint64_t pressed, TouchEvent touch, uint64_t now)
{
    static Rows r;
    buildRows(app, &r);
    Scroll *s = &app->dvr_scroll;
    pressed = buttonsRightStickAsDpad(pressed);
    int dy = (pressed & BUTTONS_UP) ? -1 : (pressed & BUTTONS_DOWN) ? 1 : 0;
    if (pressed & BUTTONS_PAGE_UP)
        dy = -UI_DOWNLOAD_ROWS;
    if (pressed & BUTTONS_PAGE_DOWN)
        dy = UI_DOWNLOAD_ROWS;
    if (dy) {
        s->cursor += dy;
        settleRows(s, &r, dy);
        app->dvr_confirm = -1;
    }
    if (pressed & HidNpadButton_A)
        activate(app, &r, s->cursor, now);
    if (pressed & HidNpadButton_X)
        removeRow(app, &r, s->cursor, now);
    if (pressed & (HidNpadButton_B | HidNpadButton_L))
        app->dvr_open = false;
    if (touch.type == TOUCH_TAP) {
        LibHit hit = uiLibraryTap(touch.x, touch.y);
        if (hit.kind == LIBHIT_DOWNLOAD_ROW && hit.a == s->cursor) {
            activate(app, &r, hit.a, now);
        } else if (hit.kind == LIBHIT_DOWNLOAD_ROW) {
            s->cursor = hit.a;
            settleRows(s, &r, 1);
            app->dvr_confirm = -1;
        } else if (hit.kind == LIBHIT_DOWNLOAD_DELETE) {
            s->cursor = hit.a;
            removeRow(app, &r, hit.a, now);
        } else if (hit.kind == LIBHIT_CLOSE) {
            app->dvr_open = false;
        }
    } else if (touch.type == TOUCH_DRAG) {
        scrollPx(s, -touch.dy, UI_DOWNLOAD_ROW_H, UI_DOWNLOAD_ROWS, 1, r.count);
    }
}

// "Tue 10:00 - 10:30" style: the day (if not today) and the times.
static void whenText(const App *app, int64_t start, int64_t end, char *out, size_t size)
{
    LiveProgram p = {.start = start, .end = end};
    int64_t offset = appUtcOffset(), now_s = (int64_t)time(NULL);
    char range[48];
    liveTimeRange(&p, offset, app->config.clock_24h, range, sizeof(range));
    int64_t day = (start + offset) / 86400 - (now_s + offset) / 86400;
    const char *which = day == 0 ? T("Today") : day == 1 ? T("Tomorrow") : day == -1 ? T("Yesterday") : NULL;
    if (which) {
        snprintf(out, size, "%s %s", which, range);
    } else {
        time_t t = (time_t)(start + offset);
        struct tm tm;
        gmtime_r(&t, &tm);
        snprintf(out, size, "%d/%d %s", tm.tm_mday, tm.tm_mon + 1, range);
    }
}

typedef struct {
    char title[TEXT_MAX], detail[TEXT_MAX], state[TEXT_MAX];
} RowText;

static void rowText(const App *app, Section section, int i, RowText *t, UiDownloadRow *row)
{
    const DvrLists *d = app->dvr;
    char when[96];
    row->progress = -1;
    if (section == SECTION_TIMERS) {
        const DvrTimer *x = &d->timers[i];
        whenText(app, x->start, x->end, when, sizeof(when));
        snprintf(t->title, sizeof(t->title), "%s", x->name);
        snprintf(t->detail, sizeof(t->detail), "%s  \xC2\xB7  %s%s%s", when, x->channel,
                 x->series_id[0] ? "  \xC2\xB7  " : "", x->series_id[0] ? T("Series") : "");
        const char *state = x->state == DVR_RECORDING ? T("Recording now")
                            : x->state == DVR_CONFLICT ? T("No tuner free")
                            : x->state == DVR_FAILED   ? T("Failed")
                                                       : T("Scheduled");
        snprintf(t->state, sizeof(t->state), "%s", state);
        row->done = x->state == DVR_RECORDING;
        row->failed = x->state == DVR_CONFLICT || x->state == DVR_FAILED;
    } else if (section == SECTION_SERIES) {
        const DvrSeries *x = &d->series[i];
        snprintf(t->title, sizeof(t->title), "%s", x->name);
        snprintf(t->detail, sizeof(t->detail), "%s", x->any_channel || !x->channel[0] ? T("On any channel") : x->channel);
        snprintf(t->state, sizeof(t->state), "%s", x->new_only ? T("New episodes") : T("Every episode"));
    } else {
        const DvrRecording *x = &d->recordings[i];
        whenText(app, x->start, x->end, when, sizeof(when));
        if (x->episode[0])
            snprintf(t->title, sizeof(t->title), "%s \xC2\xB7 %s", x->name, x->episode);
        else
            snprintf(t->title, sizeof(t->title), "%s", x->name);
        snprintf(t->detail, sizeof(t->detail), "%s%s%s", when, x->channel[0] ? "  \xC2\xB7  " : "", x->channel);
        int minutes = dvrMinutes(x->start, x->end);
        if (x->recording)
            snprintf(t->state, sizeof(t->state), "%s", T("Recording now"));
        else if (minutes > 0)
            snprintf(t->state, sizeof(t->state), T("%d min"), minutes);
        else
            t->state[0] = '\0';
        row->done = x->recording;
    }
    row->title = t->title;
    row->detail = t->detail;
    row->state = t->state;
}

bool appDvrDrawPanel(const App *app)
{
    if (!app->dvr_open)
        return false;
    static Rows r;
    static UiDownloadRow rows[ROWS_MAX];
    static RowText texts[ROWS_MAX];
    static char headings[SECTION_COUNT][64];
    buildRows(app, &r);
    const DvrLists *d = app->dvr;
    for (int i = 0; i < r.count; i++) {
        UiDownloadRow *row = &rows[i];
        memset(row, 0, sizeof(*row));
        if (r.index[i] < 0) {
            int n = r.section[i] == SECTION_TIMERS   ? d->timer_count
                    : r.section[i] == SECTION_SERIES ? d->series_count
                                                     : d->recording_count;
            snprintf(headings[r.section[i]], sizeof(headings[0]), "%s \xC2\xB7 %d", T(SECTION_NAMES[r.section[i]]), n);
            row->title = headings[r.section[i]];
            row->header = true;
            row->progress = -1;
        } else {
            rowText(app, r.section[i], r.index[i], &texts[i], row);
        }
    }
    char summary[TEXT_MAX];
    if (g_job.started)
        snprintf(summary, sizeof(summary), "%s", T("Asking the server..."));
    else
        snprintf(summary, sizeof(summary), "%s", serverKindName(SERVER_JELLYFIN));
    uiDrawRecordingsTop(T("Recordings"), summary);
    const char *empty = !d && g_job.started ? T("Asking the server...")
                        : !d                ? T("The server can't be reached.")
                                            : T("Nothing recorded or scheduled yet: R on a program of the guide records it.");
    uiDrawDownloadsTab(rows, r.count, &app->dvr_scroll, app->dvr_confirm, "", empty);
    return true;
}

const char *appDvrHelp(const App *app)
{
    if (app->dvr_choices_open)
        return T("[A] choose  [^v] move  [B] back");
    if (!app->dvr_open)
        return NULL;
    static Rows r;
    buildRows(app, &r);
    int row = app->dvr_scroll.cursor;
    if (row < 0 || row >= r.count || r.index[row] < 0)
        return T("[B] back to the guide");
    switch (r.section[row]) {
    case SECTION_TIMERS: return T("[X] cancel  [^v] move  [B] back to the guide");
    case SECTION_SERIES: return T("[X] stop recording the series  [^v] move  [B] back to the guide");
    default: return T("[A] play  [X] delete  [^v] move  [B] back to the guide");
    }
}

// --- On the Live tab ---

static bool isHeader(LibHitKind k)
{
    return k == LIBHIT_TAB || k == LIBHIT_MENU || k == LIBHIT_SOURCE || k == LIBHIT_DOWNLOADS;
}

static void recordFocused(App *app, uint64_t now)
{
    const LiveChannel *c = NULL;
    const LiveProgram *p = focusedProgram(app, &c);
    if (p)
        openChoices(app, p, c->name, now);
    else
        appShowMessage(app, T("No program here to record."), now);
}

bool appDvrInput(App *app, uint64_t pressed, TouchEvent touch, uint64_t now)
{
    if (!appDvrAvailable(app)) {
        app->dvr_open = app->dvr_choices_open = false;
        return false;
    }
    LibHit hit = touch.type == TOUCH_TAP ? uiLibraryTap(touch.x, touch.y) : (LibHit){LIBHIT_NONE, 0, 0};
    if (isHeader(hit.kind) || (pressed & (HidNpadButton_ZL | HidNpadButton_ZR))) {  // L and R are ours here.
        app->dvr_choices_open = false;  // Another tab, or the menu: the list closes behind it.
        app->dvr_open = false;
        return false;
    }
    if (app->dvr_choices_open) {
        choicesInput(app, pressed, touch, now);
        return true;
    }
    if (app->dvr_open) {
        if (pressed & (HidNpadButton_Plus | HidNpadButton_Minus))
            return false;  // The menu and Settings, over the list.
        listInput(app, pressed, touch, now);
        return true;
    }
    if ((pressed & HidNpadButton_L) || hit.kind == LIBHIT_DVR_RECORDINGS) {
        openList(app, now);
        return true;
    }
    if ((pressed & HidNpadButton_R) || hit.kind == LIBHIT_DVR_RECORD) {
        recordFocused(app, now);
        return true;
    }
    return false;
}

// --- In the player ---

// The Jellyfin channel playing, from the guide, or NULL.
static const LiveChannel *playingChannel(const App *app)
{
    if (!app->player || !liveIsLocation(app->playing_path) || !appDvrAvailable(app))
        return NULL;
    for (int i = 0; i < app->live.count; i++) {
        char location[LIBRARY_PATH_MAX];
        liveLocation(location, sizeof(location), app->live.channels[i].id);
        if (!strcmp(location, app->playing_path))
            return &app->live.channels[i];
    }
    return NULL;
}

// What's on now on c: its program in the guide's hours (with its marks), or else the one known.
static const LiveProgram *onNow(const LiveChannel *c, int64_t now_s)
{
    int at = liveProgramAt(c, now_s);
    if (at >= 0 && c->programs[at].start <= now_s)
        return &c->programs[at];
    return c->now.title[0] && c->now.end > now_s ? &c->now : NULL;
}

bool appDvrPlayerInput(App *app, uint64_t pressed, TouchEvent touch, uint64_t now)
{
    const LiveChannel *c = playingChannel(app);
    const LiveProgram *p = c ? onNow(c, (int64_t)time(NULL)) : NULL;
    if (!p || !p->id[0])
        return false;
    bool tapped = touch.type == TOUCH_TAP && uiLibraryTap(touch.x, touch.y).kind == LIBHIT_DVR_RECORD;
    if (!(pressed & HidNpadButton_ZR) && !tapped)
        return false;
    if (g_job.started)
        return true;  // One thing at a time: the button says the server is being asked.
    if (appDvrScheduled(app, p))  // Recording (set here or from the guide): it stops.
        startJob(app, JOB_CANCEL, p->id, true, p->title, true, p->end, now);
    else
        startJob(app, JOB_RECORD, p->id, false, p->title, true, p->end, now);
    return true;
}

void appDvrPlayerDraw(const App *app, bool bar_shown)
{
    const LiveChannel *c = playingChannel(app);
    const LiveProgram *p = c ? onNow(c, (int64_t)time(NULL)) : NULL;
    if (!bar_shown || !p || !p->id[0] || app->menu.open || app->app_menu != APP_MENU_NONE)
        return;
    if (askingAbout(p->id))
        uiDrawZrButton(T("Asking the server..."), bar_shown, LIBHIT_DVR_RECORD, false, true);
    else if (appDvrScheduled(app, p))
        uiDrawZrButton(T("Stop recording"), bar_shown, LIBHIT_DVR_RECORD, true, false);
    else
        uiDrawZrButton(T("Record"), bar_shown, LIBHIT_DVR_RECORD, false, false);
}
