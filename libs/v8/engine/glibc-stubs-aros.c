/*
 * AROS Stubs for glibc Functions Required by V8
 * 
 * This file provides minimal stub implementations for Linux/glibc functions
 * that V8 expects but are not available in AROS. These stubs allow V8 to
 * link and run on AROS, though some functionality may be limited.
 */

#include <exec/types.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/arossupport.h>
#include <proto/timer.h>
#include <devices/timer.h>

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <sys/types.h>
#include <limits.h>
#include <unistd.h>
#include <time.h>

/*
 * Global TimerBase for use by worker threads that don't have their own
 * posixc.library base.  Initialized once during v8.library init via
 * v8_init_timer().  Safe to call GetUpTime/GetSysTime from any task
 * context because those just read TimerBase fields under Disable().
 */
static struct Library *v8_TimerBase = NULL;
static struct MsgPort *v8_TimerPort = NULL;
static struct timerequest *v8_TimerIO = NULL;

/* Seconds between 1.1.1978 (AROS epoch) and 1.1.1970 (Unix epoch) */
#define AROS_UNIX_EPOCH_OFFSET 252460800

/* Define SEEK constants if not available */
#ifndef SEEK_SET
#define SEEK_SET 0
#endif
#ifndef SEEK_CUR
#define SEEK_CUR 1
#endif
#ifndef SEEK_END
#define SEEK_END 2
#endif

/* Global error variable for errno 
 * NOTE: This is not thread-safe. In a multi-threaded environment,
 * different threads share the same errno value. A proper implementation
 * would use thread-local storage if supported by AROS.
 */
static int _aros_errno = 0;

/*
 * v8_init_timer / v8_cleanup_timer — open/close timer.device once.
 * Call from v8.library's LibInit/LibExpunge.
 * The resulting TimerBase is safe to use from any AROS task.
 */
void v8_init_timer(void) {
    if (v8_TimerBase)
        return;
    v8_TimerPort = CreateMsgPort();
    if (!v8_TimerPort) {
        kprintf("[V8] v8_init_timer: CreateMsgPort failed\n");
        return;
    }
    /*
     * This port is only an OpenDevice anchor.  No timer request is ever sent
     * through it, so retaining a signal bit in the task which first opens
     * v8.library would both be needless and outlive that command.
     */
    FreeSignal(v8_TimerPort->mp_SigBit);
    v8_TimerPort->mp_SigBit = -1;
    v8_TimerPort->mp_Flags = PA_IGNORE;
    v8_TimerIO = (struct timerequest *)CreateIORequest(v8_TimerPort,
                                                       sizeof(struct timerequest));
    if (!v8_TimerIO) {
        kprintf("[V8] v8_init_timer: CreateIORequest failed\n");
        DeleteMsgPort(v8_TimerPort);
        v8_TimerPort = NULL;
        return;
    }
    if (OpenDevice("timer.device", UNIT_MICROHZ,
                   (struct IORequest *)v8_TimerIO, 0)) {
        kprintf("[V8] v8_init_timer: OpenDevice failed\n");
        DeleteIORequest((struct IORequest *)v8_TimerIO);
        DeleteMsgPort(v8_TimerPort);
        v8_TimerIO = NULL;
        v8_TimerPort = NULL;
        return;
    }
    v8_TimerBase = (struct Library *)v8_TimerIO->tr_node.io_Device;
    kprintf("[V8] v8_init_timer: TimerBase=%p\n", v8_TimerBase);
}

void v8_cleanup_timer(void) {
    if (v8_TimerIO) {
        CloseDevice((struct IORequest *)v8_TimerIO);
        DeleteIORequest((struct IORequest *)v8_TimerIO);
        v8_TimerIO = NULL;
    }
    if (v8_TimerPort) {
        DeleteMsgPort(v8_TimerPort);
        v8_TimerPort = NULL;
    }
    v8_TimerBase = NULL;
}

/*
 * clock_gettime — override posixc version so V8 worker threads
 * can call it without their own PosixCBase (per-task-lib issue).
 * Uses the global v8_TimerBase cached during library init.
 *
 * CRITICAL: Must NEVER return -1 for CLOCK_MONOTONIC or CLOCK_REALTIME.
 * V8's ClockNow() calls UNREACHABLE() on failure, which kills the thread.
 * If TimerBase is unavailable, return a monotonic counter as fallback.
 */
static volatile ULONG v8_fake_clock = 0;  /* fallback tick counter */

/*
 * Some V8 objects were compiled against Linux/glibc clock IDs even though this
 * shim is built with AROS headers.  AROS currently has CLOCK_MONOTONIC == 0;
 * Linux has CLOCK_REALTIME == 0 and CLOCK_MONOTONIC == 1.  V8's platform
 * workers use monotonic time for delayed-task scheduling, so accept both
 * values as monotonic rather than letting ClockNow() hit UNREACHABLE().
 */
