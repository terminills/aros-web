/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    TaskBT - list every task with its state and signal masks and, for tasks
    that are not running, a symbolised backtrace taken from the CPU context
    exec saved at the last switch (ETask et_RegFrame).  Frames are followed
    through the frame-pointer chain (rbp), so code built with
    -fno-omit-frame-pointer (Chromium's AROS toolchain) walks cleanly;
    return addresses are named through debug.library, which the ELF loader
    feeds with every loaded module's symbol table.

    Written for the hosted Chromium bring-up: run it while the browser is
    stalled to see what each of its tasks is waiting on.

        TaskBT [NAME <substring>] [DEPTH <n>] [ALL] [MUTEX <symbol>]

    NAME   only backtrace tasks whose name contains this (default: all)
    DEPTH  maximum frames per task (default 40)
    ALL    also dump tasks in the ready state (default: waiting only)
    MUTEX  name of a data symbol holding a pthread_mutex_t (looked up with
           EnumerateSymbols over every registered module); its exec
           semaphore state - owner, nest/queue counts, queued waiters - is
           dumped alongside the task snapshot so a stuck pthread_mutex_lock
           can be tied to whoever holds (or corrupted) it.

    x86_64 hosted only (reads struct ExceptionContext from the register frame).
*/

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/task.h>
#include <proto/debug.h>

#include <exec/tasks.h>
#include <exec/execbase.h>
#include <dos/dosextens.h>
#include <resources/task.h>
#include <libraries/debug.h>
#include <aros/x86_64/cpucontext.h>

#include <exec/semaphores.h>
#include <utility/hooks.h>
#include <pthread.h>

#include <string.h>

const TEXT version[] = "$VER: TaskBT 1.1 (6.9.2026)\n";

#define TEMPLATE "NAME/K,DEPTH/N,ALL/S,MUTEX/K"
enum { ARG_NAME, ARG_DEPTH, ARG_ALL, ARG_MUTEX, ARG_COUNT };

#define MAX_FRAMES  128
#define MAX_WAITERS 32

APTR TaskResBase;
struct Library *DebugBase;

struct snapshot
{
    struct Task *task;
    char        name[64];
    WORD        state;
    BYTE        pri;
    ULONG       sigalloc, sigwait, sigrecvd;
    APTR        splower, spupper, spreg;
    BOOL        havectx;
    UQUAD       rip, rsp, rbp;
    int         nframes;
    UQUAD       frame[MAX_FRAMES];
};

/* Symbol lookup: EnumerateSymbols walks every symbol of every registered
   module; keep the first exact match (and its module) for a name. */
struct symsearch
{
    const char   *want;
    APTR          addr;
    char          module[64];
};

AROS_UFH3(static void, symhook,
    AROS_UFHA(struct Hook *,       hook, A0),
    AROS_UFHA(APTR,                obj,  A2),
    AROS_UFHA(struct SymbolInfo *, si,   A1))
{
    AROS_USERFUNC_INIT

    struct symsearch *ss = hook->h_Data;

    if (!ss->addr && si->si_SymbolName && !strcmp(si->si_SymbolName, ss->want))
    {
        ss->addr = si->si_SymbolStart;
        strncpy(ss->module, si->si_ModuleName ? (const char *)si->si_ModuleName : "?",
                sizeof(ss->module) - 1);
    }

    AROS_USERFUNC_EXIT
}

static APTR find_symbol(const char *name, char *module, int modlen)
{
    struct symsearch ss = { name, NULL, { 0 } };
    struct Hook hook;

    if (!DebugBase)
        return NULL;
    memset(&hook, 0, sizeof(hook));
    hook.h_Entry = (HOOKFUNC)AROS_ASMSYMNAME(symhook);
    hook.h_Data  = &ss;
    EnumerateSymbols(&hook, TAG_DONE);
    if (ss.addr)
        strncpy(module, ss.module, modlen - 1);
    return ss.addr;
}

/* Snapshot of a pthread_mutex_t: the exec semaphore wrapped by AROS's
   libpthread, plus the tasks queued on it (SemaphoreRequest.sr_Waiter). */
struct mutexsnap
{
    APTR         addr;
    int          kind, incond;
    UBYTE        ln_type;
    WORD         nest, queue;
    struct Task *owner;
    APTR         wq_head, wq_tail, wq_tailpred;
    int          nwaiters;
    struct Task *waiter[MAX_WAITERS];
};

static void snapshot_mutex(pthread_mutex_t *m, struct mutexsnap *ms)
{
    struct SignalSemaphore *sem = &m->semaphore;
    struct SemaphoreRequest *sr;

    memset(ms, 0, sizeof(*ms));
    ms->addr    = m;
    ms->kind    = m->kind;
    ms->incond  = m->incond;
    ms->ln_type = sem->ss_Link.ln_Type;
    ms->nest    = sem->ss_NestCount;
    ms->queue   = sem->ss_QueueCount;
    ms->owner   = sem->ss_Owner;
    ms->wq_head     = sem->ss_WaitQueue.mlh_Head;
    ms->wq_tail     = sem->ss_WaitQueue.mlh_Tail;
    ms->wq_tailpred = sem->ss_WaitQueue.mlh_TailPred;

    /* Only walk a list whose head is sane: an uninitialised or clobbered
       semaphore has garbage here and we must not fault under Forbid(). */
    if (ms->wq_tail != NULL || ms->wq_head == NULL)
        return;
    ForeachNode(&sem->ss_WaitQueue, sr)
    {
        if (ms->nwaiters >= MAX_WAITERS)
            break;
        ms->waiter[ms->nwaiters++] = sr->sr_Waiter;
    }
}

static const char *task_name(struct Task *task)
{
    if (task->tc_Node.ln_Type == NT_PROCESS && ((struct Process *)task)->pr_CLI)
    {
        struct CommandLineInterface *cli = BADDR(((struct Process *)task)->pr_CLI);
        if (cli->cli_CommandName)
            return AROS_BSTR_ADDR(cli->cli_CommandName);
    }
    return task->tc_Node.ln_Name ? task->tc_Node.ln_Name : "(null)";
}

/*
 * Collect everything about one task under Forbid(): the task cannot run
 * while we read its saved context and stack, and we call nothing that can
 * Wait() in here.
 */
static void snapshot_task(struct Task *task, struct snapshot *s, int depth)
{
    struct ETask *et;
    const char *n;

    memset(s, 0, sizeof(*s));
    s->task = task;
    n = task_name(task);
    strncpy(s->name, n, sizeof(s->name) - 1);
    s->state    = task->tc_State;
    s->pri      = task->tc_Node.ln_Pri;
    s->sigalloc = task->tc_SigAlloc;
    s->sigwait  = task->tc_SigWait;
    s->sigrecvd = task->tc_SigRecvd;
    s->splower  = task->tc_SPLower;
    s->spupper  = task->tc_SPUpper;
    s->spreg    = task->tc_SPReg;

    if (task->tc_State == TS_RUN)
        return;                         /* our own context is meaningless */

    et = GetETask(task);
    if (!et || !et->et_RegFrame)
        return;

    {
        struct ExceptionContext *ctx = et->et_RegFrame;
        UQUAD fp, lo = (UQUAD)s->splower, hi = (UQUAD)s->spupper;
        int i;

        s->havectx = TRUE;
        s->rip = ctx->rip;
        s->rsp = ctx->rsp;
        s->rbp = ctx->rbp;

        s->frame[0] = ctx->rip;
        s->nframes = 1;

        fp = ctx->rbp;
        for (i = 1; i < depth && i < MAX_FRAMES; i++)
        {
            UQUAD next, ret;

            /* The frame must lie on this task's stack, be aligned, and
               each frame must be above the previous one. */
            if (fp < lo || fp + 16 > hi || (fp & 7))
                break;
            next = ((UQUAD *)fp)[0];
            ret  = ((UQUAD *)fp)[1];
            if (ret == 0)
                break;
            s->frame[s->nframes++] = ret;
            if (next <= fp)
                break;
            fp = next;
        }
    }
}

static void print_location(UQUAD addr)
{
    char *modname = NULL, *symname = NULL, *segname = NULL;
    void *symstart = NULL, *segstart = NULL;
    IPTR segnum = 0;
    struct TagItem tags[] =
    {
        { DL_ModuleName,    (IPTR)&modname  },
        { DL_SymbolName,    (IPTR)&symname  },
        { DL_SymbolStart,   (IPTR)&symstart },
        { DL_SegmentName,   (IPTR)&segname  },
        { DL_SegmentStart,  (IPTR)&segstart },
        { DL_SegmentNumber, (IPTR)&segnum   },
        { TAG_DONE,         0               }
    };

    if (DebugBase && DecodeLocationA((APTR)addr, tags))
    {
        Printf("0x%012lx  %s", addr, modname ? modname : "?");
        if (segname)
            Printf(" [%s", segname);
        else
            Printf(" [seg %ld", segnum);
        if (segstart)
            Printf("+0x%lx]", addr - (UQUAD)segstart);
        else
            PutStr("]");
        if (symname)
            Printf("  %s+0x%lx", symname, symstart ? addr - (UQUAD)symstart : 0);
        PutStr("\n");
    }
    else
        Printf("0x%012lx  (not in a registered module)\n", addr);
}

static const char *state_name(WORD state)
{
    switch (state)
    {
        case TS_INVALID: return "invalid";
        case TS_ADDED:   return "added";
        case TS_RUN:     return "running";
        case TS_READY:   return "ready";
        case TS_WAIT:    return "waiting";
        case TS_EXCEPT:  return "except";
        case TS_REMOVED: return "removed";
        default:         return "?";
    }
}

/* Name a task pointer from the snapshot table (the task may be dead). */
static const char *snap_name(struct snapshot *snaps, int count, struct Task *task)
{
    int i;

    if (!task)
        return "none";
    for (i = 0; i < count; i++)
        if (snaps[i].task == task)
            return snaps[i].name;
    return "NOT IN TASK LIST";
}

static void print_mutex(const char *sym, const char *module, struct mutexsnap *ms,
                        struct snapshot *snaps, int count)
{
    int i;

    Printf("== mutex %s (%s) at 0x%012lx\n", sym, module, (IPTR)ms->addr);
    Printf("   kind=%ld incond=%ld ln_Type=%ld (NT_SIGNALSEM=%ld)\n",
           (LONG)ms->kind, (LONG)ms->incond, (LONG)ms->ln_type, (LONG)NT_SIGNALSEM);
    Printf("   NestCount=%ld QueueCount=%ld Owner=0x%012lx %s\n",
           (LONG)ms->nest, (LONG)ms->queue, (IPTR)ms->owner,
           snap_name(snaps, count, ms->owner));
    Printf("   WaitQueue head=%p tail=%p tailpred=%p\n",
           ms->wq_head, ms->wq_tail, ms->wq_tailpred);
    for (i = 0; i < ms->nwaiters; i++)
        Printf("   waiter #%ld 0x%012lx %s\n", (LONG)i, (IPTR)ms->waiter[i],
               snap_name(snaps, count, ms->waiter[i]));
    PutStr("\n");
}

int main(void)
{
    IPTR args[ARG_COUNT] = { 0, 0, 0, 0 };
    struct RDArgs *rda;
    const char *filter = NULL, *mutexsym = NULL;
    char mutexmod[64] = "";
    pthread_mutex_t *mutex = NULL;
    struct mutexsnap ms;
    int depth = 40;
    BOOL all = FALSE;
    struct TaskList *tl;
    struct Task *task;
    struct snapshot *snaps;
    int count = 0, cap = 256, i;

    rda = ReadArgs(TEMPLATE, args, NULL);
    if (!rda)
    {
        PrintFault(IoErr(), "TaskBT");
        return RETURN_FAIL;
    }
    if (args[ARG_NAME])  filter   = (const char *)args[ARG_NAME];
    if (args[ARG_DEPTH]) depth    = *(LONG *)args[ARG_DEPTH];
    if (args[ARG_ALL])   all      = TRUE;
    if (args[ARG_MUTEX]) mutexsym = (const char *)args[ARG_MUTEX];
    if (depth < 1) depth = 1;
    if (depth > MAX_FRAMES) depth = MAX_FRAMES;

    TaskResBase = OpenResource("task.resource");
    if (!TaskResBase)
    {
        PutStr("TaskBT: can't open task.resource\n");
        FreeArgs(rda);
        return RETURN_FAIL;
    }
    DebugBase = OpenLibrary("debug.library", 0);
    if (!DebugBase)
        PutStr("TaskBT: debug.library not available, addresses will not be symbolised\n");

    snaps = AllocVec(sizeof(*snaps) * cap, MEMF_CLEAR);
    if (!snaps)
    {
        PutStr("TaskBT: out of memory\n");
        FreeArgs(rda);
        return RETURN_FAIL;
    }

    /* Symbol lookup takes debug.library's module semaphore, so do it
       before freezing the system. */
    if (mutexsym)
    {
        mutex = find_symbol(mutexsym, mutexmod, sizeof(mutexmod));
        if (!mutex)
            Printf("TaskBT: symbol '%s' not found in any registered module\n", mutexsym);
    }

    /* Snapshot phase: no output, no Wait(), tasks frozen. */
    Forbid();
    tl = LockTaskList(LTF_ALL);
    while ((task = NextTaskEntry(tl, LTF_ALL)) != NULL && count < cap)
        snapshot_task(task, &snaps[count++], depth);
    UnLockTaskList(tl, LTF_ALL);
    if (mutex)
        snapshot_mutex(mutex, &ms);
    Permit();

    Printf("%ld tasks (filter '%s', depth %ld%s)\n\n", (LONG)count,
           filter ? filter : "*", (LONG)depth, all ? ", ready included" : "");
    if (mutex)
        print_mutex(mutexsym, mutexmod, &ms, snaps, count);
    PutStr("       Address     Pri  State    SigAlloc  SigWait   SigRecvd  Name\n");
    for (i = 0; i < count; i++)
    {
        struct snapshot *s = &snaps[i];
        Printf("0x%012lx  %4ld  %-8s %08lx  %08lx  %08lx  %s\n",
               (IPTR)s->task, (LONG)s->pri, state_name(s->state),
               (IPTR)s->sigalloc, (IPTR)s->sigwait, (IPTR)s->sigrecvd, s->name);
    }
    PutStr("\n");

    for (i = 0; i < count; i++)
    {
        struct snapshot *s = &snaps[i];
        int f;

        if (filter && !strstr(s->name, filter))
            continue;
        if (s->state == TS_RUN)
            continue;
        if (s->state != TS_WAIT && !all)
            continue;

        Printf("== 0x%012lx %s (%s, pri %ld) waiting for %08lx, has %08lx\n",
               (IPTR)s->task, s->name, state_name(s->state), (LONG)s->pri,
               (IPTR)s->sigwait, (IPTR)s->sigrecvd);
        Printf("   stack %p..%p sp=%p", s->splower, s->spupper, s->spreg);
        if (!s->havectx)
        {
            PutStr("  (no saved context)\n\n");
            continue;
        }
        Printf("  rip=0x%012lx rsp=0x%012lx rbp=0x%012lx\n", s->rip, s->rsp, s->rbp);
        for (f = 0; f < s->nframes; f++)
        {
            Printf("   #%-2ld ", (LONG)f);
            print_location(s->frame[f]);
        }
        PutStr("\n");
    }

    FreeVec(snaps);
    if (DebugBase)
        CloseLibrary(DebugBase);
    FreeArgs(rda);
    return RETURN_OK;
}
