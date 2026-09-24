/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Core-first link stubs for the AROS libuv port.

    The event loop (uv_loop + uv_timer) is functional, but it references entry
    points from libuv subsystems not yet ported: async, signal, process, fs
    events, and all networking (tcp/udp/stream/pipe/poll). These stubs let the
    library LINK for the uv_timer milestone. int-returning entry points report
    UV_ENOSYS; nothing here is reached by the timer path. Each stub is replaced
    by the real implementation as its phase lands (real uv_async next, then the
    networking phase over the bsdsocket socket layer).
*/
#include "uv.h"
#include "internal.h"

/* D(bug()) traces of the spawn stdio hand-off; set DEBUG 1 to see them on
   the hosted debug log. */
#define DEBUG 0
#include <aros/debug.h>

#include <dos/var.h>
#include <exec/libraries.h>
#include <exec/memory.h>
#include <proto/dos.h>
#include <proto/exec.h>
/*
 * <aros/kernel.h> (pulled in by proto/kernel.h) declares
 *     typedef enum { SCHED_RR = 1 } KRN_SchedType;
 * and POSIX <sched.h>, which libuv has already included here, defines SCHED_RR
 * as a MACRO. The enum member then expands to a numeric constant and the header
 * fails with "expected identifier before numeric constant". Same guard
 * Chromium's base/system/sys_info_posix.cc uses for the same reason.
 */
#pragma push_macro("SCHED_RR")
#undef SCHED_RR
#include <proto/kernel.h>
#pragma pop_macro("SCHED_RR")
#include <utility/hooks.h>

#include <netdb.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

/* UV_TTY_MODE_RAW_VT (= 3) is an enum member that libuv-aros.diff adds to
   uv.h, not a macro.  An "#ifndef UV_TTY_MODE_RAW_VT / #define ... RAW"
   fallback used to live here; it was always taken, so uv_tty_set_mode()
   below compared against 1 while node's TTYWrap::SetRawMode passes the enum
   value 3 -> Ink's stdin.setRawMode(true) failed with EINVAL.  If the enum
   is ever missing the build must fail, not silently degrade. */

/* SocketBase now lives in src-aros/aros-net.c (opened lazily on first socket
   use). Byte-order helpers (htons/htonl in inet.c) resolve against it there. */

/* NOTE: uv_async_* are now the REAL AROS exec-signal implementation in
   src-aros/aros-async.c -- no longer stubbed here. */

/* --- signal --- */
void uv__signal_close(uv_signal_t* handle) { (void)handle; }
void uv__signal_cleanup(void) { }
void uv__signal_global_once_init(void) { }
void uv__signal_loop_cleanup(uv_loop_t* loop) { (void)loop; }
int  uv__signal_loop_fork(uv_loop_t* loop) { (void)loop; return 0; }

/* --- process --- */
void uv__process_close(uv_process_t* handle) {
  uv__queue_remove(&handle->queue);
  uv__queue_init(&handle->queue);
  uv__handle_stop(handle);
}
int  uv__process_init(uv_loop_t* loop) { (void)loop; return 0; }
void uv__process_title_cleanup(void) { }

void uv__aros_register_dos_pipe(int fd);
void uv__aros_unregister_dos_pipe(int fd);

/*
 * AROS has real scheduled Processes and parent/child death notification but
 * no fork().  PosixC's vfork()+execvp() is the native adapter for exactly this
 * operation.  Child stdio uses DOS PIPE: endpoints so the executed program
 * writes through its ordinary PosixC descriptors; the parent endpoint is
 * opened as a normal libuv stream and driven by ACTION_WAIT_CHAR.
 */
/*
 * A DOS pipe is one-way.  Node asks for every 'pipe' stdio slot as
 * UV_READABLE_PIPE | UV_WRITABLE_PIPE (it expects a duplex socketpair) and
 * then only ever writes the child's stdin and reads its stdout/stderr, so a
 * request for both directions is resolved by the slot's conventional role:
 * slot 0 is the child's input, every other slot is child output.  This is
 * what lets child_process.execFile/spawn exist at all on AROS; before it,
 * every spawn (Claude Code's browser hand-off included) failed with ENOTSUP.
 */
static int uv__aros_pipe_child_reads(const uv_stdio_container_t* container,
                                     int index) {
  int dir = container->flags & (UV_READABLE_PIPE | UV_WRITABLE_PIPE);

  if (dir == (UV_READABLE_PIPE | UV_WRITABLE_PIPE))
    return index == 0;
  return dir == UV_READABLE_PIPE;
}

