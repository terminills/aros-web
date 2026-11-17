/* aros_thread_glue.c -- flat-C boundary between Rust `std::thread` and AROS
 * pthread.library (the -lpthread linklib). std's `sys/thread/aros.rs` calls these
 * aros_thr_* wrappers; the TLS-key side (pthread_key_*) is called from Rust
 * directly (sys/thread_local/key/aros.rs) because those take no opaque structs.
 *
 * Uses the current native AROS SDK for opaque pthread types.
 */
#include <proto/dos.h>   /* Delay() for aros_thr_sleep -- no <time.h> */
#include <proto/exec.h>  /* FindTask/AllocVec for the spawn trampoline */
#include <dos/dosextens.h>
#include <exec/nodes.h>

#include <pthread.h>
#include <sched.h>
#include <errno.h>

_Static_assert(sizeof(pthread_t) == sizeof(unsigned int), "Rust pthread id ABI");

/* Trampoline: pthread threads are their own DOS Processes, and
 * pr_WindowPtr is per-Process — the main()'s "-1, never pop insert-volume
 * requesters" setting does NOT inherit. Without this, any worker-thread
 * path probe of an unknown volume (e.g. sqlite's VFS walking "/System:")
 * blocks that worker on a DOS requester. Set -1 on every spawned thread
 * before its Rust entry runs. */
struct aros_thr_boot { void *(*start)(void *); void *arg; };

static void *aros_thr_entry(void *raw)
{
    struct aros_thr_boot boot = *(struct aros_thr_boot *)raw;
    struct Task *me = FindTask(0);
    FreeVec(raw);
    if (me->tc_Node.ln_Type == NT_PROCESS)
        ((struct Process *)me)->pr_WindowPtr = (APTR)-1;
    return boot.start(boot.arg);
}

/* Spawn a joinable thread with the requested stack size (0 = library default).
 * Writes the thread id to *out_tid and returns 0, or a pthread errno on failure. */
int aros_thr_spawn(unsigned long stacksize, void *(*start)(void *), void *arg,
                   unsigned int *out_tid)
{
    pthread_attr_t attr;
    pthread_t tid = 0;
    int rc;
    struct aros_thr_boot *boot;

    if (!start || !out_tid)
        return 22 /* EINVAL */;

    boot = AllocVec(sizeof(*boot), 0 /* MEMF_ANY */);
    if (!boot)
        return 12 /* ENOMEM */;
    boot->start = start;
    boot->arg = arg;

    if (pthread_attr_init(&attr) != 0) {
        FreeVec(boot);
        return 12 /* ENOMEM */;
    }
    if (stacksize > 0) {
        rc = pthread_attr_setstacksize(&attr, stacksize);
        if (rc != 0) {
            pthread_attr_destroy(&attr);
            FreeVec(boot);
            return rc;
        }
    }

    rc = pthread_create(&tid, &attr, aros_thr_entry, boot);
    pthread_attr_destroy(&attr);

    if (rc == 0)
        *out_tid = tid;
    else
        FreeVec(boot);
    return rc;
}

int aros_thr_join(unsigned int tid)
{
    return pthread_join((pthread_t)tid, (void **)0);
}

int aros_thr_detach(unsigned int tid)
{
    return pthread_detach((pthread_t)tid);
}

void aros_thr_yield(void)
{
    sched_yield();
}

/* thread::sleep, via dos Delay (50 ticks/sec = 20ms granularity). Coarse but
 * robust; blocks only the calling task, so other threads keep running. */
int aros_thr_sleep(unsigned int secs, unsigned int nsecs)
{
    unsigned long ticks = (unsigned long)secs * 50UL + ((unsigned long)nsecs + 19999999UL) / 20000000UL;
    if (ticks == 0 && (secs || nsecs))
        ticks = 1;              /* round any nonzero sub-20ms request up to one tick */
    /* Delay() takes a 32-bit ULONG; secs=u32::MAX (Rust's "sleep forever" chunk) is
     * ~214e9 ticks, which would truncate mod 2^32 and wake years early. Chunk it. */
    while (ticks > 0xFFFFFFFFUL) {
        Delay(0xFFFFFFFFUL);
        ticks -= 0xFFFFFFFFUL;
    }
    if (ticks > 0)
        Delay(ticks);
    return 0;
}
