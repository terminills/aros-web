/*
 * Per-Process V8 platform context for the resident v8.library engine.
 *
 * AROS Processes share the library's writable image, but each relocatable
 * Chromium image owns its own gin::V8Platform and every Node owns its own
 * NodePlatform. Store that pointer on the task that installs it and let
 * pthread/native worker tasks inherit it from their creation chain.
 *
 * The platform also chooses V8's page allocator, worker pool and job
 * scheduler, so a task that resolves another Process's platform commits its
 * own reservations through the other allocator and posts its work to the
 * other Process's workers. task.resource's parent slot reaches one creation
 * level only, so the installing tasks are also recorded here and a task
 * whose own and parent slots are empty walks its whole et_Parent chain.
 *
 * The chain must stop at a program boundary: Claude Code starts Chromium
 * through C:OpenURL, so Chromium's main task is a descendant of a Node task.
 * Every program that opens v8.library marks its opening task as a root whose
 * platform is pending until the program installs one; a lookup that reaches
 * a pending root gets no platform rather than its parent program's.
 */

#include <exec/types.h>
#include <exec/tasks.h>
#include <exec/semaphores.h>
#include <aros/debug.h>
#include <aros/symbolsets.h>
#include <proto/exec.h>
#include <defines/task.h>

#include <stdlib.h>

#define V8_PLATFORM_OWNERS      64
#define V8_PLATFORM_MAXDEPTH    32
#define V8_PLATFORM_MAXREPORTS  16
#define V8_PLATFORM_PENDING     ((void *)1)

struct v8_platform_owner
{
    struct Task *task;
    ULONG        id;
    void        *platform;
};

static APTR v8_platform_taskres_base;
static volatile LONG v8_platform_slot;

static struct SignalSemaphore v8_platform_owners_lock;
static volatile LONG v8_platform_owners_ready;
static struct v8_platform_owner v8_platform_owners[V8_PLATFORM_OWNERS];
static volatile LONG v8_platform_reports;

static APTR v8_platform_get_taskres_base(void)
{
    APTR base = __atomic_load_n(&v8_platform_taskres_base, __ATOMIC_ACQUIRE);

    if (!base)
    {
        APTR opened = OpenResource("task.resource");
        APTR expected = NULL;

        if (!opened)
            abort();

        if (!__atomic_compare_exchange_n(&v8_platform_taskres_base, &expected,
                                         opened, FALSE, __ATOMIC_RELEASE,
                                         __ATOMIC_ACQUIRE))
            base = expected;
        else
            base = opened;
    }

    return base;
}

static LONG v8_platform_get_slot(APTR taskres_base)
{
    LONG slot;

    for (;;)
    {
        slot = __atomic_load_n(&v8_platform_slot, __ATOMIC_ACQUIRE);
        if (slot > 0)
            return slot;

        if (slot == 0)
        {
            LONG expected = 0;

            if (__atomic_compare_exchange_n(&v8_platform_slot, &expected, -1,
                                             FALSE, __ATOMIC_ACQ_REL,
                                             __ATOMIC_ACQUIRE))
            {
                slot = __AllocTaskStorageSlot_WB(taskres_base);
                if (slot <= 0)
                    abort();

                __atomic_store_n(&v8_platform_slot, slot, __ATOMIC_RELEASE);
                return slot;
            }
        }

        Reschedule();
    }
}

static struct SignalSemaphore *v8_platform_owners_sem(void)
{
    for (;;)
    {
        LONG state = __atomic_load_n(&v8_platform_owners_ready, __ATOMIC_ACQUIRE);

        if (state > 0)
            return &v8_platform_owners_lock;

        if (state == 0)
        {
            LONG expected = 0;

            if (__atomic_compare_exchange_n(&v8_platform_owners_ready, &expected,
                                             -1, FALSE, __ATOMIC_ACQ_REL,
                                             __ATOMIC_ACQUIRE))
            {
                InitSemaphore(&v8_platform_owners_lock);
                __atomic_store_n(&v8_platform_owners_ready, 1, __ATOMIC_RELEASE);
                return &v8_platform_owners_lock;
            }
        }

        Reschedule();
    }
}

/* Entries of tasks that have exited stay until their slot is needed: the
   ETask ID keeps a recycled Task structure from matching a dead entry. */
static void v8_platform_record_owner(struct Task *task, void *platform)
{
    struct SignalSemaphore *sem = v8_platform_owners_sem();
    ULONG id = GetETaskID(task);
    struct v8_platform_owner *free_entry = NULL;
    int i;

    if (!id)
        return;

    ObtainSemaphore(sem);
    for (i = 0; i < V8_PLATFORM_OWNERS; i++)
    {
        struct v8_platform_owner *o = &v8_platform_owners[i];

        if (o->task == task && o->id == id)
        {
            if (platform)
                o->platform = platform;
            else
                o->task = NULL;
            ReleaseSemaphore(sem);
            return;
        }
        if (!free_entry && (!o->task || !FindTaskByPID(o->id)))
            free_entry = o;
    }
    if (platform && free_entry)
    {
        free_entry->task = task;
        free_entry->id = id;
        free_entry->platform = platform;
    }
    ReleaseSemaphore(sem);
}

/* Exec relinks the children of an exiting task to its parent, so the chain
   only ever names live tasks; Forbid() keeps it still while it is copied, as
   task.resource does for the parent slot. */
