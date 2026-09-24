/*
 * pthread-abi-aros.c - glibc-ABI pthread/sem interposers for v8.library
 *
 * WHY THIS FILE EXISTS:
 * Upstream V8 and its bundled libc++ are compiled with target_os=linux
 * against glibc headers, so every pthread_mutex_t (40 bytes),
 * pthread_cond_t (48), pthread_rwlock_t (56), pthread_once_t (4),
 * pthread_mutexattr_t (4) and sem_t (32) embedded in a V8 object is
 * glibc-sized.  The AROS pthread linklib's types are much larger
 * (pthread_mutex_t ~120 bytes: kind + inline struct SignalSemaphore +
 * incond; sem_t ~300 bytes).  Calling the AROS implementations on
 * glibc-sized storage overflows into the neighbouring members:
 * InitSemaphore()'s NewList() wrote self-referential pointers over
 * Isolate::code_pages_buffer1_ (crash in Isolate::InitializeCodeRanges,
 * CR2=0), and V8's own writes corrupted the semaphore innards
 * ("Semaphore in an illegal state" soft alerts at V8-INIT step 4).
 *
 * FIX: these __wrap_* implementations keep only a 16-byte header
 * {state, aux, real-pointer} inside the glibc-sized slot and allocate
 * the real Exec-primitive object out of line on first use.  Lazy init
 * is mandatory anyway because glibc callers zero-initialise statics
 * (PTHREAD_MUTEX_INITIALIZER) instead of calling *_init().  The
 * mmakefile adds -Wl,--wrap= for every function defined here; --wrap
 * rewrites references in all link inputs (including AROS pthread
 * archive members), so every caller sees one consistent representation.
 * Thread lifecycle (join/self/key_*) stays with the AROS linklib: those
 * take no caller-embedded storage. pthread_create and the pthread_attr_*
 * calls V8 makes are wrapped because pthread_attr_t IS caller storage
 * (see V8_ATTR_SIZE below).
 *
 * ABI NOTES:
 *  - pthread_* return glibc error VALUES (ETIMEDOUT=110 etc.), because
 *    V8 compares against glibc constants.  AROS errno values differ.
 *  - sem_* return -1 and set errno via __errno_location() (the
 *    glibc-stubs-aros.c static), again with glibc values.
 *  - Exec semaphores nest natively, so the shim explicitly rejects
 *    same-owner trylock for non-recursive mutexes and reports EDEADLK
 *    for ERRORCHECK lock, matching the glibc contract V8 expects.
 *  - clock ids are LINUX ids (REALTIME=0, MONOTONIC=1), matching what
 *    the glibc-compiled callers pass and what this library's
 *    clock_gettime shim accepts.
 */

#include <exec/types.h>
#include <exec/lists.h>
#include <exec/ports.h>
#include <exec/semaphores.h>
#include <exec/tasks.h>
#include <exec/memory.h>
#include <exec/io.h>
#include <devices/timer.h>
#include <proto/exec.h>

#include <pthread.h>
#include <stddef.h>
#include <string.h>
#include <time.h>

extern int *__errno_location(void);   /* glibc-stubs-aros.c */
extern int clock_gettime(int clk_id, struct timespec *tp);

/*
 * Time structs crossing this ABI boundary use the GLIBC layout
 * ({int64 sec; int64 nsec}, 16 bytes), both for abstimes passed in by
 * glibc-compiled V8/libc++ and for what this library's clock_gettime
 * writes back.  AROS timespec has 32-bit tv_sec + padding, which only
 * coincidentally aligns; be explicit instead.
 */
typedef struct { long long tv_sec; long long tv_nsec; } glibc_ts;

/* glibc (Linux x86_64) error values - NOT the AROS ones */
#define GERR_EPERM       1
#define GERR_EAGAIN     11
#define GERR_ENOMEM     12
#define GERR_EBUSY      16
#define GERR_EINVAL     22
#define GERR_EDEADLK    35
#define GERR_ETIMEDOUT 110

