// The Downloads tab: what's on its way (A pauses or lets it go on, X twice
// cancels it), what's on the card (A plays, X twice deletes the file and keeps
// it as a record), and what was deleted from the card (A downloads it again,
// X takes it off the list; Y, anywhere, or Clear clears all of them).
#include <stdio.h>
#include <string.h>

#include <switch.h>

#include "app.h"
#include "buttons.h"
#include "dlworker.h"
#include "lang.h"
#include "ui_downloads.h"
#include "ui_library.h"

#define ROWS_MAX 320    // Headings and downloads the tab lists at most.
#define TEXT_MAX 200
#define GIB (1024.0 * 1024 * 1024)
#define MIB (1024.0 * 1024)

typedef enum { SECTION_COMING, SECTION_CARD, SECTION_DELETED, SECTION_COUNT } Section;

static const char *const SECTION_NAMES[SECTION_COUNT] = {N_("DOWNLOADING"), N_("ON THE CARD"),
                                                         N_("DELETED FROM THE CARD")};

static Section sectionOf(const Download *d)
{
    return d->state == DL_DONE ? SECTION_CARD : d->state == DL_DELETED ? SECTION_DELETED : SECTION_COMING;
}

// The tab's rows: each section that has downloads, its heading and then them.
typedef struct {
    int download[ROWS_MAX];  // An index in the list, -1 for a heading.
    Section section[ROWS_MAX];
    int count;
    int in_section[SECTION_COUNT];
} Rows;

static void buildRows(const App *app, Rows *r)
{
    memset(r->in_section, 0, sizeof(r->in_section));
    for (size_t i = 0; i < app->downloads.count; i++)
        r->in_section[sectionOf(&app->downloads.items[i])]++;
    r->count = 0;
    for (int s = 0; s < SECTION_COUNT; s++) {
        if (!r->in_section[s] || r->count + 2 > ROWS_MAX)
            continue;
        r->download[r->count] = -1;
        r->section[r->count++] = (Section)s;
        for (size_t i = 0; i < app->downloads.count && r->count < ROWS_MAX; i++)
            if (sectionOf(&app->downloads.items[i]) == (Section)s) {
                r->download[r->count] = (int)i;
                r->section[r->count++] = (Section)s;
            }
    }
}

// The download under row, or NULL (a heading, or past the end).
static const Download *downloadAt(const App *app, const Rows *r, int row)
{
    return row >= 0 && row < r->count && r->download[row] >= 0 ? &app->downloads.items[r->download[row]] : NULL;
}

// The cursor on a download (the nearest one going dir, else the other way),
// and in view with the heading above it when it's the first of its section.
static void settle(Scroll *s, const Rows *r, int dir)
{
    if (r->count == 0) {
        *s = (Scroll){0, 0, 0};
        return;
    }
    int at = s->cursor < 0 ? 0 : s->cursor >= r->count ? r->count - 1 : s->cursor;
    for (int step = 0; step < r->count && r->download[at] < 0; step++) {
        int next = at + (dir < 0 ? -1 : 1);
        if (next < 0 || next >= r->count)
            dir = -dir;
        else
            at = next;
    }
    s->cursor = at;
    int top = at > 0 && r->download[at - 1] < 0 ? at - 1 : at;  // Its heading shows too.
    if (top < s->first)
        s->first = top;
    if (at >= s->first + UI_DOWNLOAD_ROWS)
        s->first = at - UI_DOWNLOAD_ROWS + 1;
    int last_first = r->count > UI_DOWNLOAD_ROWS ? r->count - UI_DOWNLOAD_ROWS : 0;
    if (s->first > last_first)
        s->first = last_first;
    s->offset = 0;
}

// The row of the download whose X was pressed once, if it's still there as it
// was then; -1 (and the question dropped) if it moved to another part or went.
static int confirmedRow(App *app, const Rows *r)
{
    if (app->downloads_confirm < 0)
        return -1;
    for (int i = 0; i < r->count; i++) {
        const Download *d = downloadAt(app, r, i);
        if (d && !strcmp(d->path, app->downloads_confirm_path) && (int)sectionOf(d) == app->downloads_confirm_state)
            return app->downloads_confirm = i;
    }
    return app->downloads_confirm = -1;
}