#define V8_LINUX_CLOCK_REALTIME  0
#define V8_LINUX_CLOCK_MONOTONIC 1

/*
 * ABI CONTRACT: V8's AROS timespec occupies 16 bytes: a 32-bit time_t,
 * four bytes of padding, and a 64-bit tv_nsec.  Writing two 64-bit words
 * initializes that complete caller layout.  Writing through the native
 * C struct previously left the upper half of the first word as stack garbage:
 * V8's TimeTicks::Now() CHECK(kSecondsLimit > ts.tv_sec) then killed
 * the isolate (boot 170939, RandomNumberGenerator seeding).
 *
 * timeval is different: Chromium's V8 and ICU objects both compile it as
 * two 32-bit AROS fields (8 bytes).  A 16-byte glibc-style write overwrites
 * the caller's saved frame pointer with tv_usec.  Keep the two payload
 * layouts separate.
 */
typedef struct { long long tv_sec; long long tv_nsec; } glibc_timespec;
typedef struct { LONG tv_sec; LONG tv_usec; } aros_timeval_payload;

int clock_gettime(clockid_t clk_id, struct timespec *tp) {
    struct Library *TimerBase = v8_TimerBase;
    glibc_timespec *gtp = (glibc_timespec *)tp;

    if (!gtp) return -1;

    if (TimerBase) {
        switch (clk_id) {
            case CLOCK_MONOTONIC: {
                struct timeval tv;
                GetUpTime(&tv);
                gtp->tv_sec  = (long long)tv.tv_sec;
                gtp->tv_nsec = (long long)tv.tv_usec * 1000;
                return 0;
            }
#if CLOCK_MONOTONIC != V8_LINUX_CLOCK_MONOTONIC
            case V8_LINUX_CLOCK_MONOTONIC: {
                struct timeval tv;
                GetUpTime(&tv);
                gtp->tv_sec  = (long long)tv.tv_sec;
                gtp->tv_nsec = (long long)tv.tv_usec * 1000;
                return 0;
            }
#endif
            case CLOCK_REALTIME: {
                struct timeval tv;
                GetSysTime(&tv);
                gtp->tv_sec  = (long long)tv.tv_sec + AROS_UNIX_EPOCH_OFFSET;
                gtp->tv_nsec = (long long)tv.tv_usec * 1000;
                return 0;
            }
#if CLOCK_REALTIME != V8_LINUX_CLOCK_REALTIME && \
    CLOCK_MONOTONIC != V8_LINUX_CLOCK_REALTIME
            case V8_LINUX_CLOCK_REALTIME: {
                struct timeval tv;
                GetSysTime(&tv);
                gtp->tv_sec  = (long long)tv.tv_sec + AROS_UNIX_EPOCH_OFFSET;
                gtp->tv_nsec = (long long)tv.tv_usec * 1000;
                return 0;
            }
#endif
            default:
                /* CPU-time clocks (Linux ids 2/3) etc.: no AROS
                 * counterpart; return uptime rather than -1 so no
                 * ClockNow() path can reach UNREACHABLE(). */
            {
                struct timeval tv;
                GetUpTime(&tv);
                gtp->tv_sec  = (long long)tv.tv_sec;
                gtp->tv_nsec = (long long)tv.tv_usec * 1000;
                return 0;
            }
        }
    }

    /* Fallback: no TimerBase.  Return a crude monotonic counter so V8
     * doesn't hit UNREACHABLE().  Precision doesn't matter here — V8
     * just needs a non-zero, non-negative, increasing value. */
    kprintf("[V8] clock_gettime: no TimerBase, using fallback (clk_id=%d)\n",
            (int)clk_id);
    ULONG tick = ++v8_fake_clock;
    gtp->tv_sec  = tick / 100;
    gtp->tv_nsec = (long long)(tick % 100) * 10000000;  /* 10ms granularity */
    return 0;
}

/*
 * gettimeofday — same per-task-lib bypass for worker threads.
 */
int gettimeofday(struct timeval *tv, void *tz) {
    struct Library *TimerBase = v8_TimerBase;
    aros_timeval_payload *atv_payload = (aros_timeval_payload *)tv;
    (void)tz;
    if (!atv_payload) return -1;
    if (TimerBase) {
        struct timeval atv;   /* AROS layout for GetSysTime */
        GetSysTime(&atv);
        atv_payload->tv_sec  = (LONG)(atv.tv_sec + AROS_UNIX_EPOCH_OFFSET);
        atv_payload->tv_usec = (LONG)atv.tv_usec;
        return 0;
    }
    /* fallback */
    ULONG tick = ++v8_fake_clock;
    atv_payload->tv_sec  = (LONG)(tick / 100);
    atv_payload->tv_usec = (LONG)((tick % 100) * 10000);
    return 0;
}

