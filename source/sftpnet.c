#include "sftpnet.h"

#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <libssh2.h>
#include <libssh2_sftp.h>

#include "lang.h"

#define TIMEOUT_S 8
#define TIMEOUT_MS (TIMEOUT_S * 1000L)
#define PATH_MAX_SFTP 1024
#define NAME_BYTES 1024
#define READ_PIECE (64 * 1024)
#define FINGERPRINT_BYTES 32
#define HASH_PREFIX "SHA256:"

typedef struct {
    int sock;
    LIBSSH2_SESSION *session;
    LIBSSH2_SFTP *sftp;
} Connection;

struct SftpFile {
    Connection c;
    LIBSSH2_SFTP_HANDLE *handle;
    int64_t size;
};

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static bool g_ready;
static Connection g_list = {-1, NULL, NULL};  // Kept between listings: one sign-in for a whole library scan.
static DavAccount g_list_account;

static void initOnce(void)
{
    g_ready = libssh2_init(0) == 0;
}

static void disconnect(Connection *c)
{
    if (c->sftp)
        libssh2_sftp_shutdown(c->sftp);
    if (c->session) {
        libssh2_session_disconnect(c->session, "bye");
        libssh2_session_free(c->session);
    }
    if (c->sock >= 0)
        close(c->sock);
    *c = (Connection){-1, NULL, NULL};
}

// A TCP connection to host:port, with send and receive timeouts. -1 if none.
static int connectTo(const char *host, int port, char *why, size_t why_size)
{
    char name[256], service[8];
    size_t n = strlen(host);
    if (host[0] == '[' && n > 2 && host[n - 1] == ']')
        snprintf(name, sizeof(name), "%.*s", (int)(n - 2), host + 1);  // "[fe80::1]"
    else
        snprintf(name, sizeof(name), "%s", host);
    snprintf(service, sizeof(service), "%d", port);
    struct addrinfo hints = {.ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM}, *found = NULL;
    if (getaddrinfo(name, service, &hints, &found) != 0 || !found) {
        snprintf(why, why_size, "%s",
                 T("The name in the address couldn't be found. Check it, or use the server's IP address."));
        return -1;
    }
    int sock = -1;
    for (struct addrinfo *ai = found; ai && sock < 0; ai = ai->ai_next) {
        sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (sock < 0)
            continue;
        struct timeval tv = {TIMEOUT_S, 0};
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        if (connect(sock, ai->ai_addr, ai->ai_addrlen) != 0) {
            close(sock);
            sock = -1;
        }
    }
    freeaddrinfo(found);
    if (sock < 0)
        snprintf(why, why_size, "%s",
                 T("Nothing answered at that address and port. Is the server on, and is the port right?"));
    return sock;
}

// The server's key as "SHA256:<base64>", OpenSSH's form (no '=' at the end).
static bool fingerprintOf(LIBSSH2_SESSION *s, char out[DAV_HOST_KEY_MAX])
{
    const unsigned char *hash = (const unsigned char *)libssh2_hostkey_hash(s, LIBSSH2_HOSTKEY_HASH_SHA256);
    char b64[64];
    if (!hash || !davBase64(hash, FINGERPRINT_BYTES, b64, sizeof(b64)))
        return false;
    size_t n = strlen(b64);
    while (n && b64[n - 1] == '=')
        b64[--n] = '\0';
    snprintf(out, DAV_HOST_KEY_MAX, HASH_PREFIX "%s", b64);
    return true;
}

static const char *g_kbd_password;  // For the keyboard-interactive answer; under g_kbd_lock.
static pthread_mutex_t g_kbd_lock = PTHREAD_MUTEX_INITIALIZER;

static void answerPrompts(const char *name, int name_len, const char *instruction, int instruction_len, int count,
                          const LIBSSH2_USERAUTH_KBDINT_PROMPT *prompts, LIBSSH2_USERAUTH_KBDINT_RESPONSE *responses,
                          void **abstract)
{
    (void)name, (void)name_len, (void)instruction, (void)instruction_len, (void)prompts, (void)abstract;
    for (int i = 0; i < count; i++) {
        responses[i].text = strdup(g_kbd_password ? g_kbd_password : "");
        responses[i].length = responses[i].text ? (unsigned int)strlen(responses[i].text) : 0;
    }
}

