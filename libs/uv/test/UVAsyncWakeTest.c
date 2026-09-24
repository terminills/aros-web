/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    UVAsyncWakeTest -- back-to-back async wakeups against an armed timer.

    UVWorkTest proves ONE threadpool completion reaches the loop. This test
    pins the latency of the SECOND and later wakeups while a long timer keeps
    uv__io_poll (src-aros/aros.c) blocking on timer.device:

        round n: sender task Delay()s briefly, uv_async_send()s
                 -> loop wakes early from Wait(timermask|asyncmask)
                 -> AbortIO()s the timer, WaitIO() reaps the reply
                 -> async_cb runs, releases the sender for round n+1

    Every early wake leaves the timer reply port's signal bit set (WaitIO()
    returns without Wait()ing when the reply is already in). If that stale
    bit is carried into the next turn the loop mistakes it for an expiry and
    WaitIO() sleeps on the timer bit alone for the whole timeout, so round
    n+1 lands only when the timer really fires -- a Node Worker loading
    modules saw its fs completions once per 5 s interval tick. PASS = every
    round-trip well under the timer period.

    Output is mirrored to stdout and the kernel debug log (serial) so it is
    observable headlessly.
*/
#include <proto/exec.h>
#include <proto/dos.h>
#include <aros/debug.h>

#include <uv.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>

#define TLOG(...) do { printf("[UVAsyncWake] " __VA_ARGS__); printf("\n"); \
                       bug("[UVAsyncWake] " __VA_ARGS__); bug("\n"); } while (0)

#define NROUNDS         6
#define TIMER_MS        4000   /* poll timeout the loop blocks with */
#define SENDER_DELAY    2      /* ticks (40 ms) between rounds */
#define ROUND_LIMIT_MS  500    /* a stale-bit turn costs the full TIMER_MS */

static uv_async_t g_async;
static uv_timer_t g_timer;
static pthread_t  g_sender;

/* uv1.library exports neither uv_thread/uv_sem nor uv_hrtime: the sender is
   a plain pthread released by a polled counter, timed with CLOCK_MONOTONIC. */
static volatile int g_go = 0;
static int      g_round = 0;
static int      g_timer_fired = 0;
static uint64_t g_sent_at[NROUNDS];
static uint64_t g_seen_at[NROUNDS];

static uint64_t now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static void *sender_main(void *arg)
{
    int i;

    (void)arg;
    for (i = 0; i < NROUNDS; i++) {
        while (g_go <= i)
            Delay(1);
        Delay(SENDER_DELAY);
        g_sent_at[i] = now_ms();
        uv_async_send(&g_async);
    }
    return NULL;
}

static void async_cb(uv_async_t *handle)
{
    int i = g_round;

    (void)handle;
    if (i >= NROUNDS)
        return;
    g_seen_at[i] = now_ms();
    g_round++;
    TLOG("round %d seen after %llu ms", i,
         (unsigned long long)(g_seen_at[i] - g_sent_at[i]));

    if (g_round < NROUNDS) {
        g_go++;
    } else {
        uv_timer_stop(&g_timer);
        uv_close((uv_handle_t *)&g_timer, NULL);
        uv_close((uv_handle_t *)&g_async, NULL);
    }
}

static void timer_cb(uv_timer_t *handle)
{
    (void)handle;
    g_timer_fired = 1;
    TLOG("guard timer fired after %d rounds", g_round);
    uv_close((uv_handle_t *)&g_timer, NULL);
    uv_close((uv_handle_t *)&g_async, NULL);
}

int main(void)
{
    uv_loop_t loop;
    uint64_t worst = 0;
    int i, r;

    TLOG("libuv %s - repeated async wakeups against a %d ms timer",
         uv_version_string(), TIMER_MS);

    r = uv_loop_init(&loop);
    TLOG("uv_loop_init = %d", r);
    if (r != 0)
        return 20;

    uv_async_init(&loop, &g_async, async_cb);
    uv_timer_init(&loop, &g_timer);
    uv_timer_start(&g_timer, timer_cb, TIMER_MS, 0);

    r = pthread_create(&g_sender, NULL, sender_main, NULL);
    TLOG("pthread_create = %d", r);
    if (r != 0)
        return 20;

    g_go = 1;
    r = uv_run(&loop, UV_RUN_DEFAULT);
    TLOG("uv_run returned %d (rounds=%d timer_fired=%d)",
         r, g_round, g_timer_fired);

    /* The sender is parked on the counter if the guard timer cut us off. */
    g_go = NROUNDS;
    pthread_join(g_sender, NULL);
    uv_loop_close(&loop);

    for (i = 0; i < g_round; i++) {
        uint64_t ms = g_seen_at[i] - g_sent_at[i];
        if (ms > worst)
            worst = ms;
    }

    if (g_round == NROUNDS && !g_timer_fired && worst < ROUND_LIMIT_MS) {
        TLOG("PASS: %d async rounds, worst wakeup %llu ms (limit %d ms)",
             NROUNDS, (unsigned long long)worst, ROUND_LIMIT_MS);
        return 0;
    }

    TLOG("FAIL: rounds=%d/%d timer_fired=%d worst wakeup %llu ms (limit %d ms)",
         g_round, NROUNDS, g_timer_fired, (unsigned long long)worst,
         ROUND_LIMIT_MS);
    return 20;
}