/*
 * __errno_location - Return pointer to errno
 * glibc uses this to implement thread-local errno
 */
int* __errno_location(void) {
    return &_aros_errno;
}

/*
 * alarm - Set alarm signal (POSIX)
 * Not implemented in AROS, return 0 (no previous alarm)
 */
unsigned int alarm(unsigned int seconds) {
    /* Stub: alarm not supported */
    (void)seconds;
    return 0;
}

/*
 * backtrace - Get backtrace for debugging
 * Not implemented in AROS, return 0 (no frames)
 */
int backtrace(void** buffer, int size) {
    /* Stub: backtrace not supported */
    (void)buffer;
    (void)size;
    return 0;
}

/*
 * backtrace_symbols - Get symbol names for backtrace
 * Not implemented in AROS, return NULL
 */
char** backtrace_symbols(void* const* buffer, int size) {
    /* Stub: backtrace_symbols not supported */
    (void)buffer;
    (void)size;
    return NULL;
}

/*
 * dlsym - Dynamic symbol lookup
 * Not implemented in AROS, return NULL
 */
void* dlsym(void* handle, const char* symbol) {
    /* Stub: dlsym not supported */
    (void)handle;
    (void)symbol;
    _aros_errno = ENOSYS;
    return NULL;
}

/*
 * __dso_handle - DSO handle for shared libraries
 * Provide a weak symbol
 */
void* __dso_handle __attribute__((weak)) = NULL;

/*
 * __fprintf_chk - Fortified fprintf
 * If stream is NULL (weak stderr in library/thread context), route to AROS
 * debug output via kprintf so V8_Fatal messages are visible in the debug log.
 */
int __fprintf_chk(void* stream, int flag, const char* format, ...) {
    va_list args;
    int result;
    extern int vfprintf(void* stream, const char* format, va_list ap);
    extern int vsnprintf(char* str, size_t size, const char* format, va_list ap);
    
    (void)flag;
    va_start(args, format);
    if (stream) {
        result = vfprintf(stream, format, args);
    } else {
        /* Route to AROS debug output so V8_Fatal messages are visible */
        char buf[512];
        result = vsnprintf(buf, sizeof(buf), format, args);
        kprintf("%s", buf);
    }
    va_end(args);
    return result;
}

/*
 * __fread_chk - Fortified fread
 * Redirect to standard fread
 */
size_t __fread_chk(void* ptr, size_t ptrlen, size_t size, size_t n, void* stream) {
    extern size_t fread(void* ptr, size_t size, size_t nmemb, void* stream);
    (void)ptrlen;
    return fread(ptr, size, n, stream);
}

/*
 * getauxval - Get auxiliary vector entry
 * Not available in AROS, return 0
 */
unsigned long getauxval(unsigned long type) {
    /* Stub: getauxval not supported */
    (void)type;
    _aros_errno = ENOENT;
    return 0;
}

/*
 * getentropy - Get random entropy
 * WARNING: This implementation uses rand() which is NOT cryptographically secure.
 * V8 may use this for security-sensitive operations. A proper implementation
 * should use a hardware random source or AROS's secure random API if available.
 */
int getentropy(void* buffer, size_t length) {
    /* Stub: use simple pseudo-random (NOT SECURE) */
    unsigned char* buf = (unsigned char*)buffer;
    size_t i;
    
    for (i = 0; i < length; i++) {
        buf[i] = (unsigned char)(rand() & 0xFF);
    }
    
    return 0;
}

/*
 * getpagesize - Get system page size
 * Return a sensible default
 */
int getpagesize(void) {
    /* Return 4KB page size */
    return 4096;
}

/*
 * __gcov_dump - GCC coverage dump
 * Weak symbols for code coverage (not needed)
 */
void __gcov_dump(void) __attribute__((weak));
void __gcov_dump(void) {
    /* Stub: coverage not supported */
}

void __gcov_flush(void) __attribute__((weak));
void __gcov_flush(void) {
    /* Stub: coverage not supported */
}

/*
 * __libc_stack_end - End of stack
 * Provide a weak symbol
 */
void* __libc_stack_end __attribute__((weak)) = NULL;

/*
 * madvise - Memory advice
 * Stub implementation (no-op)
 */
int madvise(void* addr, size_t length, int advice) {
    /* Stub: madvise not supported */
    (void)addr;
    (void)length;
    (void)advice;
    return 0;
}

/*
 * malloc_usable_size - Get allocated size
 * Return the requested size (approximation)
 */
size_t malloc_usable_size(void* ptr) {
    /* Stub: return 0 if we can't determine */
    (void)ptr;
    return 0;
}

/*
 * __memcpy_chk - Fortified memcpy
 * Redirect to standard memcpy
 */