/* glibc mutex kinds crossing this ABI boundary, not AROS pthread values. */
#define GLIBC_PTHREAD_MUTEX_NORMAL      0
#define GLIBC_PTHREAD_MUTEX_RECURSIVE   1
#define GLIBC_PTHREAD_MUTEX_ERRORCHECK  2

/* Linux clock ids as passed by glibc-compiled callers */
#define LINUX_CLOCK_REALTIME  0
#define LINUX_CLOCK_MONOTONIC 1

/*
 * 16-byte header living at the start of every glibc-sized slot.
 * state transitions 0 -> BUSY -> READY via CAS; concurrent first-use
 * from several tasks is resolved without Forbid() (which is CPU-local
 * on EXECSMP and therefore not a lock).
 */
#define SLOT_FREE  0UL
#define SLOT_BUSY  0x50544842UL   /* 'PTHB' */
#define SLOT_READY 0x50544852UL   /* 'PTHR' */

typedef struct ShimSlot
{
    volatile ULONG state;
    ULONG          aux;      /* mutex kind / cond clockid, from *_init */
    APTR           real;
} ShimSlot;

typedef struct ShimWaiter
{
    struct MinNode node;
    struct Task   *task;
    BYTE           sigbit;
    volatile BYTE  queued;
} ShimWaiter;

/* minimal MinList helpers (no alib dependency) */
static void shim_newlist(struct MinList *ml)
{
    ml->mlh_Head = (struct MinNode *)&ml->mlh_Tail;
    ml->mlh_Tail = NULL;
    ml->mlh_TailPred = (struct MinNode *)&ml->mlh_Head;
}

static ShimWaiter *shim_first_waiter(struct MinList *ml)
{
    struct MinNode *h = ml->mlh_Head;

    return h->mln_Succ ? (ShimWaiter *)h : NULL;
}

struct shim_mutex
{
    struct SignalSemaphore sem;
};

struct shim_rwlock
{
    struct SignalSemaphore sem;
};

struct shim_cond
{
    struct SignalSemaphore lock;
    struct MinList         waiters;
    LONG                   clockid;
};

struct shim_sem
{
    struct SignalSemaphore lock;
    struct MinList         waiters;
    LONG                   value;
};

static int shim_sem_is_mine(struct SignalSemaphore *sem)
{
    return sem && sem->ss_NestCount > 0 && sem->ss_Owner == FindTask(NULL);
}

/*
 * Allocate-and-publish for a slot.  ctor_tag selects the object type.
 * Returns the real object, or NULL on allocation failure.
 */
enum { CTOR_MUTEX, CTOR_RWLOCK, CTOR_COND, CTOR_SEM };

static APTR slot_get(ShimSlot *s, int ctor_tag)
{
    for (;;)
    {
        ULONG st = s->state;

        if (st == SLOT_READY)
            return s->real;

        /*
         * Anything that is not our BUSY/READY magic is a
         * statically-initialised, unclaimed slot.  glibc initialisers
         * zero the storage, but AROS-header-compiled C++ (libstdc++,
         * module glue) statically initialises with AROS pthread.h
         * initialisers, which put the mutex KIND at byte 0
         * (PTHREAD_RECURSIVE_MUTEX_INITIALIZER = {1, ...}); locking
         * such a mutex spun forever here when only state==0 was
         * accepted (boot 163125 wedge at slot_get's pause loop).
         * CAS from the observed value so any convention is claimed
         * exactly once.
         */
        if (st != SLOT_BUSY &&
            __sync_bool_compare_and_swap(&s->state, st, SLOT_BUSY))
        {
            ULONG size;
            APTR r;

            switch (ctor_tag)
            {
            case CTOR_MUTEX:  size = sizeof(struct shim_mutex);  break;
            case CTOR_RWLOCK: size = sizeof(struct shim_rwlock); break;
            case CTOR_COND:   size = sizeof(struct shim_cond);   break;
            default:          size = sizeof(struct shim_sem);    break;
            }

            r = AllocMem(size, MEMF_PUBLIC | MEMF_CLEAR);
            if (!r)
            {
                s->state = SLOT_FREE;
                return NULL;
            }

            switch (ctor_tag)
            {
            case CTOR_MUTEX:
                InitSemaphore(&((struct shim_mutex *)r)->sem);
                break;
            case CTOR_RWLOCK:
                InitSemaphore(&((struct shim_rwlock *)r)->sem);
                break;
            case CTOR_COND:
            {
                struct shim_cond *c = (struct shim_cond *)r;
                InitSemaphore(&c->lock);
                shim_newlist(&c->waiters);
                c->clockid = (LONG)s->aux;
                break;
            }
            default:
            {
                struct shim_sem *m = (struct shim_sem *)r;
                InitSemaphore(&m->lock);
                shim_newlist(&m->waiters);
                m->value = (LONG)s->aux;
                break;
            }
            }

            s->real = r;
            __sync_synchronize();
            s->state = SLOT_READY;
            return r;
        }

        /* another task is constructing: brief spin */
        __asm__ __volatile__("pause");
    }
}

