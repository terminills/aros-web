/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    AROS custom event-loop backend for libuv (the src/unix/aros.c equivalent).

    Core-first phase: no POSIX socket layer yet, so uv__io_poll() has no fds to
    wait on. It blocks up to `timeout` ms via timer.device and wakes early on
    the loop's exec async-signal (uv_async / threadpool completion). Socket fd
    readiness (WaitSelect over bsdsocket) is added in the networking phase.

    Replaces linux-core.c / kqueue.c / posix-poll.c.

    Exec resources belong to the task that RUNS the loop, not the one that
    initialised it (uv__aros_loop_bind below).
*/
#include "uv.h"
#include "internal.h"

#include <proto/dos.h>
#include <proto/exec.h>
#include <exec/tasks.h>
#include <exec/io.h>
#include <exec/ports.h>
#include <devices/timer.h>
#include <unistd.h>

/* Real AROS uv_async (src-aros/aros-async.c): drain pending async handles. */
void uv__aros_async_drain(uv_loop_t* loop);

/* bsdsocket readiness layer (src-aros/aros-net.c). */
int uv__aros_is_socket(int fd);
int uv__aros_is_dos_pipe(int fd);
int uv__aros_dos_pipe_ready(int fd);
int uv__aros_waitselect(const int* fds, const int* wanted, int* revents,
                        int nfds, int timeout_ms, int asyncsig, int* async_fired);
void uv__aros_process_reap(uv_loop_t* loop);
void uv__aros_net_loop_init(void);
void uv__aros_net_loop_delete(void);

#define UV_AROS_POLL_MAX 64   /* bsdsocket FD_SETSIZE */
#define UV_AROS_TTY_POLL_MS 10

static unsigned int uv__aros_threadpool_pending(uv_loop_t* loop)
{
    unsigned int pending;

    uv_mutex_lock(&loop->wq_mutex);
    pending = !uv__queue_empty(&loop->wq);
    uv_mutex_unlock(&loop->wq_mutex);

    return pending;
}

static int uv__aros_tty_ready(uv__io_t* watcher)
{
    /*
     * The event-loop task's fd 0 is its DOS Input() handle. KCON/CON provide
     * asynchronous device input behind that handle, and WaitForChar(0) is the
     * non-blocking readiness query DOS exposes for it. Other interactive
     * descriptors need a future descriptor-to-BPTR API; do not alias them to
     * the current process input.
     */
    if (watcher == NULL || watcher->fd != 0 || !(watcher->events & POLLIN))
        return 0;
    return WaitForChar(Input(), 0) != 0;
}

static void uv__aros_dispatch_ttys(uv_loop_t* loop,
                                   uv__io_t** watchers,
                                   int count)
{
    int i;

    for (i = 0; i < count; i++) {
        uv__io_t* watcher = watchers[i];

        if (uv__aros_tty_ready(watcher))
            watcher->cb(loop, watcher, POLLIN);
    }
}

static void uv__aros_dispatch_dos_pipes(uv_loop_t* loop,
                                        uv__io_t** watchers,
                                        int count)
{
    int i;

    for (i = 0; i < count; i++) {
        uv__io_t* watcher = watchers[i];

        if ((watcher->events & POLLIN) &&
            uv__aros_dos_pipe_ready(watcher->fd))
            watcher->cb(loop, watcher, POLLIN);
        else if (watcher->events & POLLOUT)
            watcher->cb(loop, watcher, POLLOUT);
    }
}

/*
 * Exec resources of a loop -- the timer.device open, its IORequest, the reply
 * MsgPort (one signal) and the async wake signal -- are per TASK: a signal
 * bit lives in the allocating task's tc_SigAlloc, a port wakes its
 * mp_SigTask, and FreeSignal()/DeleteMsgPort() operate on the CALLING task.
 * libuv lets a loop be initialised on one thread and run on another (node's
 * tracing Agent and Watchdog init in a constructor and uv_run on a worker
 * thread; the Agent's loop is also process-global and outlives the process
 * that constructed it on AROS), so binding them at uv_loop_init() time was
 * wrong three ways: the running task waited on bits it never owned while
 * uv_async_send() woke the constructing task, a loop closed from another
 * task freed bits out of the closer's own allocation, and a never-run loop
 * pinned two bits of a task until it exited (the Shell's "returned with
 * unfreed signal 0x20000/0x40000" after every C:Node).
 *
 * So the loop owns nothing until it is polled: uv__aros_loop_bind() acquires
 * the resources on the task that calls uv__io_poll(), re-homes them when a
 * different task runs the loop, and uv__aros_loop_unbind() frees them fully
 * only on the owning task -- from any other task (or after the owner died)
 * the memory and device open are released and the owner's bits are left to
 * die with it, never touched through the wrong task.  The owner is
 * identified by task pointer AND exec unique ID; task pointers are recycled,
 * and the kept tracing loop must not mistake a later process's main task at
 * the same address for its dead owner.  uv_async_send() from any thread
 * reads the (task, signal) pair with acquire semantics and merely leaves the
 * handle pending while the loop is unbound; a fresh binding raises its own
 * async bit so such pre-binding sends are drained on the first poll.
 */