static int v8_platform_copy_chain(struct Task *me, struct Task **chain, ULONG *ids)
{
    struct ETask *et;
    int depth = 0;

    Forbid();
    et = GetETask(me);
    while (et && et->et_Parent && depth < V8_PLATFORM_MAXDEPTH)
    {
        struct Task *t = (struct Task *)et->et_Parent;

        chain[depth] = t;
        ids[depth] = GetETaskID(t);
        depth++;
        et = GetETask(t);
    }
    Permit();

    return depth;
}

static struct v8_platform_owner *v8_platform_find_owner(struct Task *t, ULONG id)
{
    int j;

    for (j = 0; j < V8_PLATFORM_OWNERS; j++)
    {
        struct v8_platform_owner *o = &v8_platform_owners[j];

        if (o->task == t && o->id == id && o->platform)
            return o;
    }
    return NULL;
}

/* The nearest recorded task at or above me decides, pending or not. */
static void *v8_platform_from_chain(struct Task *me, BOOL include_me)
{
    struct Task *chain[V8_PLATFORM_MAXDEPTH + 1];
    ULONG ids[V8_PLATFORM_MAXDEPTH + 1];
    struct SignalSemaphore *sem;
    void *platform = NULL;
    int depth, i, first = include_me ? 0 : 1;

    chain[0] = me;
    ids[0] = GetETaskID(me);
    depth = 1 + v8_platform_copy_chain(me, chain + 1, ids + 1);

    sem = v8_platform_owners_sem();
    ObtainSemaphoreShared(sem);
    for (i = first; i < depth && !platform; i++)
    {
        struct v8_platform_owner *o = v8_platform_find_owner(chain[i], ids[i]);

        if (o)
            platform = o->platform;
    }
    ReleaseSemaphore(sem);

    return platform;
}

/* A program may install its platform on any of its tasks (Chromium does it on
   Chrome_InProcRendererThread, not on the task that opened v8.library): the
   first one installed becomes the platform of the program's pending root. */
static void v8_platform_promote_root(struct Task *me, void *platform)
{
    struct Task *chain[V8_PLATFORM_MAXDEPTH];
    ULONG ids[V8_PLATFORM_MAXDEPTH];
    struct SignalSemaphore *sem;
    int depth, i;

    if (!platform)
        return;
    depth = v8_platform_copy_chain(me, chain, ids);

    sem = v8_platform_owners_sem();
    ObtainSemaphore(sem);
    for (i = 0; i < depth; i++)
    {
        struct v8_platform_owner *o = v8_platform_find_owner(chain[i], ids[i]);

        if (o)
        {
            if (o->platform == V8_PLATFORM_PENDING)
                o->platform = platform;
            break;
        }
    }
    ReleaseSemaphore(sem);
}

void __aros_v8_set_current_platform(void *platform)
{
    APTR taskres_base = v8_platform_get_taskres_base();
    LONG slot = v8_platform_get_slot(taskres_base);

    if (!__SetTaskStorageSlot_WB(taskres_base, slot, (IPTR)platform))
        abort();

    v8_platform_record_owner(FindTask(NULL), platform);
    v8_platform_promote_root(FindTask(NULL), platform);
}

void *__aros_v8_get_current_platform(void)
{
    APTR taskres_base = v8_platform_get_taskres_base();
    LONG slot = v8_platform_get_slot(taskres_base);
    APTR platform = (APTR)__GetTaskStorageSlot_WB(taskres_base, slot);
    struct Task *me;

    if (platform && platform != V8_PLATFORM_PENDING)
        return platform;

    me = FindTask(NULL);
    if (platform == V8_PLATFORM_PENDING)
        platform = v8_platform_from_chain(me, TRUE);
    else
    {
        platform = (APTR)__GetParentTaskStorageSlot_WB(taskres_base, slot);
        if (!platform || platform == V8_PLATFORM_PENDING)
            platform = v8_platform_from_chain(me, FALSE);
    }

    if (platform == V8_PLATFORM_PENDING)
        return NULL;

    if (platform)
    {
        if (!__SetTaskStorageSlot_WB(taskres_base, slot, (IPTR)platform))
            abort();
    }
    else if (__atomic_add_fetch(&v8_platform_reports, 1, __ATOMIC_RELAXED)
             <= V8_PLATFORM_MAXREPORTS)
    {
        bug("[v8.library] no V8 platform on the creation chain of '%s'\n",
            me->tc_Node.ln_Name ? me->tc_Node.ln_Name : "");
    }

    return platform;
}

/* A task opening v8.library for the first time starts a program of its own:
   record it as a root with its platform pending, so neither it nor its
   threads inherit the platform of the program that launched it. */
static int v8_platform_open_root(APTR base)
{
    APTR taskres_base = v8_platform_get_taskres_base();
    LONG slot = v8_platform_get_slot(taskres_base);
    struct Task *me = FindTask(NULL);

    (void)base;
    if (__GetTaskStorageSlot_WB(taskres_base, slot))
        return 1;
    if (!__SetTaskStorageSlot_WB(taskres_base, slot, (IPTR)V8_PLATFORM_PENDING))
        return 1;
    v8_platform_record_owner(me, V8_PLATFORM_PENDING);
    return 1;
}

ADD2OPENLIB(v8_platform_open_root, 0)

/* Called by V8's flag parser (src/flags/flags.cc) for a flag write it
   refused because the engine was already initialized by another program. */
void __aros_v8_flag_refused(const char *arg)
{
    bug("[V8:flags] %s refused: the resident engine is running\n", arg);
}