static int uv__aros_process_init_stdio(uv_stdio_container_t* container,
                                      int index,
                                      int fds[2]) {
  int mask;
  int fd;
  int pair[2];

  mask = UV_IGNORE | UV_CREATE_PIPE | UV_INHERIT_FD | UV_INHERIT_STREAM;

  switch (container->flags & mask) {
    case UV_IGNORE:
      return 0;

    case UV_CREATE_PIPE:
      if (container->data.stream == NULL ||
          container->data.stream->type != UV_NAMED_PIPE)
        return UV_EINVAL;
      if (pipe(pair) != 0)
        return UV__ERR(errno);

      if (uv__aros_pipe_child_reads(container, index)) {
        /* Child reads, parent writes. */
        fds[0] = pair[1];
        fds[1] = pair[0];
      } else {
        /* Child writes, parent reads (stdout/stderr and the default). */
        fds[0] = pair[0];
        fds[1] = pair[1];
      }
      return 0;

    case UV_INHERIT_FD:
    case UV_INHERIT_STREAM:
      if (container->flags & UV_INHERIT_FD)
        fd = container->data.fd;
      else
        fd = uv__stream_fd(container->data.stream);
      if (fd < 0)
        return UV_EINVAL;
      fds[1] = fd;
      return 0;

    default:
      return UV_EINVAL;
  }
}

static int uv__aros_process_open_stream(uv_stdio_container_t* container,
                                        int index,
                                        int fds[2]) {
  int flags;
  int err;

  D(bug("[uv_spawn] open_stream slot %d flags=0x%x fds=%d,%d stream=%p type=%d\n",
        index, container->flags, fds[0], fds[1], container->data.stream,
        container->data.stream ? container->data.stream->type : -1));
  if (!(container->flags & UV_CREATE_PIPE) || fds[0] < 0)
    return 0;

  if (fds[1] >= 0) {
    if (close(fds[1]) != 0)
      return UV__ERR(errno);
    fds[1] = -1;
  }

  /*
   * PIPE: does not implement ChangeMode(O_NONBLOCK).  aros-net.c enforces
   * nonblocking reads with ACTION_WAIT_CHAR instead.
   */
  uv__aros_register_dos_pipe(fds[0]);

  /* The parent end faces the other way from the child's (see
   * uv__aros_pipe_child_reads); the stream is open in that one direction. */
  if (uv__aros_pipe_child_reads(container, index))
    flags = UV_HANDLE_WRITABLE;
  else
    flags = UV_HANDLE_READABLE;

  err = uv__stream_open(container->data.stream, fds[0], flags);
  D(bug("[uv_spawn] open_stream slot %d: uv__stream_open(%d, 0x%x)=%d -> fd=%d flags=0x%x\n",
        index, fds[0], flags, err, container->data.stream->io_watcher.fd,
        container->data.stream->flags));
  if (err != 0)
    uv__aros_unregister_dos_pipe(fds[0]);
  return err;
}

void uv__aros_process_reap(uv_loop_t* loop) {
  struct uv__queue pending;
  struct uv__queue* q;
  struct uv__queue* next;
  uv_process_t* process;
  int status;
  pid_t pid;

  uv__queue_init(&pending);
  q = uv__queue_head(&loop->process_handles);
  while (q != &loop->process_handles) {
    next = uv__queue_next(q);
    process = uv__queue_data(q, uv_process_t, queue);

    do
      pid = waitpid(process->pid, &status, WNOHANG);
    while (pid < 0 && errno == EINTR);

    if (pid == process->pid) {
      process->status = status;
      uv__queue_remove(&process->queue);
      uv__queue_insert_tail(&pending, &process->queue);
    }
    q = next;
  }

  q = uv__queue_head(&pending);
  while (q != &pending) {
    next = uv__queue_next(q);
    process = uv__queue_data(q, uv_process_t, queue);
    uv__queue_remove(&process->queue);
    uv__queue_init(&process->queue);
    uv__handle_stop(process);
    if (process->exit_cb != NULL)
      process->exit_cb(process, WEXITSTATUS(process->status), 0);
    q = next;
  }
}

/* uv_setup_args: the proctitle .c variants are all excluded from the AROS
   build (no argv[]-rewrite process-title support), so provide the no-op form
   here -- the same behaviour as libuv's no-proctitle.c: just hand argv back.
   Node calls it once at startup; process-title get/set are not wired. */
