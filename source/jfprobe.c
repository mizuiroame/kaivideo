#include "jfprobe.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "net.h"

#define TEST_TIMEOUT_S 8L
#define TEST_STALL_S 6L
#define ANSWER_MAX (64 * 1024)
#define LISTEN_MS 2000
#define RECEIVE_STEP_MS 250
#define DATAGRAM_MAX 2048
#define INFO_PATH "/System/Info/Public"
#define ROUTE_PROBE_ADDRESS "1.1.1.1"   // Any address off the home network: only the route to it is looked up.
#define HOME_NETWORK_MASK 0xFFFFFF00u    // A home network's usual size (/24).

static struct {
    pthread_t thread;
    bool started;
    bool done;  // Under lock.
    pthread_mutex_t lock;
    bool finding;
    char server[JF_SERVER_MAX];
    char target[INET_ADDRSTRLEN];
    JfProbeResult result;
    ServerKind kind;
} g = {.lock = PTHREAD_MUTEX_INITIALIZER};

void jfProbeSetKind(ServerKind kind)
{
    pthread_mutex_lock(&g.lock);
    g.kind = kind;
    pthread_mutex_unlock(&g.lock);
}

static void runTest(JfProbeResult *r)
{
    NetClient *client = netClientOpen();
    char url[JF_SERVER_MAX + sizeof(INFO_PATH)];
    snprintf(url, sizeof(url), "%s%s", g.server, INFO_PATH);
    NetRequest request = {.url = url, .timeout_s = TEST_TIMEOUT_S, .stall_s = TEST_STALL_S};
    NetResponse answer = {0};
    if (client)
        netFetchText(client, &request, ANSWER_MAX, &answer);
    else
        answer.problem = NET_OTHER;
    r->ok = jfConnectionVerdictFor(g.kind, answer.problem, answer.status, answer.data, g.server, r->message,
                                   sizeof(r->message));
    netResponseFree(&answer);
    netClientClose(client);
}

static long long elapsedMs(const struct timespec *since)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - since->tv_sec) * 1000LL + (now.tv_nsec - since->tv_nsec) / 1000000;
}

static bool alreadyFound(char found[][JF_SERVER_MAX], int count, const char *server)
{
    for (int i = 0; i < count; i++)
        if (!strcmp(found[i], server))
            return true;
    return false;
}

// Listens for answers to the question sent on sock: up to JF_PROBE_FOUND_MAX servers.
static int collectAnswers(int sock, char found[][JF_SERVER_MAX], char names[][JF_NAME_MAX])
{
    int count = 0;
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    while (count < JF_PROBE_FOUND_MAX && elapsedMs(&start) < LISTEN_MS) {
        char datagram[DATAGRAM_MAX + 1], sender[INET_ADDRSTRLEN], server[JF_SERVER_MAX], name[JF_NAME_MAX];
        struct sockaddr_in from;
        socklen_t from_size = sizeof(from);
        ssize_t n = recvfrom(sock, datagram, DATAGRAM_MAX, 0, (struct sockaddr *)&from, &from_size);
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            break;  // The socket failed: listening on won't help.
        if (n <= 0 || !inet_ntop(AF_INET, &from.sin_addr, sender, sizeof(sender)))
            continue;  // The step's timeout, or something unreadable.
        datagram[n] = '\0';
        if (jfDiscoveryParse(datagram, sender, server, sizeof(server), name, sizeof(name)) &&
            !alreadyFound(found, count, server)) {
            snprintf(found[count], JF_SERVER_MAX, "%s", server);
            snprintf(names[count], JF_NAME_MAX, "%s", name);
            count++;
        }
    }
    return count;
}

// This device's address on the network: the one a socket "connected" to another
// network would use (connecting a datagram socket sends nothing). 0 if unknown.
static uint32_t localAddress(void)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        return 0;
    struct sockaddr_in far = {.sin_family = AF_INET, .sin_port = htons(JF_DISCOVERY_PORT)};
    inet_pton(AF_INET, ROUTE_PROBE_ADDRESS, &far.sin_addr);
    struct sockaddr_in mine = {0};
    socklen_t size = sizeof(mine);
    uint32_t address = 0;
    if (connect(sock, (struct sockaddr *)&far, sizeof(far)) == 0 &&
        getsockname(sock, (struct sockaddr *)&mine, &size) == 0)
        address = ntohl(mine.sin_addr.s_addr);
    close(sock);
    return address;
}