void* __memcpy_chk(void* dest, const void* src, size_t len, size_t destlen) {
    (void)destlen;
    return memcpy(dest, src, len);
}

/*
 * prctl - Process control
 * Stub implementation
 */
int prctl(int option, ...) {
    /* Stub: prctl not supported */
    (void)option;
    _aros_errno = ENOSYS;
    return -1;
}

/*
 * __printf_chk - Fortified printf
 */
int __printf_chk(int flag, const char* format, ...) {
    va_list args;
    int result;
    extern int vprintf(const char* format, va_list ap);
    
    (void)flag;
    va_start(args, format);
    result = vprintf(format, args);
    va_end(args);
    return result;
}

/*
 * pthread_condattr_setclock - Set clock for condition variable
 * Stub implementation
 */
int pthread_condattr_setclock(void* attr, int clock_id) {
    /* Stub: pthread_condattr_setclock not fully supported */
    (void)attr;
    (void)clock_id;
    return 0;
}

/*
 * pthread_cond_clockwait - Wait on condition variable with clock
 * Stub implementation - fallback to regular wait
 */
int pthread_cond_clockwait(void* cond, void* mutex, int clock_id, const void* abstime) {
    /* Stub: pthread_cond_clockwait not supported */
    (void)cond;
    (void)mutex;
    (void)clock_id;
    (void)abstime;
    _aros_errno = ENOSYS;
    return -1;
}

/*
 * __pthread_key_create - Thread-specific data key creation
 * Weak symbol
 */
int __pthread_key_create(void* key, void (*destructor)(void*)) __attribute__((weak));
int __pthread_key_create(void* key, void (*destructor)(void*)) {
    /* Stub: pthread_key_create not fully supported */
    (void)key;
    (void)destructor;
    return 0;
}

/*
 * pthread_sigmask - Set signal mask
 * Stub implementation
 */
int pthread_sigmask(int how, const void* set, void* oldset) {
    /* Stub: pthread_sigmask not supported */
    (void)how;
    (void)set;
    (void)oldset;
    return 0;
}

/*
 * sched_getcpu - Get CPU number
 * Return 0 (first CPU)
 */
int sched_getcpu(void) {
    /* Stub: return CPU 0 */
    return 0;
}

/*
 * setpriority - Set process priority
 * Stub implementation
 */
int setpriority(int which, int who, int prio) {
    /* Stub: setpriority not supported */
    (void)which;
    (void)who;
    (void)prio;
    return 0;
}

/*
 * sigaltstack - Set alternate signal stack
 * Stub implementation
 */
int sigaltstack(const void* ss, void* old_ss) {
    /* Stub: sigaltstack not supported */
    (void)ss;
    (void)old_ss;
    return 0;
}

/*
 * __snprintf_chk - Fortified snprintf
 */
int __snprintf_chk(char* s, size_t maxlen, int flag, size_t slen, const char* format, ...) {
    va_list args;
    int result;
    extern int vsnprintf(char* str, size_t size, const char* format, va_list ap);
    
    (void)flag;
    (void)slen;
    va_start(args, format);
    result = vsnprintf(s, maxlen, format, args);
    va_end(args);
    return result;
}

/*
 * __stack_chk_fail - Stack overflow detected
 */
void __stack_chk_fail(void) {
    /* Fatal error: stack corruption detected */
    abort();
}

/*
 * syscall - Generic system call
 * Not supported in AROS
 */
long syscall(long number, ...) {
    /* Stub: syscall not supported */
    (void)number;
    _aros_errno = ENOSYS;
    return -1;
}

/*
 * __tls_get_addr - Get TLS address
 *
 * AROS ELF loader convention (rom/dos/internalloadseg_elf.c,
 * R_X86_64_TLSGD/TLSLD): ti points at a loader-built GOT pair.
 * pair[0] is the module id (unused), pair[1] is the absolute address of
 * the single-instance TLS variable (TLSGD), or 0 (TLSLD, where the
 * compiler then adds the DTPOFF32 displacement that the loader patched
 * to an absolute value).
 *
 * AROS has no per-task TLS blocks yet, so every task shares the one
 * instance the loader allocated for the .tdata/.tbss sections. This
 * makes thread_local behave like a plain global — correct enough for
 * single-isolate bring-up, wrong for concurrent isolates per task.
 * Returning NULL here (the old stub) made every TLSGD access fault.
 */
void* __tls_get_addr(void* ti) {
    return ((void**)ti)[1];
}

/*
 * __vfprintf_chk - Fortified vfprintf
 * Route to AROS kprintf when stream is NULL so V8_Fatal messages are visible.
 */