static void slot_destroy(ShimSlot *s, ULONG size)
{
    if (s->state == SLOT_READY && s->real)
        FreeMem(s->real, size);
    s->real = NULL;
    s->state = SLOT_FREE;
}

/*
 * On-stack one-shot timer for timed waits (UNIT_MICROHZ).
 * Returns the timer signal mask, or 0 on failure.
 */
static ULONG shim_timer_start(struct MsgPort *mp, struct timerequest *tr,
                              ULONG secs, ULONG micro, BYTE *sigbit)
{
    BYTE sb = AllocSignal(-1);

    if (sb == -1)
        return 0;

    memset(mp, 0, sizeof(*mp));
    mp->mp_Node.ln_Type = NT_MSGPORT;
    mp->mp_Flags = PA_SIGNAL;
    mp->mp_SigBit = sb;
    mp->mp_SigTask = FindTask(NULL);
    shim_newlist((struct MinList *)&mp->mp_MsgList);

    memset(tr, 0, sizeof(*tr));
    tr->tr_node.io_Message.mn_Node.ln_Type = NT_MESSAGE;
    tr->tr_node.io_Message.mn_ReplyPort = mp;
    tr->tr_node.io_Message.mn_Length = sizeof(*tr);

    if (OpenDevice("timer.device", UNIT_MICROHZ,
                   (struct IORequest *)tr, 0) != 0)
    {
        FreeSignal(sb);
        return 0;
    }

    tr->tr_node.io_Command = TR_ADDREQUEST;
    tr->tr_node.io_Flags = 0;
    tr->tr_time.tv_secs = secs;
    tr->tr_time.tv_micro = micro;
    SendIO((struct IORequest *)tr);

    *sigbit = sb;
    return 1UL << sb;
}

static void shim_timer_stop(struct timerequest *tr, BYTE sigbit)
{
    if (!CheckIO((struct IORequest *)tr))
        AbortIO((struct IORequest *)tr);
    WaitIO((struct IORequest *)tr);
    CloseDevice((struct IORequest *)tr);
    SetSignal(0, 1UL << sigbit);
    FreeSignal(sigbit);
}

/*
 * abstime (on 'clockid') -> relative delay.  Returns 0 on success,
 * GERR_ETIMEDOUT if the deadline already passed, GERR_EINVAL on a
 * clock failure.
 */
