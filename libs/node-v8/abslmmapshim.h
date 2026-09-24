/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Force-included ahead of nodev8abslcompat.cc.

    That file has to see the Abseil declarations that only exist when
    ABSL_HAVE_MMAP is set - LowLevelAlloc, GraphCycles, PerThreadSem and
    CreateThreadIdentity - because it supplies those symbols for AROS, which
    has no mmap(2) and so gets them compiled out of libabseil.a while
    absl::Mutex still references them.

    It used to do that with a plain -DABSL_HAVE_MMAP on the command line.
    Abseil now refuses that outright:

        absl/base/config.h:413:2: error: ABSL_HAVE_MMAP cannot be directly set

    because config.h treats the macro as its own to compute, and #errors if it
    is already defined when it runs. The macro therefore has to be set *after*
    config.h has had its say, not before - so include config.h first and let it
    decide (it will not define the macro for AROS, which is correct: there
    genuinely is no mmap), then define it ourselves. config.h has an include
    guard, so every later include from the Abseil headers is a no-op and the
    #error never sees our definition.
*/

#ifndef NODE_V8_ABSL_MMAP_SHIM_H
#define NODE_V8_ABSL_MMAP_SHIM_H

#include "absl/base/config.h"

#ifndef ABSL_HAVE_MMAP
#define ABSL_HAVE_MMAP 1
#endif

#endif /* NODE_V8_ABSL_MMAP_SHIM_H */