static char uv_aros_process_title[256];

char** uv_setup_args(int argc, char** argv) {
  if (argc > 0 && argv != NULL && argv[0] != NULL) {
    strncpy(uv_aros_process_title, argv[0],
            sizeof(uv_aros_process_title) - 1);
    uv_aros_process_title[sizeof(uv_aros_process_title) - 1] = '\0';
  }
  return argv;
}

/*
 * libuv 1.51 APIs required by Node 22 while uv1.library is still based on
 * 1.48. Thread names are advisory on AROS, so accept valid names. AROS has no
 * per-thread rusage source yet; report that capability absence explicitly.
 */
int uv_thread_setname(const char* name) {
  return name == NULL ? UV_EINVAL : 0;
}

int uv_getrusage_thread(uv_rusage_t* rusage) {
  if (rusage == NULL)
    return UV_EINVAL;
  memset(rusage, 0, sizeof(*rusage));
  return UV_ENOSYS;
}

/* --- Node-facing system and accessor surface ---
   These implementations make capability absence explicit while preserving the
   full libuv ABI. Simple getters and memory/random services are functional;
   unavailable process, signal, TTY, and filesystem-watch services return
   UV_ENOSYS instead of pretending success. */

uint64_t uv_get_free_memory(void) {
  return (uint64_t)AvailMem(MEMF_ANY);
}

uint64_t uv_get_total_memory(void) {
  return (uint64_t)AvailMem(MEMF_ANY | MEMF_TOTAL);
}

uint64_t uv_get_constrained_memory(void) {
  return 0;
}

uint64_t uv_get_available_memory(void) {
  return uv_get_free_memory();
}

int uv_exepath(char* buffer, size_t* size) {
  char path[1024];
  size_t length;

  if (buffer == NULL || size == NULL || *size == 0)
    return UV_EINVAL;
  if (!GetProgramName(path, sizeof(path)))
    return UV_ENOENT;

  length = strlen(path);
  if (length >= *size) {
    *size = length + 1;
    return UV_ENOBUFS;
  }
  memcpy(buffer, path, length + 1);
  *size = length;
  return 0;
}

int uv_get_process_title(char* buffer, size_t size) {
  size_t length;
  if (buffer == NULL || size == 0)
    return UV_EINVAL;
  length = strlen(uv_aros_process_title);
  if (length >= size)
    return UV_ENOBUFS;
  memcpy(buffer, uv_aros_process_title, length + 1);
  return 0;
}

int uv_set_process_title(const char* title) {
  if (title == NULL)
    return UV_EINVAL;
  strncpy(uv_aros_process_title, title,
          sizeof(uv_aros_process_title) - 1);
  uv_aros_process_title[sizeof(uv_aros_process_title) - 1] = '\0';
  return 0;
}

int uv_resident_set_memory(size_t* rss) {
  if (rss == NULL)
    return UV_EINVAL;
  *rss = 0;
  return 0;
}

int uv_uptime(double* uptime) {
  if (uptime == NULL)
    return UV_EINVAL;
  *uptime = 0.0;
  return UV_ENOSYS;
}

void uv_loadavg(double average[3]) {
  if (average != NULL)
    average[0] = average[1] = average[2] = 0.0;
}

/*
 * Report the REAL core count, not 1.
 *
 * This is what feeds Node's os.cpus() and os.availableParallelism(), so every
 * JS-side parallelism decision in Node, Electron and VS Code was being made by
 * something told it had a uniprocessor. Chromium's C++ side already asks the
 * kernel directly (base/system/sys_info_posix.cc uses KrnGetCPUCount), so this
 * stub was the one place still claiming otherwise.
 *
 * kernel.resource is opened per call rather than cached: OpenResource() is a
 * cheap list lookup, uv_cpu_info() is not a hot path (callers query it once and
 * cache), and a static here would be one more piece of shared state on a
 * many-core system for no gain. If the resource is unavailable we fall back to
 * one CPU, which is the old behaviour.
 */
