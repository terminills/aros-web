/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    uv_aros_task_has_base -- can the CALLING task use uv1.library at all?

    uv1.library is a pertaskbase module: every internal call it makes into
    its rellibs (stdc, posixc) resolves the base through the current task's
    storage slot, not through the base the caller jumped in with.  A task
    that never opened uv1 (and whose creator did not either) therefore
    faults inside the first relwrapper it reaches, with CR2 = the rellib
    offset inside a NULL base.

    A caller that may be running on such a task - a library expunge, which
    lddemon runs on whichever task's AllocMem failed - needs to ask BEFORE
    calling anything else, and it cannot simply OpenLibrary("uv1.library")
    to give itself a base: LDFlush holds exec's LibList write lock across
    RemLibrary()/Expunge, and OpenLibrary takes that same lock in read mode
    (self-deadlock, spinning in KrnSpinLock on SMP; a pc-x86_64 guest
    freeze).

    This entry point is safe to call from any task: it only reads the slot
    through exec (an absolute SysBase), never a rellib.  Nothing in this
    file may call into stdc/posixc.

    The same LDFlush path is also why uv1.library vetoes its own expunge
    from any task but its last closer (hooks below).  uv1 links the static
    pthread emulation, whose EXIT-set function __pthread_Exit_Func reaps
    leftover threads with Delay(1)/pthread_join - a Wait() under exec's
    LibList WRITE lock when the expunge comes from LDFlush.  Every later
    OpenLibrary then spins under Forbid in LDRequestObject, and if the
    sleeper is pinned to the spinner's CPU it never wakes: the whole guest
    stops taking input (node.library did exactly this).  The CLOSELIB set runs on the last
    per-task close, just before genmodule drops the root open count and,
    when LIBF_DELEXP is set, expunges from that same task with lddemon's
    Forbid only (no list lock).  That task is remembered by its exec unique
    ID (task pointers are recycled), and only it may expunge.  A stale
    record can only ever match the same still-living task, which is the
    same safe path.
*/
#include <aros/symbolsets.h>
#include <exec/types.h>
#include <exec/libraries.h>
#include <proto/exec.h>

#include "aros-intbase.h"
#include "internal.h"   /* uv__platform_loop_delete */

/* aros.c: is the calling task the one holding this loop's exec resources? */
int uv__aros_loop_owned_by_caller(uv_loop_t* loop);
/* aros-net.c: drop the calling task's socket state. */
void uv__aros_net_task_release(void);

/* genmodule's per-task base lookup for this module (own slot, then the
   creator's slot).  Emitted hidden into uv1_start.c; the same module may
   use it. */
extern char *__aros_getoffsettable(void);

int uv_aros_task_has_base(void)
{
    return __aros_getoffsettable() != NULL;
}

/*
 * uv_default_loop() storage for the calling task's process: the per-task
 * base (see aros-intbase.h).  src/uv-common.c reaches its two upstream
 * statics through these, so a Node process and the C:Node it spawned no
 * longer share one loop.  A task without any base (only an expunge sweep
 * running on a foreign task ever gets here without one) falls back to a
 * guest-wide pair rather than dereferencing NULL; such a task cannot run a
 * loop anyway, every relwrapper it reached would fault first.
 */
static uv_loop_t  uv_aros_orphan_default_loop_struct;
static uv_loop_t *uv_aros_orphan_default_loop_ptr;

uv_loop_t **uv_aros_default_loop_ptr(void)
{
    struct UV1IntBase *base = (struct UV1IntBase *)__aros_getoffsettable();

    if (base == NULL)
        return &uv_aros_orphan_default_loop_ptr;
    return &base->default_loop_ptr;
}

uv_loop_t *uv_aros_default_loop_struct(void)
{
    struct UV1IntBase *base = (struct UV1IntBase *)__aros_getoffsettable();

    if (base == NULL)
        return &uv_aros_orphan_default_loop_struct;
    return &base->default_loop_struct;
}

static ULONG uv_aros_closer_id;