static int shim_abs_to_delta(int clockid, const struct timespec *abst,
                             ULONG *secs, ULONG *micro)
{
    const glibc_ts *gabst = (const glibc_ts *)abst;
    glibc_ts now;
    long sec, nsec;

    if (clock_gettime(clockid, (struct timespec *)&now) != 0)
        return GERR_EINVAL;

    sec = (long)gabst->tv_sec - (long)now.tv_sec;
    nsec = (long)gabst->tv_nsec - (long)now.tv_nsec;
    if (nsec < 0)
    {
        nsec += 1000000000L;
        sec--;
    }
    if (sec < 0 || (sec == 0 && nsec == 0))
        return GERR_ETIMEDOUT;

    *secs = (ULONG)sec;
    *micro = (ULONG)(nsec / 1000);
    return 0;
}

/* ---------------------------------------------------------------- */
/* mutex                                                             */
/* ---------------------------------------------------------------- */

int __wrap_pthread_mutex_init(void *mutex, const void *attr)
{
    ShimSlot *s = (ShimSlot *)mutex;

    memset(mutex, 0, 40);   /* full glibc slot, incl. __kind at byte 16 */
    s->state = SLOT_FREE;
    s->aux = attr ? (ULONG)*(const int *)attr
                  : GLIBC_PTHREAD_MUTEX_NORMAL;
    return 0;
}

int __wrap_pthread_mutex_destroy(void *mutex)
{
    slot_destroy((ShimSlot *)mutex, sizeof(struct shim_mutex));
    return 0;
}

int __wrap_pthread_mutex_lock(void *mutex)
{
    struct shim_mutex *m = slot_get((ShimSlot *)mutex, CTOR_MUTEX);

    if (!m)
        return GERR_ENOMEM;
    if (((ShimSlot *)mutex)->aux == GLIBC_PTHREAD_MUTEX_ERRORCHECK &&
        shim_sem_is_mine(&m->sem))
        return GERR_EDEADLK;
    ObtainSemaphore(&m->sem);
    return 0;
}

int __wrap_pthread_mutex_trylock(void *mutex)
{
    struct shim_mutex *m = slot_get((ShimSlot *)mutex, CTOR_MUTEX);

    if (!m)
        return GERR_ENOMEM;
    if (((ShimSlot *)mutex)->aux != GLIBC_PTHREAD_MUTEX_RECURSIVE &&
        shim_sem_is_mine(&m->sem))
        return GERR_EBUSY;
    return AttemptSemaphore(&m->sem) ? 0 : GERR_EBUSY;
}

int __wrap_pthread_mutex_unlock(void *mutex)
{
    ShimSlot *s = (ShimSlot *)mutex;

    if (s->state != SLOT_READY)
        return GERR_EPERM;   /* unlock of a never-locked mutex */
    ReleaseSemaphore(&((struct shim_mutex *)s->real)->sem);
    return 0;
}

/* glibc pthread_mutexattr_t is 4 bytes; the AROS linklib's is 8 and
 * its attr_init overflows the caller's slot, so these are wrapped. */
int __wrap_pthread_mutexattr_init(void *attr)
{
    *(int *)attr = 0;
    return 0;
}

int __wrap_pthread_mutexattr_destroy(void *attr)
{
    (void)attr;
    return 0;
}

int __wrap_pthread_mutexattr_settype(void *attr, int kind)
{
    *(int *)attr = kind;
    return 0;
}

/* ---------------------------------------------------------------- */
/* condition variables                                               */
/* ---------------------------------------------------------------- */

int __wrap_pthread_condattr_init(void *attr)
{
    *(int *)attr = LINUX_CLOCK_REALTIME;
    return 0;
}

int __wrap_pthread_condattr_destroy(void *attr)
{
    (void)attr;
    return 0;
}

int __wrap_pthread_condattr_setclock(void *attr, int clockid)
{
    *(int *)attr = clockid;
    return 0;
}

int __wrap_pthread_cond_init(void *cond, const void *attr)
{
    ShimSlot *s = (ShimSlot *)cond;

    memset(cond, 0, 48);   /* full glibc slot */
    s->aux = attr ? (ULONG)*(const int *)attr : LINUX_CLOCK_REALTIME;
    s->state = SLOT_FREE;
    return 0;
}