int uv_cpu_info(uv_cpu_info_t** cpu_infos, int* count) {
  uv_cpu_info_t* info;
  APTR KernelBase;
  int ncpu = 1;
  int i;

  if (cpu_infos == NULL || count == NULL)
    return UV_EINVAL;

  KernelBase = OpenResource("kernel.resource");
  if (KernelBase != NULL) {
    ncpu = (int)KrnGetCPUCount();
    if (ncpu < 1)
      ncpu = 1;
  }

  info = calloc(ncpu, sizeof(*info));
  if (info == NULL)
    return UV_ENOMEM;

  for (i = 0; i < ncpu; i++) {
    info[i].model = strdup("AROS CPU");
    if (info[i].model == NULL) {
      while (--i >= 0)
        free(info[i].model);
      free(info);
      return UV_ENOMEM;
    }
  }

  *cpu_infos = info;
  *count = ncpu;
  return 0;
}

int uv_interface_addresses(uv_interface_address_t** addresses, int* count) {
  if (addresses == NULL || count == NULL)
    return UV_EINVAL;
  *addresses = NULL;
  *count = 0;
  return 0;
}

void uv_free_interface_addresses(uv_interface_address_t* addresses, int count) {
  (void)count;
  free(addresses);
}

int uv_if_indextoiid(unsigned int ifindex, char* buffer, size_t* size) {
  (void)ifindex;
  (void)buffer;
  (void)size;
  return UV_ENOSYS;
}

int uv_random(uv_loop_t* loop, uv_random_t* req, void* buffer,
              size_t buffer_length, unsigned int flags, uv_random_cb callback) {
  if (buffer == NULL || flags != 0)
    return UV_EINVAL;
  arc4random_buf(buffer, buffer_length);
  if (req != NULL) {
    req->loop = loop;
    req->buf = buffer;
    req->buflen = buffer_length;
    req->status = 0;
    req->cb = callback;
  }
  if (callback != NULL)
    callback(req, 0, buffer, buffer_length);
  return 0;
}

int uv_getnameinfo(uv_loop_t* loop, uv_getnameinfo_t* req,
                   uv_getnameinfo_cb callback, const struct sockaddr* address,
                   int flags) {
  if (req == NULL || address == NULL)
    return UV_EINVAL;
  req->loop = loop;
  req->flags = flags;
  req->retcode = UV_EAI_FAIL;
  req->getnameinfo_cb = callback;
  if (callback != NULL)
    callback(req, UV_EAI_FAIL, NULL, NULL);
  return UV_EAI_FAIL;
}

int uv_fs_event_init(uv_loop_t* loop, uv_fs_event_t* handle) {
  (void)loop; (void)handle; return UV_ENOSYS;
}

int uv_fs_event_start(uv_fs_event_t* handle, uv_fs_event_cb callback,
                      const char* path, unsigned int flags) {
  (void)handle; (void)callback; (void)path; (void)flags; return UV_ENOSYS;
}

int uv_signal_init(uv_loop_t* loop, uv_signal_t* handle) {
  (void)loop; (void)handle; return UV_ENOSYS;
}

int uv_signal_start(uv_signal_t* handle, uv_signal_cb callback, int signum) {
  (void)handle; (void)callback; (void)signum; return UV_ENOSYS;
}

int uv_signal_stop(uv_signal_t* handle) {
  (void)handle; return UV_ENOSYS;
}