int uv__aros_loop_owned_by_caller(uv_loop_t* loop) {
    struct Task* me = FindTask(NULL);

    return loop->aros_task == me && loop->aros_task_id == GetETaskID(me);
}

static void uv__aros_loop_unbind(uv_loop_t* loop) {
    struct timerequest* tr = (struct timerequest*)loop->aros_timerio;
    struct MsgPort* port = (struct MsgPort*)loop->aros_timerport;
    int owner = uv__aros_loop_owned_by_caller(loop);

    /* uv__io_poll always reaps its timer request before returning, so no
       request is ever in flight here -- just close and free. (Calling WaitIO
       on a request that was never sent removes a non-existent message and
       faults.) */
    if (tr != NULL) {
        CloseDevice((struct IORequest*)tr);
        DeleteIORequest((struct IORequest*)tr);
    }
    if (port != NULL) {
        if (owner)
            DeleteMsgPort(port);
        else
            FreeMem(port, sizeof(*port));   /* the owner's signal stays its own */
    }
    if (loop->aros_async_sig >= 0 && owner)
        FreeSignal(loop->aros_async_sig);

    loop->aros_timerio = NULL;
    loop->aros_timerport = NULL;
    loop->aros_async_sig = -1;
    __atomic_store_n(&loop->aros_task, NULL, __ATOMIC_RELEASE);
    loop->aros_task_id = 0;
}

/* Returns 0 with the loop's exec resources owned by the calling task, or a
   UV_E* code with none of them held (a later poll retries). */
static int uv__aros_loop_bind(uv_loop_t* loop) {
    struct Task* me = FindTask(NULL);
    struct MsgPort* port;
    struct timerequest* tr;
    BYTE sig;

    if (loop->aros_task != NULL) {
        if (uv__aros_loop_owned_by_caller(loop))
            return 0;
        uv__aros_loop_unbind(loop);
    }

    port = CreateMsgPort();
    if (port == NULL)
        return UV_ENOMEM;

    tr = (struct timerequest*)CreateIORequest(port, sizeof(*tr));
    if (tr == NULL) {
        DeleteMsgPort(port);
        return UV_ENOMEM;
    }

    if (OpenDevice((CONST_STRPTR)"timer.device", UNIT_MICROHZ,
                   (struct IORequest*)tr, 0) != 0) {
        DeleteIORequest((struct IORequest*)tr);
        DeleteMsgPort(port);
        return UV_ENOSYS;
    }

    sig = AllocSignal(-1);
    if (sig == -1) {
        CloseDevice((struct IORequest*)tr);
        DeleteIORequest((struct IORequest*)tr);
        DeleteMsgPort(port);
        return UV_EMFILE;
    }

    loop->aros_timerport = port;
    loop->aros_timerio = tr;
    loop->aros_async_sig = sig;
    loop->aros_task_id = GetETaskID(me);
    /* Publish the task last: a sender that sees it also sees the signal. */
    __atomic_store_n(&loop->aros_task, me, __ATOMIC_RELEASE);

    /* Anything uv_async_send()ed before this binding only set its handle's
       pending flag; make the first wait drain it. */
    SetSignal(1UL << sig, 1UL << sig);
    return 0;
}

int uv__platform_loop_init(uv_loop_t* loop) {
    loop->aros_timerport = NULL;
    loop->aros_timerio = NULL;
    loop->aros_task = NULL;
    loop->aros_async_sig = -1;
    loop->aros_task_id = 0;

    uv__aros_net_loop_init();
    return 0;
}

void uv__platform_loop_delete(uv_loop_t* loop) {
    uv__aros_loop_unbind(loop);
    uv__aros_net_loop_delete();
}

void uv__platform_invalidate_fd(uv_loop_t* loop, int fd) {
    (void)loop;
    (void)fd;
}

