/*
 * Per-Process V8 platform context for the resident v8.library engine.
 *
 * AROS Processes share the library's writable image, but each relocatable
 * Chromium image owns its own gin::V8Platform. Store that pointer on the
 * Process task and let pthread/native worker tasks inherit it through
 * task.resource's parent slot.
 */

#include <exec/types.h>
#include <proto/exec.h>
#include <defines/task.h>

#include <stdlib.h>

static APTR v8_platform_taskres_base;
static volatile LONG v8_platform_slot;

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

void __aros_v8_set_current_platform(void *platform)
{
    APTR taskres_base = v8_platform_get_taskres_base();
    LONG slot = v8_platform_get_slot(taskres_base);

    if (!__SetTaskStorageSlot_WB(taskres_base, slot, (IPTR)platform))
        abort();
}

void *__aros_v8_get_current_platform(void)
{
    APTR taskres_base = v8_platform_get_taskres_base();
    LONG slot = v8_platform_get_slot(taskres_base);
    APTR platform = (APTR)__GetTaskStorageSlot_WB(taskres_base, slot);

    if (!platform)
    {
        platform = (APTR)__GetParentTaskStorageSlot_WB(taskres_base, slot);
        if (platform &&
            !__SetTaskStorageSlot_WB(taskres_base, slot, (IPTR)platform))
            abort();
    }

    return platform;
}