int uv_spawn(uv_loop_t* loop, uv_process_t* handle,
             const uv_process_options_t* options) {
  int pipes_storage[8][2];
  int (*pipes)[2] = pipes_storage;
  int stdio_count;
  volatile int child_error;
  pid_t pid;
  int err;
  int i;

  if (loop == NULL || handle == NULL || options == NULL)
    return UV_EINVAL;
  if (options->file == NULL || options->args == NULL)
    return UV_EINVAL;

  uv__handle_init(loop, (uv_handle_t*)handle, UV_PROCESS);
  uv__queue_init(&handle->queue);
  handle->status = 0;
  handle->exit_cb = options->exit_cb;
  handle->pid = 0;

  /*
   * UV_PROCESS_DETACHED asks for a child that outlives its parent in its own
   * process group.  An exec Process created by vfork()+exec has no process
   * group and is never torn down with its parent (waitpid() merely collects
   * its ETask), so the flag's contract already holds and is accepted; the
   * `open` package spawns browsers this way.  Credentials cannot change.
   */
  if (options->flags & (UV_PROCESS_SETUID | UV_PROCESS_SETGID))
    return UV_ENOTSUP;

  stdio_count = options->stdio_count < 3 ? 3 : options->stdio_count;
  if (stdio_count > (int)(sizeof(pipes_storage) / sizeof(pipes_storage[0]))) {
    pipes = uv__malloc((size_t)stdio_count * sizeof(*pipes));
    if (pipes == NULL)
      return UV_ENOMEM;
  }

  for (i = 0; i < stdio_count; i++)
    pipes[i][0] = pipes[i][1] = -1;

  for (i = 0; i < options->stdio_count; i++) {
    err = uv__aros_process_init_stdio(&options->stdio[i], i, pipes[i]);
    if (err != 0)
      goto fail;
  }

  D(bug("[uv_spawn] file=%s argv[1]=%s cwd=%s flags=0x%x stdio_count=%d\n",
        options->file, options->args[0] ? (options->args[1] ? options->args[1] : "") : "",
        options->cwd ? options->cwd : "(inherit)", (unsigned)options->flags,
        options->stdio_count));
  for (i = 0; i < stdio_count; i++)
    D(bug("[uv_spawn] pre-vfork slot %d: flags=0x%x parent=%d child=%d\n", i,
          i < options->stdio_count ? options->stdio[i].flags : 0,
          pipes[i][0], pipes[i][1]));

  child_error = 0;
  pid = vfork();
  if (pid < 0) {
    err = UV__ERR(errno);
    goto fail;
  }

  if (pid == 0) {
    for (i = 0; i < stdio_count; i++) {
      int source = pipes[i][1];
      int nullfd;

      if (source < 0 && i < 3) {
        nullfd = open("NIL:", i == 0 ? O_RDONLY : O_WRONLY);
        if (nullfd < 0) {
          child_error = errno;
          _exit(127);
        }
        source = nullfd;
      }

      if (source >= 0 && source != i && dup2(source, i) < 0) {
        child_error = errno;
        _exit(127);
      }
    }

    /*
     * The pretend-child runs against the launcher's *copy* of the parent's
     * descriptor table (posixc __init_fd -> __copy_fdarray under
     * VFORK_PARENT; each copied slot bumps the shared fcb's opencount and
     * adds the child table as a holder of the number in fd.library), so a
     * close() here only drops the child table's reference: the parent's own
     * slot for the same endpoint survives and is closed by the parent below.
     * (Before fd.library counted holders, this close() freed the system-wide
     * number under the parent: its later close() got EBADF, the PIPE: writer
     * handle was never Closed and the reader never saw EOF.)
     * Both created endpoints are dropped from the child table once dup2() has
     * retained the child endpoint through numbered stdio; the exec'd program
     * only inherits stdio anyway.  Inherited descriptors remain owned by
     * their caller.
     */
    for (i = 0; i < stdio_count; i++) {
      if (i < options->stdio_count &&
          (options->stdio[i].flags & UV_CREATE_PIPE)) {
        if (pipes[i][1] >= stdio_count)
          close(pipes[i][1]);
        if (pipes[i][0] >= stdio_count)
          close(pipes[i][0]);
      }
    }

    if (options->cwd != NULL && chdir(options->cwd) != 0) {
      child_error = errno;
      _exit(127);
    }

    /*
     * The caller's environment goes in explicitly.  Upstream swaps it into
     * `environ` around execvp(), but on AROS `environ` is a per-opener
     * stdcio emulation: posixc's exec reads the array registered on ITS
     * stdcio base, which for a pertaskbase uv1 is never the one this
     * file's `environ` was registered on (the loader task's), so a swapped
     * environ was simply not seen and every child inherited the parent's
     * environment.  execvpe()
     * copies envp into the child's local variables inside __exec_prepare,
     * which runs on the launcher before vfork() returns here, so node may
     * free options->env right after uv_spawn() as it does.
     */
    if (options->env != NULL)
      execvpe(options->file, options->args, options->env);
    else
      execvp(options->file, options->args);
    child_error = errno;
    _exit(127);
  }

  for (i = 0; i < stdio_count; i++)
    D(bug("[uv_spawn] post-vfork pid=%d child_error=%d slot %d: parent=%d child=%d\n",
          (int)pid, child_error, i, pipes[i][0], pipes[i][1]));

  if (child_error != 0) {
    int status;
    do
      err = waitpid(pid, &status, 0);
    while (err < 0 && errno == EINTR);
    err = UV__ERR(child_error);
  } else {
    err = 0;
    handle->pid = pid;
    uv__queue_insert_tail(&loop->process_handles, &handle->queue);
    uv__handle_start(handle);
  }

  /*
   * The parent's stream ends are opened whether or not the child got as far
   * as exec (upstream unix/process.c does the same): node's ChildProcess
   * treats ENOENT/EACCES/ENOTDIR as deliverable through the 'error' event and
   * still wraps every UV_CREATE_PIPE slot in a Socket that starts reading,
   * so a pipe handle left unopened here surfaced as an unhandled "read
   * ENOTCONN" that took the whole process down (spawn-cwd-test, a bare name
   * missing from PATH).  Opened, the pipe simply reports EOF: the child's
   * copies were closed by the pretend-child and pipes[i][1] goes below.
   */
  for (i = 0; i < options->stdio_count; i++) {
    /*
     * uv__aros_process_open_stream() closes the parent's own slot for the
     * child endpoint (pipes[i][1]).  That slot is distinct from the one the
     * pretend-child closed in its copied table, and it must go: PIPE: only
     * reports EOF once the last writer is gone, so a parent still holding
     * the child's write end never saw the child's stdout end (execFile()
     * then completed only through its own timeout, with empty output).
     */
    int open_err = uv__aros_process_open_stream(&options->stdio[i], i, pipes[i]);
    if (open_err != 0) {
      if (err == 0) {
        err = open_err;
        goto fail_active;
      }
      goto fail;
    }
    pipes[i][0] = -1; /* owned by the stream */
  }

  if (pipes != pipes_storage)
    uv__free(pipes);
  return err;

fail_active:
  uv__queue_remove(&handle->queue);
  uv__queue_init(&handle->queue);
  uv__handle_stop(handle);

fail:
  for (i = 0; i < stdio_count; i++) {
    if (i < options->stdio_count &&
        (options->stdio[i].flags & (UV_INHERIT_FD | UV_INHERIT_STREAM)))
      continue;
    if (pipes[i][0] >= 0)
      close(pipes[i][0]);
    if (pipes[i][1] >= 0)
      close(pipes[i][1]);
  }
  if (pipes != pipes_storage)
    uv__free(pipes);
  return err;
}

