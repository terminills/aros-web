/*
 * AROS task-local backend for compiler-emulated TLS used by V8.
 *
 * Chromium's V8 is built with GCC emulated TLS.  libgcc normally keys the
 * emutls array through pthread-specific storage, but Chromium also runs V8
 * work on native Exec tasks.  Those tasks do not have distinct pthread
 * identities, so unrelated tasks can observe the same thread_local values.
 *
 * Keep the compiler ABI at the v8.library boundary, but store each task's
 * emutls array in task.resource instead.
 */

#include <exec/types.h>
#include <proto/exec.h>
#include <defines/task.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef uintptr_t emutls_word;

struct __emutls_object
{
    emutls_word size;
    emutls_word align;
    union
    {
        emutls_word offset;
        void *ptr;
    } loc;
    void *templ;
};

struct emutls_array
{
    emutls_word size;
    void *data[];
};

static APTR v8_taskres_base;
static volatile LONG v8_emutls_slot;
static emutls_word v8_emutls_size;

static APTR v8_get_taskres_base(void)
{
    APTR base = __atomic_load_n(&v8_taskres_base, __ATOMIC_ACQUIRE);

    if (!base)
    {
        APTR opened = OpenResource("task.resource");
        APTR expected = NULL;

        if (!opened)
            abort();

        if (!__atomic_compare_exchange_n(&v8_taskres_base, &expected, opened,
                                         FALSE, __ATOMIC_RELEASE,
                                         __ATOMIC_ACQUIRE))
            base = expected;
        else
            base = opened;
    }

    return base;
}

static LONG v8_get_emutls_slot(APTR taskres_base)
{
    LONG slot;

    for (;;)
    {
        slot = __atomic_load_n(&v8_emutls_slot, __ATOMIC_ACQUIRE);
        if (slot > 0)
            return slot;

        if (slot == 0)
        {
            LONG expected = 0;

            if (__atomic_compare_exchange_n(&v8_emutls_slot, &expected, -1,
                                             FALSE, __ATOMIC_ACQ_REL,
                                             __ATOMIC_ACQUIRE))
            {
                slot = __AllocTaskStorageSlot_WB(taskres_base);
                if (slot <= 0)
                    abort();

                __atomic_store_n(&v8_emutls_slot, slot, __ATOMIC_RELEASE);
                return slot;
            }
        }

        Reschedule();
    }
}

static void *v8_emutls_alloc(const struct __emutls_object *obj)
{
    void *allocation;
    void *result;

    if (obj->align <= sizeof(void *))
    {
        allocation = malloc(obj->size + sizeof(void *));
        if (!allocation)
            abort();

        ((void **)allocation)[0] = allocation;
        result = (char *)allocation + sizeof(void *);
    }
    else
    {
        uintptr_t aligned;

        allocation = malloc(obj->size + sizeof(void *) + obj->align - 1);
        if (!allocation)
            abort();

        aligned = ((uintptr_t)allocation + sizeof(void *) + obj->align - 1)
                  & ~(uintptr_t)(obj->align - 1);
        result = (void *)aligned;
        ((void **)result)[-1] = allocation;
    }

    if (obj->templ)
        memcpy(result, obj->templ, obj->size);
    else
        memset(result, 0, obj->size);

    return result;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wbuiltin-declaration-mismatch"

void *__emutls_get_address(struct __emutls_object *obj)
{
    APTR taskres_base = v8_get_taskres_base();
    LONG slot = v8_get_emutls_slot(taskres_base);
    emutls_word offset = __atomic_load_n(&obj->loc.offset, __ATOMIC_ACQUIRE);
    struct emutls_array *array;
    void *result;

    if (__builtin_expect(offset == 0, 0))
    {
        emutls_word candidate =
            __atomic_add_fetch(&v8_emutls_size, 1, __ATOMIC_ACQ_REL);
        emutls_word expected = 0;

        if (__atomic_compare_exchange_n(&obj->loc.offset, &expected, candidate,
                                        FALSE, __ATOMIC_RELEASE,
                                        __ATOMIC_ACQUIRE))
            offset = candidate;
        else
            offset = expected;
    }

    array = (struct emutls_array *)
        __GetTaskStorageSlot_WB(taskres_base, slot);

    if (__builtin_expect(array == NULL, 0))
    {
        emutls_word size = offset + 32;

        array = calloc(1, sizeof(*array) + size * sizeof(array->data[0]));
        if (!array)
            abort();

        array->size = size;
        if (!__SetTaskStorageSlot_WB(taskres_base, slot, (IPTR)array))
            abort();
    }
    else if (__builtin_expect(offset > array->size, 0))
    {
        emutls_word old_size = array->size;
        emutls_word size = old_size * 2;
        struct emutls_array *grown;

        if (offset > size)
            size = offset + 32;

        grown = realloc(array,
                        sizeof(*grown) + size * sizeof(grown->data[0]));
        if (!grown)
            abort();

        memset(grown->data + old_size, 0,
               (size - old_size) * sizeof(grown->data[0]));
        grown->size = size;
        array = grown;

        if (!__SetTaskStorageSlot_WB(taskres_base, slot, (IPTR)array))
            abort();
    }

    result = array->data[offset - 1];
    if (__builtin_expect(result == NULL, 0))
    {
        result = v8_emutls_alloc(obj);
        array->data[offset - 1] = result;
    }

    return result;
}

void __emutls_register_common(struct __emutls_object *obj,
                              emutls_word size,
                              emutls_word align,
                              void *templ)
{
    if (obj->size < size)
    {
        obj->size = size;
        obj->templ = NULL;
    }
    if (obj->align < align)
        obj->align = align;
    if (templ && size == obj->size)
        obj->templ = templ;
}

#pragma GCC diagnostic pop