int __wrap_pthread_cond_destroy(void *cond)
{
    slot_destroy((ShimSlot *)cond, sizeof(struct shim_cond));
    return 0;
}

static int shim_cond_wait_common(void *cond, void *mutex,
                                 int clockid, const struct timespec *abst)
{
    struct shim_cond *c = slot_get((ShimSlot *)cond, CTOR_COND);
    ShimWaiter waiter;
    struct MsgPort tmp_port;
    struct timerequest tmp_req;
    ULONG timermask = 0;
    BYTE timersig = -1;
    ULONG waitmask, sigs;
    int timedout;

    if (!c)
        return GERR_ENOMEM;

    if (abst)
    {
        ULONG dsecs, dmicro;
        int rc = shim_abs_to_delta(clockid, abst, &dsecs, &dmicro);

        if (rc != 0)
            return rc;
        timermask = shim_timer_start(&tmp_port, &tmp_req,
                                     dsecs, dmicro, &timersig);
        if (!timermask)
            return GERR_EINVAL;
    }

    waiter.task = FindTask(NULL);
    waiter.sigbit = AllocSignal(-1);
    if (waiter.sigbit == -1)
    {
        if (timermask)
            shim_timer_stop(&tmp_req, timersig);
        return GERR_EAGAIN;   /* out of signal bits */
    }
    waiter.queued = 1;
    waitmask = 1UL << waiter.sigbit;

    ObtainSemaphore(&c->lock);
    AddTail((struct List *)&c->waiters, (struct Node *)&waiter);
    ReleaseSemaphore(&c->lock);

    __wrap_pthread_mutex_unlock(mutex);
    sigs = Wait(waitmask | timermask);
    __wrap_pthread_mutex_lock(mutex);

    ObtainSemaphore(&c->lock);
    if (waiter.queued)
    {
        Remove((struct Node *)&waiter);
        waiter.queued = 0;
    }
    ReleaseSemaphore(&c->lock);

    /* prefer "signalled" if both arrived */
    timedout = (abst && (sigs & timermask) && !(sigs & waitmask));

    SetSignal(0, waitmask);
    FreeSignal(waiter.sigbit);
    if (timermask)
        shim_timer_stop(&tmp_req, timersig);

    return timedout ? GERR_ETIMEDOUT : 0;
}

int __wrap_pthread_cond_wait(void *cond, void *mutex)
{
    return shim_cond_wait_common(cond, mutex, LINUX_CLOCK_REALTIME, NULL);
}

int __wrap_pthread_cond_timedwait(void *cond, void *mutex,
                                  const struct timespec *abst)
{
    struct shim_cond *c = slot_get((ShimSlot *)cond, CTOR_COND);

    if (!c)
        return GERR_ENOMEM;
    return shim_cond_wait_common(cond, mutex, (int)c->clockid, abst);
}

int __wrap_pthread_cond_clockwait(void *cond, void *mutex, int clockid,
                                  const struct timespec *abst)
{
    return shim_cond_wait_common(cond, mutex, clockid, abst);
}

static int shim_cond_wake(void *cond, int onlyfirst)
{
    struct shim_cond *c = slot_get((ShimSlot *)cond, CTOR_COND);
    ShimWaiter *w;

    if (!c)
        return GERR_ENOMEM;

    ObtainSemaphore(&c->lock);
    while ((w = shim_first_waiter(&c->waiters)) != NULL)
    {
        /* dequeue before Signal so a second signal picks the NEXT
         * waiter instead of re-targeting this one (lost wakeup in the
         * AROS linklib's version) */
        Remove((struct Node *)w);
        w->queued = 0;
        Signal(w->task, 1UL << w->sigbit);
        if (onlyfirst)
            break;
    }
    ReleaseSemaphore(&c->lock);
    return 0;
}

int __wrap_pthread_cond_signal(void *cond)
{
    return shim_cond_wake(cond, 1);
}

int __wrap_pthread_cond_broadcast(void *cond)
{
    return shim_cond_wake(cond, 0);
}