/*
 * process.env enumeration (RealEnvStore::Enumerate -> uv_os_environ).
 *
 * libuv's generic version walks `environ`.  In uv1.library that is ONE
 * global for every client: stdcio's environ emulation fills it once, at
 * library init, with a snapshot of the initialising task's DOS local
 * variables, allocated from that opener's stdc pool.  Every later client -
 * a C:Node beside ElectronShell, the VS Code extension host's spawns -
 * read that same array, and once the pool that owned it was gone they read
 * freed memory: strlen() faulted on 0x37f in uv__strdup() under
 * uv_os_environ() (2026-09-15, run 23).
 *
 * The environment on AROS is the calling process's local variable list, so
 * build the item list from ScanVars(GVF_LOCAL_ONLY) on every call: per
 * process, current (setenv() changes are visible, which the static snapshot
 * never showed) and without shared state.  Same source and scope as the
 * stdcio snapshot; global ENV: variables are deliberately not enumerated,
 * as there.  Items are laid out the way core.c and uv_os_free_environ()
 * expect: one uv__malloc()ed "name\0value" buffer per item, the array from
 * uv__calloc().
 */
struct uv__aros_env_scan {
  uv_env_item_t* items;
  int capacity;
  int count;
  int failed;
};

static LONG uv__aros_env_count(struct Hook* hook, APTR userdata,
                               struct ScanVarsMsg* msg) {
  struct uv__aros_env_scan* scan = userdata;

  (void)hook; (void)msg;
  scan->capacity++;
  return 0;
}

static LONG uv__aros_env_collect(struct Hook* hook, APTR userdata,
                                 struct ScanVarsMsg* msg) {
  struct uv__aros_env_scan* scan = userdata;
  size_t name_len;
  char* buf;

  (void)hook;
  if (scan->failed || scan->count >= scan->capacity)
    return 0;

  name_len = strlen((const char*)msg->sv_Name);
  buf = uv__malloc(name_len + 1 + msg->sv_VarLen + 1);
  if (buf == NULL) {
    scan->failed = 1;
    return 0;
  }

  memcpy(buf, msg->sv_Name, name_len);
  buf[name_len] = '\0';
  memcpy(buf + name_len + 1, msg->sv_Var, msg->sv_VarLen);
  buf[name_len + 1 + msg->sv_VarLen] = '\0';

  scan->items[scan->count].name = buf;
  scan->items[scan->count].value = buf + name_len + 1;
  scan->count++;
  return 0;
}