void appDownloadsTabChanged(App *app)
{
    static Rows r;
    buildRows(app, &r);
    settle(&app->downloads_scroll, &r, 1);
    confirmedRow(app, &r);
}

void appDownloadsOpen(App *app, uint64_t now)
{
    appLibraryRefreshHome(app);
    if (libviewShowTab(&app->view, TAB_DOWNLOADS))
        app->downloads_confirm = -1;
    else
        appShowMessage(app, T("Nothing downloaded yet: open a film or episode from a server and press L."), now);
}

bool appDownloadsScrollPx(App *app, int px)
{
    static Rows r;
    buildRows(app, &r);
    return scrollPx(&app->downloads_scroll, px, UI_DOWNLOAD_ROW_H, UI_DOWNLOAD_ROWS, 1, r.count);
}

static void videoName(const Download *d, char *out, size_t size)
{
    if (d->name.is_episode)
        snprintf(out, size, "%s \xC2\xB7 S%02dE%02d", d->name.show, d->name.season, d->name.episode);
    else
        snprintf(out, size, "%s", d->name.title);
}

// A: plays one on the card; pauses one coming, or lets it go on; one that
// failed, or was deleted, comes again.
static void activate(App *app, const Download *found, uint64_t now)
{
    Download d = *found;  // The list may change under these calls.
    switch (d.state) {
    case DL_DONE: {
        char name[APP_NAME_MAX];
        videoName(&d, name, sizeof(name));
        appStartVideo(app, d.path, name, now);
        return;
    }
    case DL_QUEUED:
    case DL_ACTIVE: dlWorkerPause(d.path, true); break;
    case DL_PAUSED: dlWorkerPause(d.path, false); break;
    default: dlWorkerRetry(d.path); appShowMessage(app, T("Downloading it again."), now); break;
    }
    appDownloadsRefresh(app);
}

// X: once asks, again on the same row does it. On its way: cancelled (what
// came is deleted). On the card: its file deleted, kept as a record. A record:
// taken off the list (nothing to delete, so at once).
static void pressX(App *app, int row, const Download *found, uint64_t now)
{
    Download d = *found;
    Section section = sectionOf(&d);
    bool same = app->downloads_confirm >= 0 && !strcmp(app->downloads_confirm_path, d.path) &&
                app->downloads_confirm_state == (int)section;
    if (section != SECTION_DELETED && !same) {
        app->downloads_confirm = row;
        snprintf(app->downloads_confirm_path, sizeof(app->downloads_confirm_path), "%s", d.path);
        app->downloads_confirm_state = (int)section;
        appShowMessage(app, section == SECTION_CARD ? T("Press X again to delete it from the card.")
                                                    : T("Press X again to cancel it."),
                       now);
        return;
    }
    app->downloads_confirm = -1;
    if (section == SECTION_CARD) {
        dlWorkerDeleteFile(d.path);
        appNotify(app, NOTICE_DONE, T("Deleted from the card. It stays under Deleted, to download again."), now);
        if (!app->config.use_jellyfin)
            appLibraryRescan(app, now);  // The card's library lets it go.
    } else if (section == SECTION_DELETED) {
        dlWorkerForget(d.path);
        appNotify(app, NOTICE_DONE, T("Taken off the list."), now);
    } else {
        dlWorkerRemove(d.path);
        appNotify(app, NOTICE_DONE, T("Download cancelled."), now);
    }
    appDownloadsRefresh(app);
}

// Y (or Clear), anywhere in the tab: the record of deleted ones goes
// (nothing is on the card for them).
static void clearDeleted(App *app, uint64_t now)
{
    int n = dlWorkerForgetDeleted();
    if (n == 0)
        return;
    char message[96];
    snprintf(message, sizeof(message),
             n == 1 ? T("Cleared %d deleted download from the list.")
                    : T("Cleared %d deleted downloads from the list."),
             n);
    appNotify(app, NOTICE_DONE, message, now);
    appDownloadsRefresh(app);
}