static bool signIn(Connection *c, const DavAccount *a)
{
    const char *user = a->user[0] ? a->user : "anonymous";
    char *methods = libssh2_userauth_list(c->session, user, (unsigned int)strlen(user));
    if (!methods)
        return libssh2_userauth_authenticated(c->session);  // "none" was enough.
    if (strstr(methods, "password") &&
        libssh2_userauth_password(c->session, user, a->password) == 0)
        return true;
    if (!strstr(methods, "keyboard-interactive"))
        return false;
    pthread_mutex_lock(&g_kbd_lock);
    g_kbd_password = a->password;
    bool ok = libssh2_userauth_keyboard_interactive(c->session, user, answerPrompts) == 0;
    g_kbd_password = NULL;
    pthread_mutex_unlock(&g_kbd_lock);
    return ok;
}

// Connects, checks the server's key, signs in and starts SFTP. False with why.
static bool connectSftp(Connection *c, const DavAccount *a, char new_key[DAV_HOST_KEY_MAX], char *why,
                        size_t why_size)
{
    *c = (Connection){-1, NULL, NULL};
    if (new_key)
        new_key[0] = '\0';
    pthread_once(&g_once, initOnce);
    char host[256], key[DAV_HOST_KEY_MAX];
    int port;
    if (!g_ready || !davHostPort(a->address, host, sizeof(host), &port)) {
        snprintf(why, why_size, "%s", T("The network can't be used."));
        return false;
    }
    c->sock = connectTo(host, port, why, why_size);
    if (c->sock < 0)
        return false;
    c->session = libssh2_session_init();
    if (!c->session) {
        snprintf(why, why_size, "%s", T("Out of memory"));
        disconnect(c);
        return false;
    }
    libssh2_session_set_blocking(c->session, 1);
    libssh2_session_set_timeout(c->session, TIMEOUT_MS);
    if (libssh2_session_handshake(c->session, c->sock) != 0 || !fingerprintOf(c->session, key)) {
        snprintf(why, why_size, "%s", T("That address answers, but not as an SFTP server. Is the port right?"));
        disconnect(c);
        return false;
    }
    if (a->host_key[0] && strcmp(a->host_key, key)) {  // Checked before any password is sent.
        snprintf(why, why_size, "%s",
                 T("The server's key isn't the one met before: it may be another server pretending to be it. "
                   "If you changed the server, set the address again in Settings."));
        disconnect(c);
        return false;
    }
    if (!signIn(c, a)) {
        snprintf(why, why_size, "%s", T("Wrong user name or password."));
        disconnect(c);
        return false;
    }
    c->sftp = libssh2_sftp_init(c->session);
    if (!c->sftp) {
        snprintf(why, why_size, "%s", T("The server doesn't offer SFTP to this user."));
        disconnect(c);
        return false;
    }
    if (new_key && !a->host_key[0])
        snprintf(new_key, DAV_HOST_KEY_MAX, "%s", key);
    return true;
}

static bool sameAccount(const DavAccount *a, const DavAccount *b)
{
    return !strcmp(a->address, b->address) && !strcmp(a->user, b->user) && !strcmp(a->password, b->password) &&
           !strcmp(a->host_key, b->host_key);
}

void sftpForget(void)
{
    pthread_mutex_lock(&g_lock);
    disconnect(&g_list);
    memset(&g_list_account, 0, sizeof(g_list_account));
    pthread_mutex_unlock(&g_lock);
}

static bool listOnce(Connection *c, const char *path, DavEntry **out, size_t *count, char *why, size_t why_size)
{
    LIBSSH2_SFTP_HANDLE *dir = libssh2_sftp_opendir(c->sftp, path);
    if (!dir) {
        snprintf(why, why_size, "%s", T("That folder isn't on the server, or this user can't see it."));
        return false;
    }
    DavListing l = {0};
    char name[NAME_BYTES];
    LIBSSH2_SFTP_ATTRIBUTES attrs;
    bool ok = true;
    int n;
    while (ok && (n = libssh2_sftp_readdir(dir, name, sizeof(name), &attrs)) > 0) {
        if ((size_t)n >= sizeof(name) || !strcmp(name, ".") || !strcmp(name, "..") || strchr(name, '/'))
            continue;
        DavEntry e;
        memset(&e, 0, sizeof(e));
        if ((size_t)n >= sizeof(e.name) || !davNameIsPlain(name))
            continue;
        memcpy(e.name, name, (size_t)n + 1);
        bool perms = attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS;
        e.is_dir = perms && LIBSSH2_SFTP_S_ISDIR(attrs.permissions);
        e.size = !e.is_dir && (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE) ? (int64_t)attrs.filesize : -1;
        e.modified = attrs.flags & LIBSSH2_SFTP_ATTR_ACMODTIME ? (int64_t)attrs.mtime : 0;
        ok = davListingAdd(&l, &e);
    }
    libssh2_sftp_closedir(dir);
    if (!ok || n < 0) {
        free(l.items);
        snprintf(why, why_size, "%s", T("The server's folder listing couldn't be read."));
        return false;
    }
    *out = l.items;
    *count = l.count;
    return true;
}

