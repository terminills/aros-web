// REF_GITHUB_ISSUE: #112
// AROS_IMPL: Uses AROS exec.library for memory and task management

#ifndef V8_BASE_PLATFORM_PLATFORM_AROS_H_
#define V8_BASE_PLATFORM_PLATFORM_AROS_H_

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <exec/exec.h>
#include <dos/dos.h>

// V8 platform requirements
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace v8 {
namespace base {

// Header for aligned allocation tracking
struct AlignedAllocHeader {
  uint32_t magic;        // Magic number to identify aligned allocations
  void* original_ptr;    // Original allocation pointer
  size_t original_size;  // Original allocation size
};

// Magic number for aligned allocation detection (ASCII "ALIG")
#define ALIGNED_ALLOC_MAGIC 0x414C4947

// Maximum alignment padding + header (for sanity checks)
#define MAX_ALIGNMENT_OVERHEAD 512

// Maximum reasonable allocation size (100MB - for sanity checks)
#define MAX_REASONABLE_ALLOC_SIZE (100 * 1024 * 1024)

// Header alignment mask for 4-byte alignment check
// (addr & HEADER_ALIGNMENT_MASK) == 0 means addr is 4-byte aligned
#define HEADER_ALIGNMENT_MASK (sizeof(uint32_t) - 1)

// Minimum heap address for safety checks (64KB)
// Pointers below this are likely NULL or low memory (stack/static data)
#define MIN_HEAP_ADDRESS 0x10000

// AROS-specific platform definitions
class AROSPlatform {
 public:
  // Memory management
  static void* AllocateMemory(size_t size, size_t alignment = 0);
  static void FreeMemory(void* ptr, size_t size);
  static void* AllocateExecutableMemory(size_t size);
  static void FreeExecutableMemory(void* ptr, size_t size);
  
  // Threading
  static int GetNumberOfProcessors();
  static void* CreateThread(void (*entry)(void*), void* data);
  static void JoinThread(void* thread);
  
  // Time
  static double GetCurrentTimeMillis();
  static uint64_t GetCurrentTimeTicks();
  
  // System information
  static size_t GetPageSize();
  static size_t GetPhysicalMemory();
  
 private:
  static struct ExecBase* exec_base_;
  static APTR memory_pool_;
  
  // Initialize AROS system interfaces
  static bool InitializeAROSInterfaces();
  static void CleanupAROSInterfaces();
  
  friend class Platform;
};

// Platform abstraction macros for AROS
#define V8_OS_AROS 1

// Memory allocation macros
#define V8_AROS_MEMORY_TYPE MEMF_PUBLIC
#define V8_AROS_EXEC_MEMORY_TYPE (MEMF_PUBLIC | MEMF_EXECUTABLE)

// Threading macros  
#define V8_AROS_TASK_PRIORITY 0
#define V8_AROS_STACK_SIZE 8192

// Debug support
#ifdef DEBUG
#define V8_AROS_DEBUG_PRINT(msg) \
  do { \
    struct IntuitionBase* IntuitionBase = \
        (struct IntuitionBase*)OpenLibrary("intuition.library", 0); \
    if (IntuitionBase) { \
      /* Use AROS debug output */ \
      CloseLibrary((struct Library*)IntuitionBase); \
    } \
  } while(0)
#else
#define V8_AROS_DEBUG_PRINT(msg) do {} while(0)
#endif

} // namespace base
} // namespace v8

#endif // V8_BASE_PLATFORM_PLATFORM_AROS_H_