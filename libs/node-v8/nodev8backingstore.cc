/*
 * Node-specific ArrayBuffer allocation semantics over shared v8.library.
 */

#include <exec/types.h>
#include <aros/debug.h>
#include <aros/libcall.h>
#include <proto/v8.h>

#include LC_LIBDEFS_FILE

/* Exec's classic Allocate() macro collides with V8 C++ method names. */
#ifdef Allocate
#undef Allocate
#endif

#include <v8-array-buffer.h>
#include <v8-isolate.h>

#include <cstddef>
#include <cstring>
#include <memory>
#include <string>

namespace
{

using GetArrayBufferAllocatorFn =
    v8::ArrayBuffer::Allocator *(*)(v8::Isolate *);
using WrapBackingStoreFn = std::unique_ptr<v8::BackingStore> (*)(
    void *,
    size_t,
    v8::BackingStore::DeleterCallback,
    void *);
using DestroyBackingStoreFn = void (*)(v8::BackingStore *);
using GetDefaultLocaleFn = const std::string &(*)(void *);

static void FreeArrayBuffer(void *data, size_t length, void *deleterData)
{
    auto *allocator =
        static_cast<v8::ArrayBuffer::Allocator *>(deleterData);

    allocator->Free(data, length);
}

} // namespace

/*
 * Keep the exact V8 destructor outside node-v8.library.  This resolver is the
 * first entry in the compatibility library's direct C++ symbol bridge.
 */
extern "C" void NodeV8BackingStoreDestructor(v8::BackingStore *store)
    __asm__("_ZN2v812BackingStoreD1Ev");

extern "C" void NodeV8BackingStoreDestructor(v8::BackingStore *store)
{
    auto destroy = reinterpret_cast<DestroyBackingStoreFn>(
        V8FindEngineSymbol("_ZN2v812BackingStoreD1Ev"));

    if (destroy != nullptr)
        destroy(store);
}

extern "C" AROS_LH2(APTR, NodeV8NewBackingStoreForNodeLTS,
    AROS_LHA(APTR, isolateHandle, A0),
    AROS_LHA(IPTR, byteLength, D0),
    LIBBASETYPEPTR, LIBBASE, 5, NodeV8)
{
    AROS_LIBFUNC_INIT

    (void)LIBBASE;

    auto getAllocator = reinterpret_cast<GetArrayBufferAllocatorFn>(
        V8FindEngineSymbol("_ZN2v87Isolate23GetArrayBufferAllocatorEv"));
    auto wrapBackingStore = reinterpret_cast<WrapBackingStoreFn>(
        V8FindEngineSymbol(
            "_ZN2v811ArrayBuffer15NewBackingStoreEPvmPFvS1_mS1_ES1_"));

    if (isolateHandle == nullptr || getAllocator == nullptr ||
        wrapBackingStore == nullptr)
        return nullptr;

    auto *isolate = static_cast<v8::Isolate *>(isolateHandle);
    auto *allocator = getAllocator(isolate);
    if (allocator == nullptr)
        return nullptr;

    size_t length = static_cast<size_t>(byteLength);
    void *data = allocator->AllocateUninitialized(length);
    if (data == nullptr && length != 0)
        return nullptr;

    std::unique_ptr<v8::BackingStore> store =
        wrapBackingStore(data, length, FreeArrayBuffer, allocator);
    if (!store)
    {
        if (data != nullptr)
            allocator->Free(data, length);
        return nullptr;
    }

    return static_cast<APTR>(store.release());

    AROS_LIBFUNC_EXIT
}

extern "C" AROS_LH3(IPTR, NodeV8GetDefaultLocale,
    AROS_LHA(APTR, isolateHandle, A0),
    AROS_LHA(STRPTR, buffer, A1),
    AROS_LHA(IPTR, bufferSize, D0),
    LIBBASETYPEPTR, LIBBASE, 6, NodeV8)
{
    AROS_LIBFUNC_INIT

    (void)LIBBASE;

    auto getDefaultLocale = reinterpret_cast<GetDefaultLocaleFn>(
        V8FindEngineSymbol(
            "_ZN2v88internal7Isolate13DefaultLocaleB5cxx11Ev"));
    if (isolateHandle == nullptr || getDefaultLocale == nullptr)
        return -1;

    const std::string &locale = getDefaultLocale(isolateHandle);
    const IPTR localeLength = static_cast<IPTR>(locale.size());

    if (buffer == nullptr || bufferSize <= 0)
        return localeLength;

    const size_t capacity = static_cast<size_t>(bufferSize);
    const size_t copyLength =
        locale.size() < capacity - 1 ? locale.size() : capacity - 1;
    std::memcpy(buffer, locale.data(), copyLength);
    buffer[copyLength] = '\0';

    return localeLength;

    AROS_LIBFUNC_EXIT
}

/*
 * Resolve an exact-build public V8 C++ ABI entry through v8.library.  The
 * caller-side node-v8 ABI archive uses this LVO from small assembly thunks;
 * the engine remains resident in v8.library and is never copied into Node.
 */
extern "C" AROS_LH1(APTR, NodeV8ResolveEngineSymbol,
    AROS_LHA(CONST_STRPTR, symbolName, A0),
    LIBBASETYPEPTR, LIBBASE, 7, NodeV8)
{
    AROS_LIBFUNC_INIT

    (void)LIBBASE;

    if (symbolName == nullptr || symbolName[0] == '\0')
        return nullptr;

    APTR address = V8FindEngineSymbol(symbolName);
    static ULONG resolutionCount;
    ULONG index = __atomic_fetch_add(&resolutionCount, 1, __ATOMIC_RELAXED);
    if (index < 1024)
        bug("[NodeV8:%lu] %s -> %p\n", index, symbolName, address);

    return address;

    AROS_LIBFUNC_EXIT
}
