#include "smbnet.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <smb2/smb2.h>  // Before libsmb2.h, which uses its types.
#include <smb2/libsmb2.h>

#include "lang.h"

#define LIST_GROWTH 64
#define PATH_MAX_SMB 1024

struct SmbFile {
    struct smb2_context *smb2;
    struct smb2fh *fh;
    int64_t size;
    uint32_t max_read;
};

// What a failure means, for the person. libsmb2's codes don't say it (a
// wrong password comes back as -ECONNREFUSED, nobody there as -1): its
// message does, with the server's NT status in it.
static pthread_mutex_t g_error_lock = PTHREAD_MUTEX_INITIALIZER;
static char g_last_error[256];

void smbLastError(char *out, size_t size)
{
    pthread_mutex_lock(&g_error_lock);
    snprintf(out, size, "%s", g_last_error);
    pthread_mutex_unlock(&g_error_lock);
}

static void explain(struct smb2_context *smb2, int rc, char *why, size_t size)
{
    const char *error = smb2 ? smb2_get_error(smb2) : "";
    pthread_mutex_lock(&g_error_lock);
    snprintf(g_last_error, sizeof(g_last_error), "%d: %s", rc, error);
    pthread_mutex_unlock(&g_error_lock);
    const char *text;
    if (strstr(error, "LOGON_FAILURE") || strstr(error, "ACCOUNT_") || strstr(error, "PASSWORD_") ||
        strstr(error, "WRONG_PASSWORD"))
        text = T("Wrong user name or password.");
    else if (strstr(error, "BAD_NETWORK_NAME"))
        text = T("That shared folder isn't on the computer. Check the share's name after the computer's.");
    else if (strstr(error, "OBJECT_NAME_NOT_FOUND") || strstr(error, "OBJECT_PATH_NOT_FOUND") ||
             strstr(error, "NO_SUCH_FILE") || strstr(error, "NOT_A_DIRECTORY"))
        text = T("That folder isn't on the server.");
    else if (strstr(error, "ACCESS_DENIED"))
        text = T("The server doesn't let this user see that folder.");
    else if (strstr(error, "Invalid address") || strstr(error, "resolve"))
        text = T("The name in the address couldn't be found. Check it, or use the server's IP address.");
    else if (rc == -ETIMEDOUT || strstr(error, "imed out") || strstr(error, "imeout"))
        text = T("The server didn't answer in time. Is the console on the same network as the server?");
    else if (strstr(error, "connect failed") || strstr(error, "Socket"))
        text = T("Nothing answered at that address. Is the computer on, and is the folder shared?");
    else if (rc == -ENOENT)
        text = T("That folder isn't on the server.");
    else {  // Unknown: its own words, to find out why.
        char shown[160];
        snprintf(shown, sizeof(shown), "%s", error[0] ? error : "?");
        shown[strcspn(shown, "\r\n")] = '\0';
        snprintf(why, size, T("The shared folder couldn't be opened (%d: %s)."), rc, shown);
        return;
    }
    snprintf(why, size, "%s", text);
}

// A connection to the account's share. NULL with why if it can't be made.
static struct smb2_context *connectShare(const DavAccount *a, char *why, size_t why_size)
{
    char server[DAV_ADDRESS_MAX], share[DAV_ADDRESS_MAX], folder[DAV_ADDRESS_MAX];
    if (!davSmbParts(a->address, server, sizeof(server), share, sizeof(share), folder, sizeof(folder))) {
        snprintf(why, why_size, "%s", T("That isn't a network folder's address."));
        return NULL;
    }
    struct smb2_context *smb2 = smb2_init_context();
    if (!smb2) {
        snprintf(why, why_size, "%s", T("Out of memory."));
        return NULL;
    }
    char domain[DAV_TEXT_MAX], name[DAV_TEXT_MAX];
    davSplitUser(a->user, domain, sizeof(domain), name, sizeof(name));
    smb2_set_timeout(smb2, SMB_TIMEOUT_S);
    smb2_set_security_mode(smb2, SMB2_NEGOTIATE_SIGNING_ENABLED);
    if (domain[0])
        smb2_set_domain(smb2, domain);
    if (name[0]) {
        smb2_set_user(smb2, name);
        smb2_set_password(smb2, a->password);
    } else {
        smb2_set_user(smb2, "guest");  // Without a user: the share's guest access.
        smb2_set_password(smb2, "");
    }
    int rc = smb2_connect_share(smb2, server, share, name[0] ? name : "guest");
    if (rc < 0) {
        explain(smb2, rc, why, why_size);
        smb2_destroy_context(smb2);
        return NULL;
    }
    return smb2;
}

// --- Listing, on a connection kept between folders (a library's scan asks for many) ---

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static struct smb2_context *g_list;
static DavAccount g_list_account;  // Whose g_list is.

static void dropList(void)
{
    if (g_list) {
        smb2_disconnect_share(g_list);
        smb2_destroy_context(g_list);
    }
    g_list = NULL;
    memset(&g_list_account, 0, sizeof(g_list_account));
}

void smbForget(void)
{
    pthread_mutex_lock(&g_lock);
    dropList();
    pthread_mutex_unlock(&g_lock);
}