static bool sendQuestion(int sock, uint32_t address)
{
    struct sockaddr_in to = {.sin_family = AF_INET, .sin_port = htons(JF_DISCOVERY_PORT)};
    to.sin_addr.s_addr = htonl(address);
    const char *question = g.kind == SERVER_EMBY ? EMBY_DISCOVERY_QUESTION : JF_DISCOVERY_QUESTION;
    return sendto(sock, question, strlen(question), 0, (struct sockaddr *)&to, sizeof(to)) > 0;
}

// Asks the target; when it's the whole network, also the home network's own
// broadcast and each of its addresses, as some networks don't pass the first.
// Returns how many questions went out.
static int sendQuestions(int sock, JfProbeResult *r)
{
    struct in_addr target;
    if (inet_pton(AF_INET, g.target, &target) != 1)
        return 0;
    int sent = sendQuestion(sock, ntohl(target.s_addr));
    uint32_t mine = localAddress();
    if (strcmp(g.target, JF_PROBE_BROADCAST) || !mine) {
        snprintf(r->detail, sizeof(r->detail), "asked %s (%d sent)", g.target, sent);
        return sent;
    }
    uint32_t network = mine & HOME_NETWORK_MASK;
    for (uint32_t host = 1; host <= ~HOME_NETWORK_MASK; host++)
        if ((network | host) != mine)
            sent += sendQuestion(sock, network | host);
    snprintf(r->detail, sizeof(r->detail), "from %u.%u.%u.%u: %d questions sent", mine >> 24, (mine >> 16) & 255,
             (mine >> 8) & 255, mine & 255, sent);
    return sent;
}

static int askNetwork(char found[][JF_SERVER_MAX], char names[][JF_NAME_MAX], JfProbeResult *r)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        return -1;
    int yes = 1;
    struct timeval step = {0, RECEIVE_STEP_MS * 1000};
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &step, sizeof(step));
    int count = sendQuestions(sock, r) > 0 ? collectAnswers(sock, found, names) : -1;
    close(sock);
    return count;
}

static void runFind(JfProbeResult *r)
{
    char found[JF_PROBE_FOUND_MAX][JF_SERVER_MAX], names[JF_PROBE_FOUND_MAX][JF_NAME_MAX];
    int count = askNetwork(found, names, r);
    r->ok = count > 0;
    if (count < 0) {
        snprintf(r->message, sizeof(r->message), "The network couldn't be asked. Type the server's address under Server.");
    } else if (count == 0) {
        snprintf(r->message, sizeof(r->message),
                 "No Jellyfin server answered on this network. Type its address under Server.");
    } else {
        snprintf(r->found, sizeof(r->found), "%s", found[0]);
        if (count == 1)
            snprintf(r->message, sizeof(r->message), "Found %s at %s.", names[0], found[0]);
        else
            snprintf(r->message, sizeof(r->message), "Found %d servers. Using %s at %s; type another under Server.",
                     count, names[0], found[0]);
    }
}

static void *probeThread(void *arg)
{
    (void)arg;
    JfProbeResult result = {.finding = g.finding};
    if (g.finding)
        runFind(&result);
    else
        runTest(&result);
    pthread_mutex_lock(&g.lock);
    g.result = result;
    g.done = true;
    pthread_mutex_unlock(&g.lock);
    return NULL;
}

static bool start(bool finding, const char *text)
{
    if (g.started)
        return false;
    g.finding = finding;
    g.done = false;
    if (finding)
        snprintf(g.target, sizeof(g.target), "%s", text);
    else
        snprintf(g.server, sizeof(g.server), "%s", text);
    g.started = pthread_create(&g.thread, NULL, probeThread, NULL) == 0;
    return g.started;
}

bool jfProbeStartTest(const char *server)
{
    return server && server[0] && start(false, server);
}

bool jfProbeStartFind(const char *target)
{
    return start(true, target);
}

bool jfProbeRunning(void)
{
    return g.started;
}

bool jfProbeTake(JfProbeResult *out)
{
    if (!g.started)
        return false;
    pthread_mutex_lock(&g.lock);
    bool done = g.done;
    if (done)
        *out = g.result;
    pthread_mutex_unlock(&g.lock);
    if (done) {
        pthread_join(g.thread, NULL);
        g.started = false;
    }
    return done;
}