/* ---------------------------------------------------------------- */
/* rwlock - Exec shared semaphores map 1:1                           */
/* ---------------------------------------------------------------- */

int __wrap_pthread_rwlock_init(void *lock, const void *attr)
{
    (void)attr;
    memset(lock, 0, 56);   /* full glibc slot */
    return 0;
}

int __wrap_pthread_rwlock_destroy(void *lock)
{
    slot_destroy((ShimSlot *)lock, sizeof(struct shim_rwlock));
    return 0;
}

int __wrap_pthread_rwlock_rdlock(void *lock)
{
    struct shim_rwlock *l = slot_get((ShimSlot *)lock, CTOR_RWLOCK);

    if (!l)
        return GERR_ENOMEM;
    ObtainSemaphoreShared(&l->sem);
    return 0;
}

int __wrap_pthread_rwlock_tryrdlock(void *lock)
{
    struct shim_rwlock *l = slot_get((ShimSlot *)lock, CTOR_RWLOCK);

    if (!l)
        return GERR_ENOMEM;
    return AttemptSemaphoreShared(&l->sem) ? 0 : GERR_EBUSY;
}

int __wrap_pthread_rwlock_wrlock(void *lock)
{
    struct shim_rwlock *l = slot_get((ShimSlot *)lock, CTOR_RWLOCK);

    if (!l)
        return GERR_ENOMEM;
    ObtainSemaphore(&l->sem);
    return 0;
}

int __wrap_pthread_rwlock_trywrlock(void *lock)
{
    struct shim_rwlock *l = slot_get((ShimSlot *)lock, CTOR_RWLOCK);

    if (!l)
        return GERR_ENOMEM;
    return AttemptSemaphore(&l->sem) ? 0 : GERR_EBUSY;
}

int __wrap_pthread_rwlock_unlock(void *lock)
{
    ShimSlot *s = (ShimSlot *)lock;

    if (s->state != SLOT_READY)
        return GERR_EPERM;
    ReleaseSemaphore(&((struct shim_rwlock *)s->real)->sem);
    return 0;
}

/* ---------------------------------------------------------------- */
/* once - operates directly on the glibc 4-byte int                  */
/* ---------------------------------------------------------------- */

int __wrap_pthread_once(int *once_control, void (*init_routine)(void))
{
    for (;;)
    {
        int st = *once_control;

        if (st == 2)
            return 0;
        if (st == 0 &&
            __sync_bool_compare_and_swap(once_control, 0, 1))
        {
            init_routine();
            __sync_synchronize();
            *once_control = 2;
            return 0;
        }
        __asm__ __volatile__("pause");
    }
}

/* ---------------------------------------------------------------- */
/* stack bounds - V8 base::Stack calls getattr_np + attr_getstack    */
/* on the CURRENT thread only.  Chromium's AROS V8 is compiled with  */
/* the AROS pthread_attr_t layout of its SDK, so clear V8's slot     */
/* (V8_ATTR_SIZE) rather than the current type or the 56-byte glibc  */
/* one.  The field offsets remain stackaddr @0, stacksize @8.        */
/* ---------------------------------------------------------------- */

/*
 * pthread_attr_t as the V8 monolith sees it. V8 is compiled once against
 * the SDK of its build and reserves exactly that many bytes on its stack;
 * pthread_attr_t has since grown (cpuaffinity), and the AROS functions
 * clear and read the whole current type. Clearing 40 bytes of V8's 32-byte
 * slot zeroed the caller's saved frame pointer, and pthread_create read an
 * affinity mask out of whatever followed the slot. V8 only ever sees the
 * fields in front of cpuaffinity; the rest is supplied here, with
 * cpuaffinity NULL (inherit, the behaviour before the field existed).
 */
#define V8_ATTR_SIZE offsetof(pthread_attr_t, cpuaffinity)
_Static_assert(offsetof(pthread_attr_t, cpuaffinity) == 32,
               "V8's pthread_attr_t slot is 32 bytes; a field added in front "
               "of cpuaffinity changes it - rebuild the V8 monolith");

