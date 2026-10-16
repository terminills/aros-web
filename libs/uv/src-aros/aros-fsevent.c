/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    AROS uv_fs_event over DOS file notification.

    StartNotify() delivers its messages to a PA_SOFTINT port: PutMsg() runs
    on the file system's task and Cause()s the handle's software interrupt,
    which only uv_async_send()s an internal async handle.  The loop task then
    replies the messages and calls the handle's callback.  NRF_WAIT_REPLY
    lets a burst of writes collapse into one event.

    DOS notification does not say which entry of a directory changed, so a
    directory watch reports UV_RENAME with no filename (consumers rescan)
    and a file watch reports UV_CHANGE with the file's own name.
*/
#include "uv.h"
#include "internal.h"

#include <dos/dos.h>
#include <dos/notify.h>
#include <exec/interrupts.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include <stddef.h>
#include <string.h>

struct uv__aros_fsevent {
    struct MsgPort* port;
    struct Interrupt softint;
    struct NotifyRequest nr;
    uv_async_t async;
    uv_fs_event_t* handle;
    int isdir;
};

#define FSEVENT(h) ((struct uv__aros_fsevent*)(h)->u.reserved[0])

static AROS_INTH1(uv__aros_fsevent_arrived, struct uv__aros_fsevent*, ev)
{
    AROS_INTFUNC_INIT

    uv_async_send(&ev->async);
    return 0;

    AROS_INTFUNC_EXIT
}

static int uv__aros_fsevent_reply_all(struct uv__aros_fsevent* ev) {
    struct Message* msg;
    int n = 0;

    while ((msg = GetMsg(ev->port)) != NULL) {
        ReplyMsg(msg);
        n++;
    }
    return n;
}

static void uv__aros_fsevent_async_cb(uv_async_t* async) {
    struct uv__aros_fsevent* ev = (struct uv__aros_fsevent*)
        ((char*)async - offsetof(struct uv__aros_fsevent, async));
    uv_fs_event_t* handle = ev->handle;
    const char* name;

    if (uv__aros_fsevent_reply_all(ev) == 0 || handle == NULL ||
        !uv__is_active(handle))
        return;

    if (ev->isdir) {
        handle->cb(handle, NULL, UV_RENAME, 0);
    } else {
        name = FilePart((CONST_STRPTR)handle->path);
        handle->cb(handle, name, UV_CHANGE, 0);
    }
}

static void uv__aros_fsevent_free(uv_handle_t* async) {
    struct uv__aros_fsevent* ev = (struct uv__aros_fsevent*)
        ((char*)async - offsetof(struct uv__aros_fsevent, async));

    FreeMem(ev->port, sizeof(*ev->port));
    FreeMem(ev, sizeof(*ev));
}

int uv_fs_event_init(uv_loop_t* loop, uv_fs_event_t* handle) {
    uv__handle_init(loop, (uv_handle_t*)handle, UV_FS_EVENT);
    handle->path = NULL;
    handle->u.reserved[0] = NULL;
    return 0;
}

int uv_fs_event_start(uv_fs_event_t* handle, uv_fs_event_cb cb,
                      const char* path, unsigned int flags) {
    struct uv__aros_fsevent* ev;
    struct FileInfoBlock* fib;
    BPTR lock;
    int err;

    (void)flags;   /* UV_FS_EVENT_RECURSIVE is not supported by DOS notify */

    if (uv__is_active(handle))
        return UV_EINVAL;
    if (cb == NULL || path == NULL)
        return UV_EINVAL;

    lock = Lock((CONST_STRPTR)path, SHARED_LOCK);
    if (lock == BNULL)
        return IoErr() == ERROR_OBJECT_NOT_FOUND ? UV_ENOENT : UV_EACCES;

    ev = AllocMem(sizeof(*ev), MEMF_PUBLIC | MEMF_CLEAR);
    fib = AllocDosObject(DOS_FIB, NULL);
    if (ev != NULL)
        ev->port = CreateMsgPort();
    if (ev == NULL || ev->port == NULL || fib == NULL) {
        if (fib != NULL)
            FreeDosObject(DOS_FIB, fib);
        if (ev != NULL) {
            if (ev->port != NULL)
                DeleteMsgPort(ev->port);
            FreeMem(ev, sizeof(*ev));
        }
        UnLock(lock);
        return UV_ENOMEM;
    }
    /* The port interrupts instead of signalling; the loop may run on
       another task, so hold no signal of this one. */
    FreeSignal(ev->port->mp_SigBit);
    ev->port->mp_SigBit = -1;
    ev->softint.is_Node.ln_Type = NT_INTERRUPT;
    ev->softint.is_Data = ev;
    ev->softint.is_Code = (VOID_FUNC)uv__aros_fsevent_arrived;
    ev->port->mp_SoftInt = &ev->softint;
    ev->port->mp_Flags = PA_SOFTINT;
    ev->isdir = Examine(lock, fib) && fib->fib_DirEntryType > 0;
    FreeDosObject(DOS_FIB, fib);
    UnLock(lock);

    handle->path = uv__strdup(path);
    if (handle->path == NULL) {
        FreeMem(ev->port, sizeof(*ev->port));
        FreeMem(ev, sizeof(*ev));
        return UV_ENOMEM;
    }

    err = uv_async_init(handle->loop, &ev->async, uv__aros_fsevent_async_cb);
    if (err) {
        uv__free(handle->path);
        handle->path = NULL;
        FreeMem(ev->port, sizeof(*ev->port));
        FreeMem(ev, sizeof(*ev));
        return err;
    }
    uv_unref((uv_handle_t*)&ev->async);
    ev->async.flags |= UV_HANDLE_INTERNAL;

    ev->handle = handle;
    ev->nr.nr_Name = (STRPTR)handle->path;
    ev->nr.nr_Flags = NRF_SEND_MESSAGE | NRF_WAIT_REPLY;
    ev->nr.nr_stuff.nr_Msg.nr_Port = ev->port;

    if (!StartNotify(&ev->nr)) {
        uv_close((uv_handle_t*)&ev->async, uv__aros_fsevent_free);
        uv__free(handle->path);
        handle->path = NULL;
        return UV_ENOSYS;
    }

    handle->u.reserved[0] = ev;
    handle->cb = cb;
    uv__handle_start(handle);
    return 0;
}

int uv_fs_event_stop(uv_fs_event_t* handle) {
    struct uv__aros_fsevent* ev = FSEVENT(handle);

    if (!uv__is_active(handle))
        return 0;

    uv__handle_stop(handle);
    if (ev != NULL) {
        EndNotify(&ev->nr);
        uv__aros_fsevent_reply_all(ev);
        ev->handle = NULL;
        uv_close((uv_handle_t*)&ev->async, uv__aros_fsevent_free);
        handle->u.reserved[0] = NULL;
    }
    uv__free(handle->path);
    handle->path = NULL;
    return 0;
}

void uv__fs_event_close(uv_fs_event_t* handle) {
    uv_fs_event_stop(handle);
}