/* poll(2) is stubbed on AROS, so uv_poll_init's fd validation is permissive;
   only genuine bsdsocket fds are actually waited on (uv__io_poll below). */
int uv__io_check_fd(uv_loop_t* loop, int fd) {
    (void)loop;
    (void)fd;
    return 0;
}

void uv__io_poll(uv_loop_t* loop, int timeout) {
    struct timerequest* tr;
    struct MsgPort* port;
    struct uv__queue* q;
    uv__io_t* w;
    int fds[UV_AROS_POLL_MAX];
    int wants[UV_AROS_POLL_MAX];
    int revents[UV_AROS_POLL_MAX];
    uv__io_t* tty_watchers[UV_AROS_POLL_MAX];
    uv__io_t* pipe_watchers[UV_AROS_POLL_MAX];
    int nsock, ntty, npipe, i, fd, re;
    ULONG timermask;
    ULONG asyncmask = 0;
    ULONG got;

    /* Own (or take over) the loop's exec resources on this task.  On failure
       the loop runs unbound this turn: no timer, no async wake, and the
       branches below fall back to a non-waiting turn rather than sleeping
       on signals this task does not hold. */
    (void)uv__aros_loop_bind(loop);

    /* Fold each queued watcher's pending mask into its active mask. Do this
       first, unconditionally -- it is the contract of uv__io_start/stop. */
    while (!uv__queue_empty(&loop->watcher_queue)) {
        q = uv__queue_head(&loop->watcher_queue);
        uv__queue_remove(q);
        uv__queue_init(q);
        w = uv__queue_data(q, uv__io_t, watcher_queue);
        w->events = w->pevents;
    }

    /*
     * Collect socket watchers for WaitSelect and interactive stdin separately.
     * DOS descriptors and bsdsocket descriptors are different namespaces;
     * passing KCON fd 0 to WaitSelect cannot report console.device input.
     */
    uv__aros_process_reap(loop);

    nsock = 0;
    ntty = 0;
    npipe = 0;
    for (fd = 0; fd < (int)loop->nwatchers && nsock < UV_AROS_POLL_MAX; fd++) {
        w = loop->watchers[fd];
        if (w == NULL || w->events == 0)
            continue;
        if (!uv__aros_is_socket(fd)) {
            if (uv__aros_is_dos_pipe(fd) && npipe < UV_AROS_POLL_MAX)
                pipe_watchers[npipe++] = w;
            else if (isatty(fd) && ntty < UV_AROS_POLL_MAX)
                tty_watchers[ntty++] = w;
            continue;
        }
        fds[nsock]  = fd;
        wants[nsock] = ((w->events & POLLIN)  ? 1 : 0) |
                       ((w->events & POLLOUT) ? 2 : 0);
        nsock++;
    }

    if (ntty > 0) {
        uv__aros_dispatch_ttys(loop, tty_watchers, ntty);
        /*
         * Node keeps an idle/check watcher armed while its interactive UI is
         * active, which normally asks the backend for a non-blocking poll.
         * A DOS console has no fd signal to merge into WaitSelect yet, so a
         * literal timeout zero would spin an entire CPU. Bound the adapter to
         * terminal-scale latency until KCON exposes a waitable endpoint.
         */
        if (timeout < 0 || timeout == 0 || timeout > UV_AROS_TTY_POLL_MS)
            timeout = UV_AROS_TTY_POLL_MS;
    }

    if (npipe > 0) {
        uv__aros_dispatch_dos_pipes(loop, pipe_watchers, npipe);
        if (timeout < 0 || timeout == 0 || timeout > UV_AROS_TTY_POLL_MS)
            timeout = UV_AROS_TTY_POLL_MS;
    }

    /* AROS reports child death through Exec rather than a pollable SIGCHLD fd.
       Bound the wait while process handles are active and reap each turn. */
    if (!uv__queue_empty(&loop->process_handles) &&
        (timeout < 0 || timeout > UV_AROS_TTY_POLL_MS))
        timeout = UV_AROS_TTY_POLL_MS;

    /* Socket fds present: WaitSelect covers fd readiness + timeout + the loop's
       async signal in one wait (timeout == 0 => non-blocking reap). */
    if (nsock > 0) {
        int async_fired = 0;
        int r = uv__aros_waitselect(fds, wants, revents, nsock, timeout,
                                    loop->aros_async_sig, &async_fired);
        uv__update_time(loop);

        if (async_fired)
            uv__aros_async_drain(loop);

        if (r > 0) {
            for (i = 0; i < nsock; i++) {
                re = 0;
                if (revents[i] & 1) re |= POLLIN;
                if (revents[i] & 2) re |= POLLOUT;
                if (re == 0)
                    continue;
                w = loop->watchers[fds[i]];
                if (w != NULL)
                    w->cb(loop, w, re);
            }
        }
        uv__aros_dispatch_dos_pipes(loop, pipe_watchers, npipe);
        uv__aros_dispatch_ttys(loop, tty_watchers, ntty);
        uv__aros_process_reap(loop);
        return;
    }

    /* --- No socket fds: timer.device + async-signal wait (core-first path). --- */
    tr = (struct timerequest*)loop->aros_timerio;

    if (timeout == 0) {
        /*
         * Active idle/check handles make libuv poll with timeout zero.  A
         * threadpool worker may already have signalled wq_async; returning
         * without consuming that signal starves the completed request forever.
         */
        if (loop->aros_async_sig >= 0) {
            asyncmask = 1UL << loop->aros_async_sig;
            got = SetSignal(0, asyncmask);
            if (got & asyncmask)
                uv__aros_async_drain(loop);
        }
        /*
         * If a request remains active, make the zero-timeout poll an
         * interruptible one-millisecond wait below.  This closes the race
         * where a worker raises wq_async just after the nonblocking check,
         * without taxing idle/check-only turns with a full timer tick.
         */
        if (uv__aros_threadpool_pending(loop) != 0 && tr != NULL &&
            loop->aros_timerport != NULL) {
            timeout = 1;
        } else {
            uv__aros_dispatch_ttys(loop, tty_watchers, ntty);
            uv__aros_dispatch_dos_pipes(loop, pipe_watchers, npipe);
            uv__aros_process_reap(loop);
            Reschedule();
            uv__update_time(loop);
            return;
        }
    }

    if (loop->aros_async_sig >= 0)
        asyncmask = 1UL << loop->aros_async_sig;

    /* With no socket fds, an infinite wait can only be released by the async
       signal; if async isn't wired yet, don't hang the loop forever. */
    if (timeout < 0 && asyncmask == 0)
        return;

    if (tr == NULL) {
        if (asyncmask)
            Wait(asyncmask);
        else if (timeout != 0)
            Delay(1);   /* unbound loop (bind failed): pace, do not spin */
        uv__update_time(loop);
        uv__aros_dispatch_dos_pipes(loop, pipe_watchers, npipe);
        uv__aros_dispatch_ttys(loop, tty_watchers, ntty);
        uv__aros_process_reap(loop);
        return;
    }

    port = (struct MsgPort*)loop->aros_timerport;
    timermask = 1UL << port->mp_SigBit;

    /*
     * The reply port carries exactly one request and nothing is in flight
     * here, so any port signal still set is stale. WaitIO() below returns
     * without Wait()ing when the request has already been replied -- every
     * early wake that AbortIO()s the timer, and every expiry that lands
     * between Wait() and WaitIO() -- and that leaves the reply's signal bit
     * pending. Carried into the next turn it makes Wait() return at once
     * with `timermask` set for a timer that has NOT fired, and WaitIO() then
     * sleeps on the timer bit alone for the whole timeout while async
     * wakeups (threadpool completions, cross-thread uv_async_send) queue up
     * unseen: a Node Worker loading modules saw its fs completions only at
     * its 5 s interval ticks. Clear it before arming the timer.
     */
    SetSignal(0, timermask);

    if (timeout >= 0) {
        tr->tr_node.io_Command = TR_ADDREQUEST;
        tr->tr_time.tv_secs = (ULONG)(timeout / 1000);
        tr->tr_time.tv_micro = (ULONG)((timeout % 1000) * 1000);
        SendIO((struct IORequest*)tr);
    }

    got = Wait(timermask | asyncmask);

    if (timeout >= 0) {
        if (!(got & timermask)) {
            /* Woken early by async: cancel the still-pending timer. */
            if (!CheckIO((struct IORequest*)tr))
                AbortIO((struct IORequest*)tr);
        }
        WaitIO((struct IORequest*)tr);
    }

    /* Dispatch any pending uv_async handles woken by the async signal. */
    if (asyncmask && (got & asyncmask))
        uv__aros_async_drain(loop);

    uv__aros_dispatch_ttys(loop, tty_watchers, ntty);
    uv__aros_dispatch_dos_pipes(loop, pipe_watchers, npipe);
    uv__aros_process_reap(loop);
    uv__update_time(loop);
}
