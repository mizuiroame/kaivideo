#include "certtrust.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define ADDRESS_MAX 512
#define HOST_MAX 256

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static char g_address[TRUST_COUNT][ADDRESS_MAX];
static bool g_unchecked[TRUST_COUNT];

static int defaultPort(const char *scheme, size_t n)
{
    static const struct {
        const char *name;
        int port;
    } PORTS[] = {{"http", 80}, {"https", 443}, {"ftp", 21}, {"ftpes", 21}, {"ftps", 990}, {"sftp", 22}};
    for (size_t i = 0; i < sizeof(PORTS) / sizeof(PORTS[0]); i++)
        if (strlen(PORTS[i].name) == n && !strncasecmp(scheme, PORTS[i].name, n))
            return PORTS[i].port;
    return 0;
}

// An address's host and port. False if it has no scheme or host.
static bool hostPort(const char *url, char host[HOST_MAX], int *port)
{
    const char *sep = url ? strstr(url, "://") : NULL;
    if (!sep || sep == url)
        return false;
    const char *p = sep + 3, *end = p + strcspn(p, "/?#");
    const char *at = memchr(p, '@', (size_t)(end - p));  // A user before the host isn't part of it.
    if (at)
        p = at + 1;
    const char *host_end, *after;
    if (*p == '[') {  // [IPv6]:port
        host_end = memchr(p, ']', (size_t)(end - p));
        if (!host_end)
            return false;
        p++;
        after = host_end + 1;
    } else {
        host_end = memchr(p, ':', (size_t)(end - p));
        host_end = host_end ? host_end : end;
        after = host_end;
    }
    size_t n = (size_t)(host_end - p);
    if (n == 0 || n >= HOST_MAX)
        return false;
    memcpy(host, p, n);
    host[n] = '\0';
    *port = defaultPort(url, (size_t)(sep - url));
    if (after < end && *after == ':') {
        char *stop;
        long value = strtol(after + 1, &stop, 10);
        if (stop != end || value <= 0 || value > 65535)
            return false;
        *port = (int)value;
    }
    return true;
}

bool trustSameServer(const char *a, const char *b)
{
    char host_a[HOST_MAX], host_b[HOST_MAX];
    int port_a, port_b;
    return hostPort(a, host_a, &port_a) && hostPort(b, host_b, &port_b) && port_a == port_b &&
           !strcasecmp(host_a, host_b);
}

void trustSetAddress(TrustSource source, const char *url)
{
    if (source < 0 || source >= TRUST_COUNT)
        return;
    pthread_mutex_lock(&g_lock);
    snprintf(g_address[source], sizeof(g_address[source]), "%s", url ? url : "");
    pthread_mutex_unlock(&g_lock);
}

void trustSetChecked(TrustSource source, bool check)
{
    if (source < 0 || source >= TRUST_COUNT)
        return;
    pthread_mutex_lock(&g_lock);
    g_unchecked[source] = !check;
    pthread_mutex_unlock(&g_lock);
}

bool trustChecks(const char *url)
{
    bool check = true;
    pthread_mutex_lock(&g_lock);
    for (int s = 0; s < TRUST_COUNT && check; s++)
        check = !(g_unchecked[s] && g_address[s][0] && trustSameServer(url, g_address[s]));
    pthread_mutex_unlock(&g_lock);
    return check;
}