int uv_os_environ(uv_env_item_t** envitems, int* count) {
  struct uv__aros_env_scan scan;
  struct Hook hook;

  *envitems = NULL;
  *count = 0;

  memset(&scan, 0, sizeof(scan));
  memset(&hook, 0, sizeof(hook));

  hook.h_Entry = (HOOKFUNC)uv__aros_env_count;
  ScanVars(&hook, GVF_LOCAL_ONLY, &scan);

  /* uv__calloc(0, ...) may legitimately return NULL; keep the empty
     environment a success with a real (freeable) array. */
  scan.items = uv__calloc(scan.capacity > 0 ? scan.capacity : 1,
                          sizeof(*scan.items));
  if (scan.items == NULL)
    return UV_ENOMEM;

  hook.h_Entry = (HOOKFUNC)uv__aros_env_collect;
  ScanVars(&hook, GVF_LOCAL_ONLY, &scan);

  if (scan.failed) {
    uv_os_free_environ(scan.items, scan.count);
    return UV_ENOMEM;
  }

  *envitems = scan.items;
  *count = scan.count;
  return 0;
}

int uv_process_kill(uv_process_t* handle, int signum) {
  (void)handle; (void)signum; return UV_ENOSYS;
}

int uv_kill(int pid, int signum) {
  (void)pid; (void)signum; return UV_ENOSYS;
}

static int uv_aros_tty_restore_fd = -1;
static struct termios uv_aros_tty_restore_mode;

int uv_tty_init(uv_loop_t* loop, uv_tty_t* handle, uv_file fd, int readable) {
  int access;
  int flags;
  int rc;

  (void)readable;  /* Deprecated by libuv; derive access from the descriptor. */

  if (loop == NULL || handle == NULL || fd < 0)
    return UV_EINVAL;
  if (uv_guess_handle(fd) != UV_TTY)
    return UV_EINVAL;

  access = fcntl(fd, F_GETFL);
  if (access == -1)
    return UV__ERR(errno);

  flags = UV_HANDLE_BLOCKING_WRITES;
  switch (access & O_ACCMODE) {
    case O_RDONLY:
      flags |= UV_HANDLE_READABLE;
      break;
    case O_WRONLY:
      flags |= UV_HANDLE_WRITABLE;
      break;
    default:
      flags |= UV_HANDLE_READABLE | UV_HANDLE_WRITABLE;
      break;
  }

  uv__stream_init(loop, (uv_stream_t*)handle, UV_TTY);
  rc = uv__stream_open((uv_stream_t*)handle, fd, flags);
  if (rc != 0) {
    uv__queue_remove(&handle->handle_queue);
    return rc;
  }

  memset(&handle->orig_termios, 0, sizeof(handle->orig_termios));
  if (tcgetattr(fd, &handle->orig_termios) != 0) {
    uv__stream_close((uv_stream_t*)handle);
    uv__queue_remove(&handle->handle_queue);
    return UV__ERR(errno);
  }
  handle->mode = UV_TTY_MODE_NORMAL;
  return 0;
}

int uv_tty_set_mode(uv_tty_t* handle, uv_tty_mode_t mode) {
  struct termios next;
  int fd;

  if (handle == NULL)
    return UV_EINVAL;
  if (mode != UV_TTY_MODE_NORMAL &&
      mode != UV_TTY_MODE_RAW &&
      mode != UV_TTY_MODE_IO &&
      mode != UV_TTY_MODE_RAW_VT)
    return UV_EINVAL;
  if (handle->mode == (int)mode)
    return 0;

  fd = uv__stream_fd(handle);
  next = handle->orig_termios;
  if (mode != UV_TTY_MODE_NORMAL)
    next.c_lflag &= ~ICANON;

  if (tcsetattr(fd, TCSADRAIN, &next) != 0)
    return UV__ERR(errno);

  if (handle->mode == UV_TTY_MODE_NORMAL && mode != UV_TTY_MODE_NORMAL) {
    uv_aros_tty_restore_fd = fd;
    uv_aros_tty_restore_mode = handle->orig_termios;
  } else if (mode == UV_TTY_MODE_NORMAL &&
             uv_aros_tty_restore_fd == fd) {
    uv_aros_tty_restore_fd = -1;
  }

  handle->mode = mode;
  return 0;
}