void appDownloadsInput(App *app, uint64_t pressed, TouchEvent touch, uint64_t now)
{
    static Rows r;
    buildRows(app, &r);
    Scroll *s = &app->downloads_scroll;
    if (pressed & (BUTTONS_UP | BUTTONS_DOWN)) {
        int dir = (pressed & BUTTONS_UP) ? -1 : 1;
        int at = s->cursor + dir;
        while (at >= 0 && at < r.count && r.download[at] < 0)
            at += dir;  // Past a heading.
        if (at >= 0 && at < r.count)
            s->cursor = at;
        settle(s, &r, dir);
        app->downloads_confirm = -1;
    }
    const Download *d = downloadAt(app, &r, s->cursor);
    if (d && (pressed & HidNpadButton_A))
        activate(app, d, now);
    else if (d && (pressed & HidNpadButton_X))
        pressX(app, s->cursor, d, now);
    if (pressed & HidNpadButton_Y)
        clearDeleted(app, now);
    if (pressed & (HidNpadButton_A | HidNpadButton_X | HidNpadButton_Y))
        buildRows(app, &r);  // A press may have changed the list: a tap goes by the rows now.
    if (touch.type == TOUCH_DRAG) {
        appLibraryDrag(app, touch, now);
        return;
    }
    if (touch.type != TOUCH_TAP)
        return;
    LibHit hit = uiLibraryTap(touch.x, touch.y);
    const Download *tapped = downloadAt(app, &r, hit.a);
    if (hit.kind == LIBHIT_DOWNLOAD_ROW && tapped) {
        s->cursor = hit.a;
        app->downloads_confirm = -1;
        activate(app, tapped, now);
    } else if (hit.kind == LIBHIT_DOWNLOAD_DELETE && tapped) {
        s->cursor = hit.a;
        pressX(app, hit.a, tapped, now);
    } else if (hit.kind == LIBHIT_DOWNLOADS_CLEAR) {
        clearDeleted(app, now);
    }
}

const char *appDownloadsHelp(const App *app)
{
    static Rows r;
    static char help[320];
    buildRows(app, &r);
    const Download *d = downloadAt(app, &r, app->downloads_scroll.cursor);
    const char *row = "";
    if (d) {
        switch (sectionOf(d)) {
        case SECTION_CARD: row = T("[A] play  [X] delete from the card"); break;
        case SECTION_DELETED: row = T("[A] download again  [X] take off the list"); break;
        default: row = d->state == DL_FAILED ? T("[A] try again  [X] cancel") : T("[A] pause / go on  [X] cancel"); break;
        }
    }
    // The pieces have no spaces at their ends (a translator's editor may drop them): they're put between here.
    const char *clear = r.in_section[SECTION_DELETED] ? T("[Y] clear deleted") : "";
    snprintf(help, sizeof(help), "%s%s%s%s%s", row, row[0] ? "  " : "", clear, clear[0] ? "  " : "",
             T("[ZL][ZR] tabs  [+] menu"));
    return help;
}

// --- Drawing ---

typedef struct {
    char title[TEXT_MAX], detail[TEXT_MAX], state[TEXT_MAX];
} RowText;

static void sizeText(int64_t bytes, char *out, size_t size)
{
    if (bytes >= GIB)
        snprintf(out, size, "%.1f GB", bytes / GIB);
    else
        snprintf(out, size, "%.0f MB", bytes / MIB);
}

// "Saving as MKV · 42%" while a conversion is rewrapped ("Finishing..." until it says how far).
static void finishingText(const Download *d, char *out, size_t size)
{
    if (d->finish_fraction > 0.0f)
        snprintf(out, size, T("Saving as MKV \xC2\xB7 %d%%"), (int)(d->finish_fraction * 100));
    else
        snprintf(out, size, "%s", T("Finishing..."));
}