/*
 * The exec-side half of "process death frees everything".  An embedder that
 * never uv_loop_close()s the default loop relies on process exit to reclaim
 * it (on Linux the kernel does).  On AROS a loop that has been polled holds
 * exec resources of the polling task (aros.c uv__aros_loop_bind: timer port
 * signal, async signal, timer.device open) and nothing reclaims them at exit;
 * the Shell that ran the command reports each leaked bit as
 *   *** '<cmd>' returned with unfreed signal 0x<bit>
 * and frees it.  Reclaim them on the last per-task close, which runs on the
 * closing task with LIBBASE = its own copy, so default_loop_ptr here is that
 * process's loop.  Safe at any exit point: a callback (hence exit()) only
 * ever runs after uv__io_poll has reaped its timer request, so nothing is in
 * flight.  Handle memory comes from the task's stdc pool and goes with the
 * process; only exec resources matter.  Only the task that runs the loop
 * tears it down -- a pthread worker's own copy has default_loop_ptr NULL,
 * and a foreign closer must not free another task's signals.
 * (node closes its default loop itself, c12662e16b; this covers the rest.)
 */
static void uv_aros_reclaim_default_loop(struct UV1IntBase *base)
{
    uv_loop_t *loop = base->default_loop_ptr;

    if (loop == NULL || !uv__aros_loop_owned_by_caller(loop))
        return;

    uv__platform_loop_delete(loop);
    base->default_loop_ptr = NULL;
}

/*
 * A base to lend to a task whose entire creation chain is foreign.
 *
 * genmodule's per-task lookup checks the task's own slot, then its creator's,
 * then this (weak) hook.  Both slots are empty for a thread Chromium created
 * from another Chromium thread - a Blink "ServiceWorker thread" is two or
 * more creations from the Process that opened us, and nothing in between ever
 * called uv1, so there is no slot anywhere on the chain to inherit.  Such a
 * thread still reaches uv1 through the LVO (Blink asks for the time), and its
 * first relwrapper faulted resolving PosixCBase inside a NULL base:
 *   Program failed: ServiceWorker thread, 0x80000003 illegal address access,
 *   __clock_gettime_PosixCBase_relwrapper+0x1D  (alpha9, VS Code + GitHub
 *   Desktop, 2026-09-17)
 * the same shape node.library lends its base for
 * (__node_aros_bind_callback_task_base, 2026-09-15/16).
 *
 * What is lent is the base of the most recent per-task OPEN.  A task with no
 * base of its own cannot be running a loop (uv_aros_default_loop_* hands
 * those the orphan pair), so what it can reach is the leaf, stateless end of
 * the API - clock_gettime, hrtime, the string/memory calls uv1 makes through
 * stdc - for which any valid base answers identically.  A borrow is never an
 * identity: a task that later opens uv1 for real gets its own base, and the
 * value stays valid because a process that opened uv1 keeps it open for its
 * whole life.  Guest-wide, not per-process: with TWO uv1-using processes
 * alive, such a thread may borrow the other one's base, which is why this is
 * the LAST resort and not a substitute for opening the library.
 */
static char *uv_aros_borrow_base;

char *__aros_borrow_offsettable(void)
{
    return uv_aros_borrow_base;
}

static int uv_aros_open_record_base(struct Library *lh)
{
    uv_aros_borrow_base = (char *)lh;
    return 1;
}

static int uv_aros_close_last(struct Library *lh)
{
    /* Stop lending a base that is about to be freed; a later opener installs
       its own. The root base is never handed out here: only per-task dup
       bases reach the hooks. */
    if (uv_aros_borrow_base == (char *)lh)
        uv_aros_borrow_base = NULL;
    uv_aros_reclaim_default_loop((struct UV1IntBase *)lh);
    /* The closing task's bsdsocket base and socket registry (aros-net.c
       keeps them per task); exec calls only, like everything here. */
    uv__aros_net_task_release();
    uv_aros_closer_id = GetETaskID(FindTask(NULL));
    return 1;
}

static int uv_aros_expunge_prepare(struct Library *lh)
{
    ULONG closer = uv_aros_closer_id;

    (void)lh;
    uv_aros_closer_id = 0;
    return closer != 0 && closer == GetETaskID(FindTask(NULL));
}

ADD2OPENLIB(uv_aros_open_record_base, 0)
ADD2CLOSELIB(uv_aros_close_last, 0)
ADD2EXPUNGELIB(uv_aros_expunge_prepare, 0)