int __vfprintf_chk(void* stream, int flag, const char* format, va_list ap) {
    extern int vfprintf(void* stream, const char* format, va_list ap);
    extern int vsnprintf(char* str, size_t size, const char* format, va_list ap);
    (void)flag;
    if (stream)
        return vfprintf(stream, format, ap);
    /* Route to AROS debug output */
    char buf[512];
    int result = vsnprintf(buf, sizeof(buf), format, ap);
    kprintf("%s", buf);
    return result;
}

/*
 * __vsnprintf_chk - Fortified vsnprintf  
 */
int __vsnprintf_chk(char* s, size_t maxlen, int flag, size_t slen, const char* format, va_list ap) {
    extern int vsnprintf(char* str, size_t size, const char* format, va_list ap);
    (void)flag;
    (void)slen;
    return vsnprintf(s, maxlen, format, ap);
}

/*
 * pkey_* - Memory protection keys (Linux 4.6+)
 * Weak symbols for optional features
 */
int pkey_alloc(unsigned int flags, unsigned int access_rights) __attribute__((weak));
int pkey_alloc(unsigned int flags, unsigned int access_rights) {
    (void)flags;
    (void)access_rights;
    _aros_errno = ENOSYS;
    return -1;
}

int pkey_free(int pkey) __attribute__((weak));
int pkey_free(int pkey) {
    (void)pkey;
    _aros_errno = ENOSYS;
    return -1;
}

int pkey_get(int pkey) __attribute__((weak));
int pkey_get(int pkey) {
    (void)pkey;
    _aros_errno = ENOSYS;
    return -1;
}

int pkey_set(int pkey, unsigned int rights) __attribute__((weak));
int pkey_set(int pkey, unsigned int rights) {
    (void)pkey;
    (void)rights;
    _aros_errno = ENOSYS;
    return -1;
}

int pkey_mprotect(void* addr, size_t len, int prot, int pkey) __attribute__((weak));
int pkey_mprotect(void* addr, size_t len, int prot, int pkey) {
    (void)addr;
    (void)len;
    (void)prot;
    (void)pkey;
    _aros_errno = ENOSYS;
    return -1;
}

/*
 * File functions with 64-bit support
 * Redirect to AROS equivalents where possible
 */
extern int ftruncate(int fd, long length);
int ftruncate64(int fd, long long length) {
    /* Check if value fits in long before truncation */
    if (length > LONG_MAX || length < LONG_MIN) {
        _aros_errno = EOVERFLOW;
        return -1;
    }
    return ftruncate(fd, (long)length);
}

/*
 * __fxstat - Versioned fstat (glibc internal)
 * Provide implementation using standard fstat
 */
extern int fstat(int fd, void* buf);
int __fxstat(int ver, int fd, void* buf) {
    /* Ignore version parameter, use standard fstat */
    (void)ver;
    return fstat(fd, buf);
}

/*
 * __fxstat64 - Versioned fstat (glibc internal)
 * Redirect to __fxstat for AROS
 */
int __fxstat64(int ver, int fd, void* buf) {
    /* Redirect to __fxstat */
    return __fxstat(ver, fd, buf);
}

/*
 * __xstat - Versioned stat (glibc internal)
 * Provide implementation using standard stat
 */
extern int stat(const char* path, void* buf);
int __xstat(int ver, const char* path, void* buf) {
    /* Ignore version parameter, use standard stat */
    (void)ver;
    return stat(path, buf);
}

/*
 * __xstat64 - Versioned stat (glibc internal)
 * Redirect to __xstat for AROS
 */
int __xstat64(int ver, const char* path, void* buf) {
    /* Redirect to __xstat */
    return __xstat(ver, path, buf);
}

/*
 * mmap tracking table — V8 calls munmap on sub-ranges of mmap'd regions
 * (e.g. trimming an 8GB reservation to find a 4GB-aligned cage).  We need
 * to know which addresses are OUR mmap return values vs. interior pointers
 * so munmap can free correctly or no-op on partial unmaps.
 */
/*
 * V8's reservation pattern is mmap(len + align) -> munmap(head-trim)
 * -> munmap(tail-trim) -> ... -> munmap(remainder).  AllocMem can't
 * free partially, so each entry tracks the LIVE subrange [live_start,
 * live_end); trims shrink it and the underlying block is freed only
 * once the live range is fully unmapped.  Matching munmap on address
 * alone freed the whole 512MB pointer cage on V8's first head-trim
 * (boot 173111) and recycled it for the next reservations —
 * use-after-free of the entire isolate heap.
 */
#define MMAP_TRACK_MAX 512
static struct {
    void *raw;          /* underlying AllocMem pointer (NULL = slot free) */
    size_t alloc_size;  /* AllocMem size */
    IPTR live_start;    /* current live subrange (starts at aligned) */
    IPTR live_end;
} mmap_track[MMAP_TRACK_MAX];
static int mmap_track_count = 0;   /* high-water mark of used slots */