extern int __real_pthread_attr_init(pthread_attr_t *attr);
extern int __real_pthread_attr_setstacksize(pthread_attr_t *attr,
                                            size_t stacksize);
extern int __real_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                                 void *(*start)(void *), void *arg);

static void v8_attr_expand(pthread_attr_t *full, const void *attr)
{
    memset(full, 0, sizeof(*full));
    memcpy(full, attr, V8_ATTR_SIZE);
}

int __wrap_pthread_attr_init(void *attr)
{
    pthread_attr_t full;
    int rc;

    if (attr == NULL)
        return GERR_EINVAL;
    rc = __real_pthread_attr_init(&full);
    if (rc == 0)
        memcpy(attr, &full, V8_ATTR_SIZE);
    return rc ? GERR_EINVAL : 0;
}

int __wrap_pthread_attr_destroy(void *attr)
{
    if (attr == NULL)
        return GERR_EINVAL;
    memset(attr, 0, V8_ATTR_SIZE);
    return 0;
}

int __wrap_pthread_attr_setstacksize(void *attr, size_t stacksize)
{
    pthread_attr_t full;
    int rc;

    if (attr == NULL)
        return GERR_EINVAL;
    v8_attr_expand(&full, attr);
    rc = __real_pthread_attr_setstacksize(&full, stacksize);
    if (rc == 0)
        memcpy(attr, &full, V8_ATTR_SIZE);
    return rc ? GERR_EINVAL : 0;
}

int __wrap_pthread_create(pthread_t *thread, const void *attr,
                          void *(*start)(void *), void *arg)
{
    pthread_attr_t full;

    if (attr == NULL)
        return __real_pthread_create(thread, NULL, start, arg);
    v8_attr_expand(&full, attr);
    return __real_pthread_create(thread, &full, start, arg);
}

int __wrap_pthread_getattr_np(unsigned long thread, void *attr)
{
    struct Task *t = FindTask(NULL);
    char **fields = (char **)attr;

    (void)thread;
    memset(attr, 0, V8_ATTR_SIZE);
    fields[0] = (char *)t->tc_SPLower;
    ((size_t *)attr)[1] = (size_t)((char *)t->tc_SPUpper -
                                   (char *)t->tc_SPLower);
    return 0;
}

int __wrap_pthread_attr_getstack(const void *attr, void **stackaddr,
                                 size_t *stacksize)
{
    *stackaddr = ((void **)attr)[0];
    *stacksize = ((const size_t *)attr)[1];
    return 0;
}

/* ---------------------------------------------------------------- */
/* POSIX unnamed semaphores (glibc sem_t = 32 bytes; AROS ~300)      */
/* Return -1 + errno (glibc values), unlike pthread_* above.         */
/* ---------------------------------------------------------------- */

int __wrap_sem_init(void *sem, int pshared, unsigned int value)
{
    ShimSlot *s = (ShimSlot *)sem;

    (void)pshared;
    memset(sem, 0, 32);   /* full glibc slot */
    s->aux = value;
    s->state = SLOT_FREE;
    return 0;
}

int __wrap_sem_destroy(void *sem)
{
    slot_destroy((ShimSlot *)sem, sizeof(struct shim_sem));
    return 0;
}

int __wrap_sem_post(void *sem)
{
    struct shim_sem *m = slot_get((ShimSlot *)sem, CTOR_SEM);
    ShimWaiter *w;

    if (!m)
    {
        *__errno_location() = GERR_EINVAL;
        return -1;
    }

    ObtainSemaphore(&m->lock);
    w = shim_first_waiter(&m->waiters);
    if (w)
    {
        /* hand the token directly to the first waiter */
        Remove((struct Node *)w);
        w->queued = 0;
        Signal(w->task, 1UL << w->sigbit);
    }
    else
    {
        m->value++;
    }
    ReleaseSemaphore(&m->lock);
    return 0;
}