bool sftpList(const DavAccount *a, const char *location, DavEntry **out, size_t *count, char new_key[DAV_HOST_KEY_MAX],
              char *why, size_t why_size)
{
    *out = NULL;
    *count = 0;
    if (new_key)
        new_key[0] = '\0';
    char path[PATH_MAX_SFTP];
    if (!davServerPath(a->address, location, path, sizeof(path))) {
        snprintf(why, why_size, "%s", T("That folder's name is too long."));
        return false;
    }
    pthread_mutex_lock(&g_lock);
    bool ok = false;
    for (int attempt = 0; attempt < 2 && !ok; attempt++) {
        bool fresh = false;
        if (!g_list.sftp || !sameAccount(&g_list_account, a)) {
            disconnect(&g_list);
            if (!connectSftp(&g_list, a, new_key, why, why_size))
                break;
            g_list_account = *a;
            if (new_key && new_key[0])
                snprintf(g_list_account.host_key, sizeof(g_list_account.host_key), "%s", new_key);
            fresh = true;
        }
        ok = listOnce(&g_list, path, out, count, why, why_size);
        if (!ok && fresh)
            break;  // A new connection that fails: the folder, not the connection.
        if (!ok) {
            disconnect(&g_list);
            memset(&g_list_account, 0, sizeof(g_list_account));
        }
    }
    pthread_mutex_unlock(&g_lock);
    return ok;
}

SftpFile *sftpFileOpen(const DavAccount *a, const char *location, char new_key[DAV_HOST_KEY_MAX], char *why,
                       size_t why_size)
{
    char path[PATH_MAX_SFTP];
    if (!davServerPath(a->address, location, path, sizeof(path))) {
        snprintf(why, why_size, "%s", T("That folder's name is too long."));
        return NULL;
    }
    SftpFile *f = calloc(1, sizeof(*f));
    if (!f) {
        snprintf(why, why_size, "%s", T("Out of memory"));
        return NULL;
    }
    if (!connectSftp(&f->c, a, new_key, why, why_size)) {
        free(f);
        return NULL;
    }
    LIBSSH2_SFTP_ATTRIBUTES attrs;
    f->handle = libssh2_sftp_open(f->c.sftp, path, LIBSSH2_FXF_READ, 0);
    if (!f->handle || libssh2_sftp_fstat(f->handle, &attrs) != 0 || !(attrs.flags & LIBSSH2_SFTP_ATTR_SIZE)) {
        snprintf(why, why_size, "%s", T("That video isn't on the server any more, or this user can't read it."));
        sftpFileClose(f);
        return NULL;
    }
    f->size = (int64_t)attrs.filesize;
    return f;
}

int64_t sftpFileSize(const SftpFile *f)
{
    return f->size;
}

int64_t sftpFileRead(SftpFile *f, int64_t offset, uint8_t *to, size_t size, const volatile bool *cancel)
{
    if (offset >= f->size || !size)
        return 0;
    if ((int64_t)size > f->size - offset)
        size = (size_t)(f->size - offset);
    libssh2_sftp_seek64(f->handle, (libssh2_uint64_t)offset);
    size_t got = 0;
    while (got < size) {
        if (cancel && *cancel)
            return -1;
        size_t want = size - got < READ_PIECE ? size - got : READ_PIECE;
        ssize_t n = libssh2_sftp_read(f->handle, (char *)to + got, want);
        if (n < 0)
            return -1;
        if (n == 0)
            break;  // The end came early: the file shrank.
        got += (size_t)n;
    }
    return (int64_t)got;
}

void sftpFileClose(SftpFile *f)
{
    if (!f)
        return;
    if (f->handle)
        libssh2_sftp_close(f->handle);
    disconnect(&f->c);
    free(f);
}
