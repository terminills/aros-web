/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Desc: v8.library allocator seam - see v8_allocator.cpp for the reasoning.
*/

#ifndef V8_ALLOCATOR_H
#define V8_ALLOCATOR_H

#include <exec/types.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *(*V8AllocFunc)(size_t size);
typedef void  (*V8FreeFunc)(void *ptr);

/*
 * Install the allocator this library's operator new/delete route through.
 *
 * MUST be called before anything else touches V8 - including node, which runs
 * before CEF in Electron's lifecycle. Returns FALSE if an allocation has
 * already been served, in which case the caller must treat it as fatal: going
 * on means objects allocated by one heap get freed to another, which is the
 * exact defect this seam exists to remove.
 *
 * Default without any call: malloc/free, i.e. unchanged behaviour.
 */
/* Implementation; the public entry point is the V8AllocatorInstall LVO. */
BOOL V8AllocatorInstallImpl(V8AllocFunc allocFn, V8FreeFunc freeFn);

/* TRUE once a non-default allocator has been installed. */
BOOL V8AllocatorIsInstalledImpl(void);

/*
 * Owned address ranges: a pointer inside a registered range is freed by that
 * range's free function, whichever thread or process releases it. See
 * v8_allocator.cpp. Implementations of the V8OwnerRangeAdd/Remove LVOs.
 */
void V8OwnerRangesInitImpl(void);
BOOL V8OwnerRangeAddImpl(APTR base, IPTR size, V8FreeFunc freeFn);
BOOL V8OwnerRangeRemoveImpl(APTR base);

#ifdef __cplusplus
}
#endif

#endif /* V8_ALLOCATOR_H */