/* mmap/munmap arrive from V8 worker tasks too - guard the table with a
 * CAS spinlock (usable before SysBase-dependent semaphores). */
static volatile int mmap_track_lock = 0;
static void mmap_lock(void) {
    while (__sync_lock_test_and_set(&mmap_track_lock, 1))
        __asm__ __volatile__("pause");
}
static void mmap_unlock(void) {
    __sync_lock_release(&mmap_track_lock);
}

/*
 * mmap - Memory mapping (Unix)
 * AROS doesn't have mmap. Use AllocMem with page alignment so V8's page
 * allocator gets properly aligned addresses (CommitPageSize check).
 */
void* mmap(void* addr, size_t length, int prot, int flags, int fd, long offset) {
    (void)addr;
    (void)prot;
    (void)fd;
    (void)offset;

    kprintf("[V8 mmap] request: addr=%p len=%lu prot=%d flags=0x%x\n",
            addr, (unsigned long)length, prot, flags);

    /* Allocate with extra space for page alignment + header to store original ptr */
    const size_t page_size = 4096;
    size_t alloc_size = length + page_size + sizeof(void*);
    void* raw = AllocMem(alloc_size, MEMF_PUBLIC | MEMF_CLEAR);
    if (!raw) {
        _aros_errno = ENOMEM;
        kprintf("[V8 mmap] FAILED length=%lu\n", (unsigned long)length);
        return (void*)-1;
    }
    /* Align to page boundary, leaving room for stored original pointer */
    IPTR raw_addr = (IPTR)raw + sizeof(void*);
    IPTR aligned = (raw_addr + page_size - 1) & ~(page_size - 1);
    /* Store original pointer just before the aligned address */
    ((void**)aligned)[-1] = raw;
    /* Store alloc_size at raw start for munmap */
    *(size_t*)raw = alloc_size;

    /* Track this allocation so munmap can validate */
    mmap_lock();
    {
        int slot = -1, j;
        for (j = 0; j < mmap_track_count; j++)
            if (!mmap_track[j].raw) { slot = j; break; }
        if (slot < 0 && mmap_track_count < MMAP_TRACK_MAX)
            slot = mmap_track_count++;
        if (slot >= 0) {
            mmap_track[slot].raw = raw;
            mmap_track[slot].alloc_size = alloc_size;
            mmap_track[slot].live_start = aligned;
            mmap_track[slot].live_end = aligned + length;
        } else {
            kprintf("[V8 mmap] WARNING: tracking table full, %lu bytes untracked\n",
                    (unsigned long)length);
        }
    }
    mmap_unlock();

    kprintf("[V8 mmap] len=%lu raw=%p aligned=%p\n",
            (unsigned long)length, raw, (void*)aligned);
    return (void*)aligned;
}

void* mmap64(void* addr, size_t length, int prot, int flags, int fd, long long offset) {
    kprintf("[V8 mmap64] addr=%p len=%lu prot=%d flags=0x%x fd=%d off=%lld\n",
            addr, (unsigned long)length, prot, flags, fd, offset);
    /* Check if offset fits in long before truncation */
    if (offset > LONG_MAX || offset < LONG_MIN) {
        _aros_errno = EOVERFLOW;
        return (void*)-1;
    }
    return mmap(addr, length, prot, flags, fd, (long)offset);
}

extern int mkstemp(char* template);
int mkstemp64(char* template) {
    /* Redirect to regular mkstemp */
    return mkstemp(template);
}

extern int open(const char* pathname, int flags, ...);
int open64(const char* pathname, int flags, ...) {
    /* Redirect to regular open */
    return open(pathname, flags);
}

/*
 * pread - Read from file at offset without changing position
 * Provide implementation using lseek + read
 */
extern off_t lseek(int fd, off_t offset, int whence);
extern ssize_t read(int fd, void* buf, size_t count);

ssize_t pread(int fd, void* buf, size_t count, long offset) {
    /* Save current position */
    off_t old_pos = lseek(fd, 0, SEEK_CUR);
    if (old_pos == (off_t)-1) {
        return -1;
    }
    
    /* Seek to offset */
    if (lseek(fd, offset, SEEK_SET) == (off_t)-1) {
        return -1;
    }
    
    /* Read data */
    ssize_t result = read(fd, buf, count);
    
    /* Restore position */
    lseek(fd, old_pos, SEEK_SET);
    
    return result;
}

ssize_t pread64(int fd, void* buf, size_t count, long long offset) {
    /* Check if offset fits in long before truncation */
    if (offset > LONG_MAX || offset < LONG_MIN) {
        _aros_errno = EOVERFLOW;
        return -1;
    }
    return pread(fd, buf, count, (long)offset);
}

extern void* tmpfile(void);
void* tmpfile64(void) {
    /* Redirect to regular tmpfile */
    return tmpfile();
}

