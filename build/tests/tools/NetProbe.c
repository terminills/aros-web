/*
 * Copyright (C) 2026, The AROS Development Team. All rights reserved.
 *
 * NetProbe - replay what Chromium's base/aros/socket_library.cc does against
 * bsdsocket.library, from the main Task and from a pthread worker: open the
 * library per task, point its errno at ours, socket(), SIOCGIFCONF +
 * SIOCGIFFLAGS enumeration, the name-server list, gethostbyname(), and a TCP
 * connect + HTTP HEAD so DNS, routing and NAT are all exercised end to end.
 *
 *   NetProbe [HOST] [PORT]     default HOST=en.wikibooks.org PORT=80
 */
#define BSDSOCKET_ROADSHOW_DNS 1
#include <exec/types.h>
#include <exec/lists.h>
#include <proto/exec.h>
#include <libraries/bsdsocket.h>
#include <proto/bsdsocket.h>
#include <bsdsocket/socketbasetags.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct Library *SocketBase;

static int failures;

static void report(const char *ctx, const char *what, int ok, long got)
{
    printf("  [%s] %-34s %s (got %ld, errno %d)\n", ctx, what, ok ? "ok" : "FAIL", got, errno);
    if (!ok) failures++;
    fflush(stdout);
}

static void list_interfaces(const char *ctx, int sd)
{
    static char buf[16 * 1024];
    struct ifconf ifc;
    char *cur, *end;
    int n = 0;

    ifc.ifc_len = sizeof buf;
    ifc.ifc_buf = buf;
    if (IoctlSocket(sd, SIOCGIFCONF, (char *)&ifc) < 0) {
        report(ctx, "IoctlSocket(SIOCGIFCONF)", 0, -1);
        return;
    }
    cur = ifc.ifc_buf;
    end = cur + ifc.ifc_len;
    while (cur + sizeof(struct ifreq) <= end) {
        struct ifreq *ifr = (struct ifreq *)cur;
        size_t sz = sizeof(ifr->ifr_name) + ifr->ifr_addr.sa_len;
        struct ifreq q;
        char addr[32] = "?";

        if (sz < sizeof(struct ifreq)) sz = sizeof(struct ifreq);
        if (sz > (size_t)(end - cur)) break;
        if (ifr->ifr_addr.sa_family == AF_INET)
            snprintf(addr, sizeof addr, "%s",
                     Inet_NtoA(((struct sockaddr_in *)&ifr->ifr_addr)->sin_addr.s_addr));
        memset(&q, 0, sizeof q);
        memcpy(q.ifr_name, ifr->ifr_name, sizeof q.ifr_name);
        if (IoctlSocket(sd, SIOCGIFFLAGS, (char *)&q) == 0)
            printf("    if %-6s family %d addr %-15s flags 0x%x%s%s%s\n", ifr->ifr_name,
                   ifr->ifr_addr.sa_family, addr, q.ifr_flags,
                   (q.ifr_flags & IFF_UP) ? " UP" : "",
                   (q.ifr_flags & IFF_DRV_RUNNING) ? " RUNNING" : "",
                   (q.ifr_flags & IFF_LOOPBACK) ? " LOOPBACK" : "");
        else
            printf("    if %-6s family %d addr %-15s SIOCGIFFLAGS failed errno %d\n",
                   ifr->ifr_name, ifr->ifr_addr.sa_family, addr, errno);
        n++;
        cur += sz;
    }
    report(ctx, "SIOCGIFCONF interfaces", n > 0, n);
}

static void list_nameservers(const char *ctx)
{
    struct List *l = ObtainDomainNameServerList();
    struct Node *n;
    int count = 0;

    if (!l) { report(ctx, "ObtainDomainNameServerList", 0, 0); return; }
    for (n = l->lh_Head; n->ln_Succ; n = n->ln_Succ) {
        struct DomainNameServerNode *d = (struct DomainNameServerNode *)n;
        printf("    nameserver %s\n", d->dnsn_Address ? (const char *)d->dnsn_Address : "(null)");
        count++;
    }
    ReleaseDomainNameServerList(l);
    report(ctx, "name servers", count > 0, count);
}

static void probe(const char *ctx, const char *host, int port)
{
    int sd, tcp;
    struct hostent *he;
    struct sockaddr_in sa;
    char line[512];
    long r;

    SocketBase = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4);
    report(ctx, "OpenLibrary(bsdsocket.library,4)", SocketBase != NULL, (long)(IPTR)SocketBase);
    if (!SocketBase) return;
    SetErrnoPtr(&errno, sizeof(errno));

    sd = socket(AF_INET, SOCK_DGRAM, 0);
    report(ctx, "socket(AF_INET,SOCK_DGRAM)", sd >= 0, sd);
    if (sd >= 0) {
        list_interfaces(ctx, sd);
        CloseSocket(sd);
    }
    list_nameservers(ctx);

    he = gethostbyname((char *)host);
    report(ctx, "gethostbyname()", he != NULL, he ? (long)he->h_addrtype : -1);
    if (he && he->h_addr_list[0]) {
        memset(&sa, 0, sizeof sa);
        sa.sin_family = AF_INET;
        sa.sin_port = htons(port);
        memcpy(&sa.sin_addr, he->h_addr_list[0], sizeof sa.sin_addr);
        printf("    %s -> %s\n", host, Inet_NtoA(sa.sin_addr.s_addr));

        tcp = socket(AF_INET, SOCK_STREAM, 0);
        report(ctx, "socket(AF_INET,SOCK_STREAM)", tcp >= 0, tcp);
        if (tcp >= 0) {
            r = connect(tcp, (struct sockaddr *)&sa, sizeof sa);
            report(ctx, "connect()", r == 0, r);
            if (r == 0) {
                snprintf(line, sizeof line, "HEAD / HTTP/1.0\r\nHost: %s\r\n\r\n", host);
                r = send(tcp, line, strlen(line), 0);
                report(ctx, "send(HEAD)", r == (long)strlen(line), r);
                r = recv(tcp, line, sizeof line - 1, 0);
                report(ctx, "recv(response)", r > 0, r);
                if (r > 0) {
                    line[r] = 0;
                    printf("    %.*s\n", (int)strcspn(line, "\r\n"), line);
                }
            }
            CloseSocket(tcp);
        }
    }
    CloseLibrary(SocketBase);
    SocketBase = NULL;
}

struct job { const char *host; int port; };

static void *worker(void *arg)
{
    struct job *j = arg;
    probe("pthread", j->host, j->port);
    return NULL;
}

int main(int argc, char **argv)
{
    struct job j = { argc > 1 ? argv[1] : "en.wikibooks.org", argc > 2 ? atoi(argv[2]) : 80 };
    pthread_t t;

    printf("NetProbe: %s:%d\n", j.host, j.port);
    probe("main", j.host, j.port);
    if (pthread_create(&t, NULL, worker, &j) == 0)
        pthread_join(t, NULL);
    else
        report("main", "pthread_create", 0, -1);
    printf("NetProbe: %s (failures=%d)\n", failures ? "FAIL" : "ok", failures);
    return failures ? 1 : 0;
}
