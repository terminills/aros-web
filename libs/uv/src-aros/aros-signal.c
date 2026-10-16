/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    uv_signal handles for the AROS libuv port.

    Nothing on AROS raises a POSIX signal in another process (posixc kill()
    is not implemented) and Ctrl-C reaches a raw-mode console program as a
    byte, so a watcher never fires. Registering one must still succeed:
    Node aborts in SignalWrap when uv_signal_init fails, which every
    process.on('SIGINT') reached. Start and stop keep libuv's handle
    accounting and its argument checks.
*/
#include "uv.h"
#include "internal.h"

#include <signal.h>

int uv_signal_init(uv_loop_t* loop, uv_signal_t* handle) {
  uv__handle_init(loop, (uv_handle_t*) handle, UV_SIGNAL);
  handle->signum = 0;
  handle->caught_signals = 0;
  handle->dispatched_signals = 0;
  return 0;
}

static void uv__signal_stop(uv_signal_t* handle) {
  if (handle->signum == 0)
    return;
  handle->signum = 0;
  handle->flags &= ~(UV_SIGNAL_ONE_SHOT | UV_SIGNAL_ONE_SHOT_DISPATCHED);
  uv__handle_stop(handle);
}

static int uv__signal_start(uv_signal_t* handle, uv_signal_cb signal_cb,
                            int signum, int oneshot) {
  assert(!uv__is_closing(handle));

  if (signum <= 0 || signum >= NSIG || signum == SIGKILL || signum == SIGSTOP)
    return UV_EINVAL;

  if (signum == handle->signum) {
    handle->signal_cb = signal_cb;
    return 0;
  }

  uv__signal_stop(handle);

  handle->signum = signum;
  handle->signal_cb = signal_cb;
  if (oneshot)
    handle->flags |= UV_SIGNAL_ONE_SHOT;
  uv__handle_start(handle);
  return 0;
}

int uv_signal_start(uv_signal_t* handle, uv_signal_cb signal_cb, int signum) {
  return uv__signal_start(handle, signal_cb, signum, 0);
}

int uv_signal_start_oneshot(uv_signal_t* handle, uv_signal_cb signal_cb,
                            int signum) {
  return uv__signal_start(handle, signal_cb, signum, 1);
}

int uv_signal_stop(uv_signal_t* handle) {
  assert(!uv__is_closing(handle));
  uv__signal_stop(handle);
  return 0;
}

void uv__signal_close(uv_signal_t* handle) {
  uv__signal_stop(handle);
}

void uv__signal_cleanup(void) {
}

void uv__signal_global_once_init(void) {
}

void uv__signal_loop_cleanup(uv_loop_t* loop) {
  (void) loop;
}

int uv__signal_loop_fork(uv_loop_t* loop) {
  (void) loop;
  return 0;
}
