/*
    UVTimerTest -- first-green runtime smoke for the AROS libuv port.

    Creates a uv_loop, arms a 100 ms uv_timer, and runs the loop. This
    exercises the custom AROS event-loop backend (src/unix/aros.c: uv__io_poll
    timed-wait via timer.device). PASS = the timer callback fires exactly once
    and uv_run returns. Output is mirrored to both stdout and the kernel debug
    log (serial) so it is observable headlessly, matching the VHIProbe /
    Bluetooth bring-up pattern.
*/
#include <proto/exec.h>
#include <proto/dos.h>
#include <aros/debug.h>

#include <uv.h>
#include <stdio.h>

#define TLOG(...) do { printf("[UVTimer] " __VA_ARGS__); printf("\n"); \
                       bug("[UVTimer] " __VA_ARGS__); bug("\n"); } while (0)

static int g_fired = 0;

static void on_timer(uv_timer_t *handle)
{
    g_fired++;
    TLOG("timer fired (count=%d)", g_fired);
    uv_timer_stop(handle);
    uv_stop(uv_handle_get_loop((uv_handle_t *)handle));
}

int main(void)
{
    uv_loop_t loop;
    uv_timer_t timer;
    int r;

    TLOG("libuv version %s", uv_version_string());

    r = uv_loop_init(&loop);
    TLOG("uv_loop_init = %d", r);
    if (r != 0)
        return 20;

    r = uv_timer_init(&loop, &timer);
    TLOG("uv_timer_init = %d", r);

    r = uv_timer_start(&timer, on_timer, 100, 0);
    TLOG("uv_timer_start(100ms) = %d", r);

    TLOG("running loop...");
    r = uv_run(&loop, UV_RUN_DEFAULT);
    TLOG("uv_run returned %d (fired=%d)", r, g_fired);

    uv_loop_close(&loop);

    if (g_fired == 1) {
        TLOG("PASS: uv_timer fired exactly once on AROS");
        return 0;
    }

    TLOG("FAIL: timer fired %d time(s), expected 1", g_fired);
    return 20;
}