static void stateText(const Download *d, char *out, size_t size)
{
    char done[32], total[32];
    int percent = (int)(dlProgress(d) * 100);
    sizeText(d->done, done, sizeof(done));
    sizeText(d->size, total, sizeof(total));
    switch (d->state) {
    case DL_QUEUED: snprintf(out, size, "%s", d->error[0] ? d->error : T("Waiting")); break;
    case DL_ACTIVE:
        if (d->finishing)
            finishingText(d, out, size);
        else if (d->getting_subtitles)
            snprintf(out, size, "%s", T("Getting subtitles..."));
        else if (d->max_bitrate > 0 && d->part_count > 0)
            snprintf(out, size, T("Converting \xC2\xB7 %d%% \xC2\xB7 %s"), percent, done);
        else if (d->size > 0)
            snprintf(out, size, T("%s of %s"), done, total);
        else
            snprintf(out, size, "%s", T("Starting..."));
        break;
    case DL_PAUSED: snprintf(out, size, T("Paused \xC2\xB7 %d%%"), percent); break;
    case DL_FAILED: snprintf(out, size, "%s", d->error[0] ? d->error : T("It stopped.")); break;
    case DL_DELETED: snprintf(out, size, "%s", T("Deleted from the card")); break;
    default:
        if (d->finishing)
            finishingText(d, out, size);
        else
            snprintf(out, size, "%s", total);
        break;
    }
}

static void describe(const Download *d, RowText *t, UiDownloadRow *row)
{
    if (d->name.is_episode) {
        snprintf(t->title, sizeof(t->title), "%s", d->name.title[0] ? d->name.title : d->name.show);
        snprintf(t->detail, sizeof(t->detail), "%s \xC2\xB7 S%02dE%02d", d->name.show, d->name.season, d->name.episode);
    } else {
        snprintf(t->title, sizeof(t->title), d->name.year ? "%s (%d)" : "%s", d->name.title, d->name.year);
        snprintf(t->detail, sizeof(t->detail), "%s", T("Film"));
    }
    if (d->max_bitrate > 0) {  // Converted by the server: at what bitrate.
        size_t n = strlen(t->detail);
        snprintf(t->detail + n, sizeof(t->detail) - n, T(" \xC2\xB7 converted, %.0f Mb/s"), d->max_bitrate / 1e6);
    }
    stateText(d, t->state, sizeof(t->state));
    double progress = dlProgress(d);
    bool coming = sectionOf(d) == SECTION_COMING;
    *row = (UiDownloadRow){t->title, t->detail, t->state, coming && progress > 0 ? progress : -1,
                           d->state == DL_DONE, d->state == DL_FAILED, false, d->state == DL_DELETED};
}

static void summaryText(const App *app, const Rows *r, char *out, size_t size)
{
    int64_t on_card = 0;
    for (size_t i = 0; i < app->downloads.count; i++)
        if (app->downloads.items[i].state == DL_DONE)
            on_card += app->downloads.items[i].size;
    char bytes[32];
    sizeText(on_card, bytes, sizeof(bytes));
    int n = snprintf(out, size, T("%d downloading \xC2\xB7 %d on the card (%s) \xC2\xB7 %d deleted"),
                     r->in_section[SECTION_COMING], r->in_section[SECTION_CARD], bytes, r->in_section[SECTION_DELETED]);
    int untold = dlWorkerSyncWaiting();
    if (untold > 0 && n > 0 && (size_t)n < size)
        snprintf(out + n, size - (size_t)n, T(" \xC2\xB7 %d watched offline, told to the server when it's back"),
                 untold);
}

void appDownloadsDraw(const App *app)
{
    static Rows r;
    static RowText texts[ROWS_MAX];
    static char headings[SECTION_COUNT][96];
    static UiDownloadRow rows[ROWS_MAX];
    buildRows(app, &r);
    for (int i = 0; i < r.count; i++) {
        if (r.download[i] >= 0) {
            describe(&app->downloads.items[r.download[i]], &texts[i], &rows[i]);
            continue;
        }
        Section s = r.section[i];
        snprintf(headings[s], sizeof(headings[s]), "%s \xC2\xB7 %d", T(SECTION_NAMES[s]), r.in_section[s]);
        rows[i] = (UiDownloadRow){.title = headings[s], .detail = "", .state = "", .progress = -1, .header = true,
                                  .action = s == SECTION_DELETED ? T("Clear") : NULL};
    }
    char summary[TEXT_MAX];
    summaryText(app, &r, summary, sizeof(summary));
    uiDrawDownloadsTab(rows, r.count, &app->downloads_scroll, app->downloads_confirm, summary,
                       T("Nothing yet: open a film or episode from a server and press L to download it."));
}