/*
 * mremap - Remap memory (Linux-specific)
 * Stub implementation
 */
void* mremap(void* old_address, size_t old_size, size_t new_size, int flags, ...) {
    /* Stub: mremap not supported */
    (void)old_address;
    (void)old_size;
    (void)new_size;
    (void)flags;
    _aros_errno = ENOSYS;
    return (void*)-1;
}

/*
 * Resource limit functions
 */
struct rlimit64_stub {
    unsigned long long rlim_cur;
    unsigned long long rlim_max;
};

extern int getrlimit(int resource, void* rlim);
int getrlimit64(int resource, struct rlimit64_stub* rlim) {
    /* Stub: redirect to regular getrlimit or return defaults */
    (void)resource;
    if (rlim) {
        rlim->rlim_cur = 0x10000000ULL;  /* 256MB default */
        rlim->rlim_max = 0x10000000ULL;
    }
    return 0;
}

/*
 * getrusage - Get resource usage
 * Provide stub implementation
 */
struct rusage_stub {
    long ru_utime_sec;
    long ru_utime_usec;
    long ru_stime_sec;
    long ru_stime_usec;
    /* Add other fields as needed */
};

int getrusage(int who, void* usage) {
    /* Stub: return zeros for all usage stats */
    struct rusage_stub* ru = (struct rusage_stub*)usage;
    (void)who;
    
    if (ru) {
        memset(ru, 0, sizeof(struct rusage_stub));
    }
    return 0;
}

/*
 * Formatted I/O functions (__isoc99_*)
 * These are glibc-specific versions
 */
extern int vfscanf(void* stream, const char* format, va_list ap);
int __isoc99_fscanf(void* stream, const char* format, ...) {
    va_list args;
    int result;
    
    va_start(args, format);
    result = vfscanf(stream, format, args);
    va_end(args);
    return result;
}

extern int vsscanf(const char* str, const char* format, va_list ap);
int __isoc99_sscanf(const char* str, const char* format, ...) {
    va_list args;
    int result;
    
    va_start(args, format);
    result = vsscanf(str, format, args);
    va_end(args);
    return result;
}

/*
 * Standard streams
 * NOTE: These weak symbols remain NULL if not initialized. This could cause
 * segmentation faults if V8 tries to use stdin/stdout/stderr.
 * 
 * TODO: Initialize these to AROS stream equivalents, possibly using a
 * constructor function or by linking against AROS's standard I/O library.
 * The posixc library should provide these streams, so they may be resolved
 * at link time if we link against -lposixc.
 */
void* stdin __attribute__((weak)) = NULL;
void* stdout __attribute__((weak)) = NULL;
void* stderr __attribute__((weak)) = NULL;

/*
 * Memory management stubs
 */

/*
 * mprotect - Change memory protection
 * AROS uses AllocMem which returns fully accessible memory — no page
 * protection model.  V8 calls mprotect to "commit" pages from reservations,
 * but on AROS all AllocMem memory is already read-write.  No-op is correct.
 */
int mprotect(void* addr, size_t len, int prot) {
    kprintf("[V8 mprotect] addr=%p len=%lu prot=%d (no-op)\n",
            addr, (unsigned long)len, prot);
    return 0;
}

/*
 * v8::base::OS::SetDataReadOnly override via --wrap
 *
 * V8 calls SetDataReadOnly(&v8_flags, sizeof(v8_flags)) after freezing flags.
 * It CHECKs that the address is page-aligned before calling mprotect().
 * On AROS, the ELF loader uses AllocMem for sections — no page alignment
 * guarantee — so the CHECK always fires and V8_Fatal aborts.
 *
 * Since AROS doesn't support mprotect anyway, we skip the whole thing.
 * Linked with: -Wl,--wrap=_ZN2v84base2OS15SetDataReadOnlyEPvm
 */
void __wrap__ZN2v84base2OS15SetDataReadOnlyEPvm(void* address, unsigned long size) {
    (void)address;
    (void)size;
    /* No-op: AROS has no mprotect, skip page-alignment check */
}

/*
 * MemoryAllocator::InitializeOncePerProcess override via --wrap
 *
 * The original calls CommitPageSize() which goes through sysconf.
 * On AROS the static-local initialization in AllocatePageSize() may fail
 * because C++ guard variables in BSS may not work correctly after ELF
 * relocation.  We set commit_page_size_ and commit_page_size_bits_ directly.
 *
 * These are BSS symbols:
 *   _ZN2v88internal15MemoryAllocator17commit_page_size_E  (size_t)
 *   _ZN2v88internal15MemoryAllocator22commit_page_size_bits_E (size_t)
 */
extern unsigned long _ZN2v88internal15MemoryAllocator17commit_page_size_E;
extern unsigned long _ZN2v88internal15MemoryAllocator22commit_page_size_bits_E;