static bool add(DavEntry **items, size_t *count, size_t *capacity, const struct smb2dirent *d)
{
    if (*count >= DAV_LISTING_MAX)
        return true;
    if (*count == *capacity) {
        size_t grown = *capacity ? *capacity * 2 : LIST_GROWTH;
        DavEntry *more = realloc(*items, grown * sizeof(DavEntry));
        if (!more)
            return false;
        *items = more;
        *capacity = grown;
    }
    DavEntry *e = &(*items)[(*count)++];
    memset(e, 0, sizeof(*e));
    snprintf(e->name, sizeof(e->name), "%s", d->name);
    e->is_dir = d->st.smb2_type == SMB2_TYPE_DIRECTORY;
    e->size = e->is_dir ? -1 : (int64_t)d->st.smb2_size;
    e->modified = (int64_t)d->st.smb2_mtime;
    return true;
}

// A name a line-based file can keep, and not "." or "..".
static bool usable(const char *name)
{
    size_t length = strlen(name);
    if (!length || length >= DAV_NAME_MAX || !strcmp(name, ".") || !strcmp(name, ".."))
        return false;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++)
        if (*p < 0x20 || *p == 0x7f)
            return false;
    return true;
}

// Under g_lock, with g_list connected.
static bool listOnce(const char *path, DavEntry **out, size_t *count, char *why, size_t why_size)
{
    struct smb2dir *dir = smb2_opendir(g_list, path);
    if (!dir) {
        explain(g_list, -ENOENT, why, why_size);
        return false;
    }
    size_t capacity = 0;
    bool ok = true;
    struct smb2dirent *d;
    while (ok && (d = smb2_readdir(g_list, dir)) != NULL)
        if (usable(d->name) && d->st.smb2_type != SMB2_TYPE_LINK)
            ok = add(out, count, &capacity, d);
    smb2_closedir(g_list, dir);
    if (!ok)
        snprintf(why, why_size, "%s", T("Out of memory."));
    return ok;
}

static bool sameAccount(const DavAccount *a, const DavAccount *b)
{
    return !strcmp(a->address, b->address) && !strcmp(a->user, b->user) && !strcmp(a->password, b->password);
}

bool smbList(const DavAccount *a, const char *location, DavEntry **out, size_t *count, char *why, size_t why_size)
{
    *out = NULL;
    *count = 0;
    char path[PATH_MAX_SMB];
    if (!davSmbPath(a->address, location, path, sizeof(path))) {
        snprintf(why, why_size, "%s", T("That folder's name is too long."));
        return false;
    }
    pthread_mutex_lock(&g_lock);
    bool ok = false;
    // A connection kept from before may have gone (the PC slept): one more try on a new one.
    for (int attempt = 0; attempt < 2 && !ok; attempt++) {
        bool fresh = false;
        if (!g_list || !sameAccount(&g_list_account, a)) {
            dropList();
            g_list = connectShare(a, why, why_size);
            if (!g_list)
                break;
            g_list_account = *a;
            fresh = true;
        }
        ok = listOnce(path, out, count, why, why_size);
        if (!ok) {
            free(*out);
            *out = NULL;
            *count = 0;
            if (fresh)
                break;  // A new connection that fails: the folder, not the connection.
            dropList();
        }
    }
    pthread_mutex_unlock(&g_lock);
    return ok;
}

// --- Reading a video ---

SmbFile *smbFileOpen(const DavAccount *a, const char *location, char *why, size_t why_size)
{
    char path[PATH_MAX_SMB];
    if (!davSmbPath(a->address, location, path, sizeof(path))) {
        snprintf(why, why_size, "%s", T("That folder's name is too long."));
        return NULL;
    }
    SmbFile *f = calloc(1, sizeof(*f));
    if (!f) {
        snprintf(why, why_size, "%s", T("Out of memory."));
        return NULL;
    }
    f->smb2 = connectShare(a, why, why_size);
    f->fh = f->smb2 ? smb2_open(f->smb2, path, O_RDONLY) : NULL;
    struct smb2_stat_64 st;
    if (f->smb2 && (!f->fh || smb2_fstat(f->smb2, f->fh, &st) < 0)) {
        explain(f->smb2, -ENOENT, why, why_size);
        smbFileClose(f);
        return NULL;
    }
    if (!f->smb2) {
        free(f);
        return NULL;
    }
    f->size = (int64_t)st.smb2_size;
    f->max_read = smb2_get_max_read_size(f->smb2);
    if (!f->max_read)
        f->max_read = 64 * 1024;
    return f;
}

int64_t smbFileSize(const SmbFile *f)
{
    return f->size;
}

int64_t smbFileRead(SmbFile *f, int64_t offset, uint8_t *to, size_t size, const volatile bool *cancel)
{
    if (offset >= f->size)
        return 0;
    if ((int64_t)size > f->size - offset)
        size = (size_t)(f->size - offset);
    size_t done = 0;
    while (done < size) {
        if (cancel && *cancel)
            return -1;
        uint32_t piece = size - done < f->max_read ? (uint32_t)(size - done) : f->max_read;
        int n = smb2_pread(f->smb2, f->fh, to + done, piece, (uint64_t)(offset + (int64_t)done));
        if (n <= 0)
            return n == 0 ? (int64_t)done : -1;
        done += (size_t)n;
    }
    return (int64_t)done;
}

void smbFileClose(SmbFile *f)
{
    if (!f)
        return;
    if (f->smb2) {
        if (f->fh)
            smb2_close(f->smb2, f->fh);
        smb2_disconnect_share(f->smb2);
        smb2_destroy_context(f->smb2);
    }
    free(f);
}