int __wrap_sem_trywait(void *sem)
{
    struct shim_sem *m = slot_get((ShimSlot *)sem, CTOR_SEM);
    int got = 0;

    if (!m)
    {
        *__errno_location() = GERR_EINVAL;
        return -1;
    }

    ObtainSemaphore(&m->lock);
    if (m->value > 0)
    {
        m->value--;
        got = 1;
    }
    ReleaseSemaphore(&m->lock);

    if (!got)
    {
        *__errno_location() = GERR_EAGAIN;
        return -1;
    }
    return 0;
}

static int shim_sem_wait_common(void *sem, const struct timespec *abst)
{
    struct shim_sem *m = slot_get((ShimSlot *)sem, CTOR_SEM);
    ShimWaiter waiter;
    struct MsgPort tmp_port;
    struct timerequest tmp_req;
    ULONG timermask = 0;
    BYTE timersig = -1;
    ULONG waitmask, sigs;
    int have_token;

    if (!m)
    {
        *__errno_location() = GERR_EINVAL;
        return -1;
    }

    ObtainSemaphore(&m->lock);
    if (m->value > 0)
    {
        m->value--;
        ReleaseSemaphore(&m->lock);
        return 0;
    }
    ReleaseSemaphore(&m->lock);

    if (abst)
    {
        ULONG dsecs, dmicro;
        /* sem_timedwait abstime is CLOCK_REALTIME per POSIX; within
         * this library's clock shim Linux id 0 is the consistent
         * counterpart of what glibc-compiled callers computed */
        int rc = shim_abs_to_delta(LINUX_CLOCK_REALTIME, abst,
                                   &dsecs, &dmicro);

        if (rc != 0)
        {
            *__errno_location() = rc;
            return -1;
        }
        timermask = shim_timer_start(&tmp_port, &tmp_req,
                                     dsecs, dmicro, &timersig);
        if (!timermask)
        {
            *__errno_location() = GERR_EINVAL;
            return -1;
        }
    }

    waiter.task = FindTask(NULL);
    waiter.sigbit = AllocSignal(-1);
    if (waiter.sigbit == -1)
    {
        if (timermask)
            shim_timer_stop(&tmp_req, timersig);
        *__errno_location() = GERR_EAGAIN;
        return -1;
    }
    waitmask = 1UL << waiter.sigbit;

    /* re-check under the lock; enqueue if still no token */
    ObtainSemaphore(&m->lock);
    if (m->value > 0)
    {
        m->value--;
        ReleaseSemaphore(&m->lock);
        SetSignal(0, waitmask);
        FreeSignal(waiter.sigbit);
        if (timermask)
            shim_timer_stop(&tmp_req, timersig);
        return 0;
    }
    waiter.queued = 1;
    AddTail((struct List *)&m->waiters, (struct Node *)&waiter);
    ReleaseSemaphore(&m->lock);

    sigs = Wait(waitmask | timermask);
    (void)sigs;

    /* a post that dequeued us handed us the token, even if the timer
     * also fired */
    ObtainSemaphore(&m->lock);
    if (waiter.queued)
    {
        Remove((struct Node *)&waiter);
        waiter.queued = 0;
        have_token = 0;
    }
    else
    {
        have_token = 1;
    }
    ReleaseSemaphore(&m->lock);

    SetSignal(0, waitmask);
    FreeSignal(waiter.sigbit);
    if (timermask)
        shim_timer_stop(&tmp_req, timersig);

    if (!have_token)
    {
        *__errno_location() = GERR_ETIMEDOUT;
        return -1;
    }
    return 0;
}

int __wrap_sem_wait(void *sem)
{
    return shim_sem_wait_common(sem, NULL);
}

int __wrap_sem_timedwait(void *sem, const struct timespec *abst)
{
    return shim_sem_wait_common(sem, abst);
}