void __wrap__ZN2v88internal15MemoryAllocator24InitializeOncePerProcessEv(void) {
    _ZN2v88internal15MemoryAllocator17commit_page_size_E = 4096;
    _ZN2v88internal15MemoryAllocator22commit_page_size_bits_E = 12; /* log2(4096) */
    kprintf("[V8] MemoryAllocator::InitializeOncePerProcess: commit_page_size=4096\n");
}

/*
 * v8::base::OS::Abort override via --wrap
 *
 * V8 calls OS::Abort on fatal errors (CHECK failures, unreachable code).
 * Do not route this through stdc abort()/raise(): V8 worker threads are not
 * entered through stdc startup, so that path falls into __stdc_jmp2exit.
 * A direct trap lets the system capture the failing V8 task with a useful
 * context.
 */
void __wrap__ZN2v84base2OS5AbortEv(void) {
    kprintf("[V8] OS::Abort called - trapping for an alert\n");
    __builtin_trap();
    for(;;) {} /* Should never reach here */
}

/*
 * sysconf - Get system configuration values
 *
 * V8 calls sysconf(_SC_PAGESIZE) for page size and _SC_NPROCESSORS_ONLN for
 * thread count.  The monolith was compiled with Linux/glibc headers so it uses
 * LINUX _SC_ values, not AROS ones.
 *
 * Linux _SC_ values:  _SC_ARG_MAX=0, _SC_CLK_TCK=2, _SC_PAGESIZE=30,
 *   _SC_NPROCESSORS_CONF=83, _SC_NPROCESSORS_ONLN=84
 * AROS _SC_ values:   _SC_ARG_MAX=19, _SC_PAGESIZE=54
 */
long sysconf(int name) {
    switch (name) {
        /* Linux _SC_PAGESIZE / _SC_PAGE_SIZE */
        case 30:
        /* AROS _SC_PAGE_SIZE / _SC_PAGESIZE (in case anything uses these) */
        case 53:
        case 54:
            return 4096;
        /* Linux _SC_NPROCESSORS_ONLN */
        case 84:
            return 1; /* AROS: single CPU for now */
        /* Linux _SC_NPROCESSORS_CONF */
        case 83:
            return 1;
        /* Linux _SC_CLK_TCK */
        case 2:
            return 100; /* standard HZ */
        /* Linux _SC_ARG_MAX */
        case 0:
        /* AROS _SC_ARG_MAX */
        case 19:
            return 4096;
        default:
            kprintf("[V8 sysconf] unknown name=%d\n", name);
            return -1;
    }
}

/*
 * munmap - Unmap memory
 * Only free memory if addr matches one of our mmap return values.
 * V8 calls munmap on sub-ranges (partial unmaps) which we must ignore —
 * AROS AllocMem/FreeMem doesn't support partial frees.
 */
int munmap(void* addr, size_t length) {
    IPTR ua = (IPTR)addr, ue = (IPTR)addr + length;
    int i;
    if (!addr || addr == (void*)-1) return -1;

    kprintf("[V8 munmap] addr=%p len=%lu\n", addr, (unsigned long)length);

    mmap_lock();
    for (i = 0; i < mmap_track_count; i++) {
        if (!mmap_track[i].raw)
            continue;
        if (ua >= mmap_track[i].live_start && ua < mmap_track[i].live_end) {
            /* head trim / full free: shrink from the front */
            if (ua == mmap_track[i].live_start) {
                mmap_track[i].live_start = (ue < mmap_track[i].live_end)
                                               ? ue : mmap_track[i].live_end;
            }
            /* tail trim: shrink from the back */
            else if (ue >= mmap_track[i].live_end) {
                mmap_track[i].live_end = ua;
            }
            /* interior release: cannot split an AllocMem block - keep it */
            else {
                kprintf("[V8 munmap] interior release, keeping backing\n");
                mmap_unlock();
                return 0;
            }

            if (mmap_track[i].live_start >= mmap_track[i].live_end) {
                kprintf("[V8 munmap] live range empty, freeing raw=%p size=%lu\n",
                        mmap_track[i].raw, (unsigned long)mmap_track[i].alloc_size);
                FreeMem(mmap_track[i].raw, mmap_track[i].alloc_size);
                mmap_track[i].raw = NULL;
            } else {
                kprintf("[V8 munmap] trimmed, live=[%p..%p)\n",
                        (void*)mmap_track[i].live_start,
                        (void*)mmap_track[i].live_end);
            }
            mmap_unlock();
            return 0;
        }
    }
    mmap_unlock();

    /* Unknown address (tail slack beyond live range, or untracked).  No-op. */
    kprintf("[V8 munmap] no match, ignoring\n");
    return 0;
}
