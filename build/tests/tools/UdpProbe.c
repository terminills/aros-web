/*
 * Copyright (C) 2026, The AROS Development Team. All rights reserved.
 *
 * UdpProbe - measure how quickly AROSTCP's WaitSelect() reports a UDP
 * datagram as readable.  Chromium's QUIC stack (YouTube's googlevideo
 * delivery) sees a smoothed RTT of 1-2 s on the hosted tree where TCP
 * streams are fine, which is the signature of datagrams sitting in the
 * socket buffer until some unrelated timeout wakes the IO pump.
 *
 * Three cases, each repeated a few times, from the main Task and from a
 * pthread worker (Chromium's IO thread is a pthread with its own SocketBase):
 *   loopback   sendto() 127.0.0.1 from a second socket, WaitSelect() on the
 *              first with a 5 s timeout, recvfrom()
 *   self       same via the interface address (AROS_NET_IP), if given
 *   dns        a real A query to DNS server (default 1.1.1.1:53)
 * Prints the WaitSelect() wait in microseconds and whether the select
 * reported the socket at all (a timeout with a datagram then readable by a
 * non-blocking recvfrom() is the bug).
 *
 *   UdpProbe [DNS-SERVER] [SELF-IP]
 */
#include <exec/types.h>
#include <proto/exec.h>
#include <libraries/bsdsocket.h>
#include <proto/bsdsocket.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct Library *SocketBase;

static int failures;

static long long now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}

static int udp_socket(const char *ctx, unsigned short port)
{
    struct sockaddr_in sa;
    int sd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sd < 0) {
        printf("  [%s] socket(SOCK_DGRAM) FAIL errno %d\n", ctx, errno);
        failures++;
        return -1;
    }
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sd, (struct sockaddr *)&sa, sizeof sa) < 0) {
        printf("  [%s] bind() FAIL errno %d\n", ctx, errno);
        failures++;
        CloseSocket(sd);
        return -1;
    }
    return sd;
}

static unsigned short bound_port(int sd)
{
    struct sockaddr_in sa;
    socklen_t len = sizeof sa;
    if (getsockname(sd, (struct sockaddr *)&sa, &len) < 0) return 0;
    return ntohs(sa.sin_port);
}

/* WaitSelect() for readability with a 5 s cap, then drain one datagram. */
static void wait_and_read(const char *ctx, const char *what, int sd, long long sent_at)
{
    fd_set rd;
    struct timeval tv;
    ULONG sigmask = SIGBREAKF_CTRL_C;
    char buf[1024];
    struct sockaddr_in from;
    socklen_t flen = sizeof from;
    long long woke;
    int r, n, late = 0;

    tv.tv_sec = 5;
    tv.tv_usec = 0;
    FD_ZERO(&rd);
    FD_SET(sd, &rd);
    r = WaitSelect(sd + 1, &rd, NULL, NULL, &tv, &sigmask);
    woke = now_us();
    if (r <= 0) {
        /* Timed out (or error): is a datagram nevertheless waiting? */
        long nb = 1;
        IoctlSocket(sd, FIONBIO, (char *)&nb);
        n = recvfrom(sd, buf, sizeof buf, 0, (struct sockaddr *)&from, &flen);
        late = (n > 0);
        nb = 0;
        IoctlSocket(sd, FIONBIO, (char *)&nb);
        printf("  [%s] %-9s WaitSelect=%d after %lld us, datagram %s%s\n", ctx, what, r,
               woke - sent_at, late ? "WAS readable (select missed it)" : "absent",
               r < 0 ? " (errno set)" : "");
        failures++;
        return;
    }
    n = recvfrom(sd, buf, sizeof buf, 0, (struct sockaddr *)&from, &flen);
    printf("  [%s] %-9s wake %lld us, recvfrom %d bytes\n", ctx, what, woke - sent_at, n);
    if (n <= 0) failures++;
    fflush(stdout);
}

static void loopback_case(const char *ctx, const char *what, const char *ip)
{
    int a = udp_socket(ctx, 0), b;
    struct sockaddr_in to;
    int i;

    if (a < 0) return;
    b = udp_socket(ctx, 0);
    if (b < 0) { CloseSocket(a); return; }
    memset(&to, 0, sizeof to);
    to.sin_family = AF_INET;
    to.sin_port = htons(bound_port(a));
    to.sin_addr.s_addr = inet_addr(ip);
    for (i = 0; i < 5; i++) {
        long long t = now_us();
        if (sendto(b, "ping", 4, 0, (struct sockaddr *)&to, sizeof to) != 4) {
            printf("  [%s] %-9s sendto(%s) FAIL errno %d\n", ctx, what, ip, errno);
            failures++;
            break;
        }
        wait_and_read(ctx, what, a, t);
    }
    CloseSocket(a);
    CloseSocket(b);
}

/* Minimal DNS A query for "a." - any answer (even NXDOMAIN) is a datagram. */
static void dns_case(const char *ctx, const char *server)
{
    static const unsigned char q[] = {
        0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x01, 'a', 0x00, 0x00, 0x01, 0x00, 0x01
    };
    int sd = udp_socket(ctx, 0);
    struct sockaddr_in to;
    int i;

    if (sd < 0) return;
    memset(&to, 0, sizeof to);
    to.sin_family = AF_INET;
    to.sin_port = htons(53);
    to.sin_addr.s_addr = inet_addr(server);
    for (i = 0; i < 5; i++) {
        long long t = now_us();
        if (sendto(sd, (char *)q, sizeof q, 0, (struct sockaddr *)&to, sizeof to) != (int)sizeof q) {
            printf("  [%s] %-9s sendto(%s:53) FAIL errno %d\n", ctx, "dns", server, errno);
            failures++;
            break;
        }
        wait_and_read(ctx, "dns", sd, t);
    }
    CloseSocket(sd);
}

struct args { const char *dns; const char *self; };

static void run_cases(const char *ctx, const struct args *a)
{
    SocketBase = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4);
    if (!SocketBase) {
        printf("  [%s] OpenLibrary(bsdsocket.library) FAIL\n", ctx);
        failures++;
        return;
    }
    SetErrnoPtr(&errno, sizeof(errno));
    loopback_case(ctx, "loopback", "127.0.0.1");
    if (a->self) loopback_case(ctx, "self", a->self);
    dns_case(ctx, a->dns);
    CloseLibrary(SocketBase);
    SocketBase = NULL;
}

static void *worker(void *p)
{
    run_cases("pthread", (const struct args *)p);
    return NULL;
}

int main(int argc, char **argv)
{
    struct args a = { argc > 1 ? argv[1] : "1.1.1.1", argc > 2 ? argv[2] : NULL };
    pthread_t th;

    printf("UdpProbe dns=%s self=%s\n", a.dns, a.self ? a.self : "-");
    run_cases("main", &a);
    if (pthread_create(&th, NULL, worker, &a) == 0)
        pthread_join(th, NULL);
    else {
        printf("  pthread_create FAIL errno %d\n", errno);
        failures++;
    }
    printf("UdpProbe failures=%d\n", failures);
    return failures ? 10 : 0;
}
