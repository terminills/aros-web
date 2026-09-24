/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    uv1.library's per-task library base.

    uv1.library is a pertaskbase module: genmodule hands every opener task
    (every process) its own copy of this struct, and a pthread worker that
    never opened the library borrows its creator's copy through et_Parent
    (__aros_getoffsettable in uv1_start.c).  That copy is therefore the one
    place in the library that already means "this process", so per-process
    libuv state lives here and nowhere else.

    Upstream libuv keeps uv_default_loop() in two file-level statics in
    src/uv-common.c.  On AROS a file-level static is one variable for the
    whole guest, because node.library and every other embedder share the one
    uv1.library image: a Node process that spawned C:Node had both Node
    processes running ONE uv loop.  The child's uv_run() then ran the
    parent's check handle (Environment::CheckImmediate on the parent's
    isolate), and V8 aborted the child with "HandleScope::HandleScope
    Entering the V8 API without proper locking in place" during its own
    teardown.
*/
#ifndef UV1_AROS_INTBASE_H
#define UV1_AROS_INTBASE_H

#include <exec/libraries.h>
#include <uv.h>

struct UV1IntBase
{
    struct Library lib;

    /* uv_default_loop() for the opener task and its pthreads; lazily
       initialised, NULL until first use, NULL again after uv_loop_close()
       or after the last per-task close reclaimed its exec resources
       (aros-taskbase.c uv_aros_reclaim_default_loop). */
    uv_loop_t *default_loop_ptr;
    uv_loop_t  default_loop_struct;
};

#endif /* UV1_AROS_INTBASE_H */