int uv_tty_reset_mode(void) {
  if (uv_aros_tty_restore_fd >= 0) {
    int fd = uv_aros_tty_restore_fd;
    uv_aros_tty_restore_fd = -1;
    if (tcsetattr(fd, TCSANOW, &uv_aros_tty_restore_mode) != 0)
      return UV__ERR(errno);
  }
  return 0;
}

int uv_tty_get_winsize(uv_tty_t* handle, int* width, int* height) {
  struct winsize size;

  if (handle == NULL || width == NULL || height == NULL)
    return UV_EINVAL;
  if (ioctl(uv__stream_fd(handle), TIOCGWINSZ, &size) != 0)
    return UV__ERR(errno);

  *width = size.ws_col;
  *height = size.ws_row;
  return 0;
}

/* --- fs events --- */
void uv__fs_event_close(uv_fs_event_t* handle) { (void)handle; }

/* --- loop fork --- */
int  uv__io_fork(uv_loop_t* loop) { (void)loop; return 0; }

/* --- tty / handle classification ---
   KCON and CON are real interactive DOS handles.  PosixC's isatty() asks the
   handler through IsInteractive(), so retain redirected-file behaviour while
   allowing Node to construct its TTYWrap for an interactive console. */
uv_handle_type uv_guess_handle(uv_file file) {
  struct stat status;

  if (file < 0)
    return UV_UNKNOWN_HANDLE;

  if (isatty(file))
  {
#if defined(AROS_HAVE_TTY_BACKEND)
    /* A real terminal emulator is present (xterm port or Console NG), so the
       full termios/ioctl surface node's tty.WriteStream needs exists.  Report
       the handle honestly and let node drive it as a TTY. */
    return UV_TTY;
#else
    /* No terminal emulator yet.  AROS's console is interactive, so isatty()
       says yes, but it does not provide the termios/window-size surface that
       node's tty.WriteStream assumes: constructing process.stdout faults
       (null dereference right after the internal/tty builtin compiles, while
       fs.writeSync(1, ...) on the very same descriptor works).

       Reporting UV_FILE makes node build an fs.SyncWriteStream instead, which
       writes through fs.writeSync - the path that is known good here.  Output
       is then byte-oriented: the console renders single bytes, so non-ASCII
       should be clamped to Latin-1 by the caller rather than emitted as UTF-8
       multibyte, which the console would show as mojibake.

       Flip this by defining AROS_HAVE_TTY_BACKEND once a terminal emulator
       lands; nothing else in the port needs to change. */
    return UV_FILE;
#endif
  }

  if (fstat(file, &status) != 0)
    return UV_UNKNOWN_HANDLE;

  if (S_ISREG(status.st_mode) || S_ISDIR(status.st_mode))
    return UV_FILE;

  /* Every DOS handle that is not a file reaches posixc's fstat as a character
     device: a console, NIL:, and - since isatty() stopped calling a pipe a
     terminal (compiler/crt/posixc/isatty.c) - the PIPE: endpoints a spawned
     child inherits as its stdio.  Upstream's unix/tty.c answers UV_FILE for a
     character device too, and that is the right answer here: node then drives
     the descriptor with fs.writeSync / fs.ReadStream, which is what a DOS
     handle supports.  Falling through to UV_UNKNOWN_HANDLE instead made node
     install its black-hole stdout ("Provide a dummy black-hole output"), so a
     child's every write vanished silently. */
  if (S_ISCHR(status.st_mode))
    return UV_FILE;

  if (S_ISFIFO(status.st_mode))
    return UV_NAMED_PIPE;

  return UV_UNKNOWN_HANDLE;
}

/* --- stream / pipe / poll ---
   uv__read_start / uv__stream_close / uv__stream_destroy are now REAL
   (src/unix/stream.c) over the aros-net.c socket layer -- no longer stubbed.
   uv__pipe_close/uv__pipe_listen are now REAL (src/unix/pipe.c). Named-pipe
   bind/connect fail at runtime (bsdsocket has no AF_UNIX); the socketpair-backed
   anonymous path works via the aros-net.c AF_INET socketpair emulation. */
/* uv__poll_close + uv_poll_* are REAL (src/unix/poll.c), driven by the
   WaitSelect backend in src-aros/aros.c -- no longer stubbed here. */

/* --- tcp --- now REAL (src/unix/tcp.c) over the aros-net.c bsdsocket layer. */

/* --- udp --- now REAL (src/unix/udp.c) over the aros-net.c bsdsocket layer. */
