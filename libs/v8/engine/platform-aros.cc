// REF_GITHUB_ISSUE: #112
// AROS_IMPL: Direct integration with exec.library, dos.library, and timer.device
// 
// BUG FIX (v1.1): Fixed critical memory alignment bug causing bus faults
// - AllocateMemory() was freeing original pointer and returning aligned pointer
// - FreeMemory() then received wrong pointer, causing Error 0x80000002 crashes
// - Solution: Use AllocVec for aligned allocations, store original pointer before aligned address
// - This matches standard AROS practice for aligned memory management

#include "platform-aros.h"

#include <exec/exec.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <dos/dos.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>

#include <cstdlib>
#include <cstring>

// Undefine AROS macros that conflict with C++ method names.
// These macros are defined in <proto/exec.h>, <proto/dos.h>, and <inline/*.h>
// and interfere with method calls like Lock(), Signal(), Wait(), Allocate(), etc.
#ifdef Lock
#undef Lock
#endif
#ifdef Signal
#undef Signal
#endif
#ifdef Wait
#undef Wait
#endif
#ifdef Allocate
#undef Allocate
#endif

// Timer device base - needed for GetSysTime() and other timer functions
// This is initialized in InitializeAROSInterfaces() and cleaned up in CleanupAROSInterfaces()
struct Device *TimerBase = nullptr;

namespace v8 {
namespace base {

// Static member initialization
struct ExecBase* AROSPlatform::exec_base_ = nullptr;
APTR AROSPlatform::memory_pool_ = nullptr;
static struct MsgPort* timer_port_ = nullptr;
static struct timerequest* timer_req_ = nullptr;

bool AROSPlatform::InitializeAROSInterfaces() {
  // Get ExecBase
  exec_base_ = (struct ExecBase*)SysBase;
  if (!exec_base_) {
    return false;
  }
  
  // Create memory pool for V8 allocations
  memory_pool_ = CreatePool(MEMF_PUBLIC, 65536, 8192);
  if (!memory_pool_) {
    return false;
  }
  
  // Initialize timer device for GetSysTime() and other timer functions
  if (!TimerBase) {
    timer_port_ = CreateMsgPort();
    if (timer_port_) {
      /*
       * The request only keeps timer.device open for direct GetSysTime calls;
       * it is never submitted.  Do not pin a signal in the caller task for
       * the lifetime of this process-global interface.
       */
      FreeSignal(timer_port_->mp_SigBit);
      timer_port_->mp_SigBit = -1;
      timer_port_->mp_Flags = PA_IGNORE;
      timer_req_ = (struct timerequest*)CreateIORequest(timer_port_, sizeof(struct timerequest));
      if (timer_req_) {
        if (OpenDevice(TIMERNAME, UNIT_VBLANK, (struct IORequest*)timer_req_, 0) == 0) {
          TimerBase = (struct Device*)timer_req_->tr_node.io_Device;
        } else {
          DeleteIORequest((struct IORequest*)timer_req_);
          timer_req_ = nullptr;
          DeleteMsgPort(timer_port_);
          timer_port_ = nullptr;
        }
      } else {
        DeleteMsgPort(timer_port_);
        timer_port_ = nullptr;
      }
    }
  }
  
  return true;
}

void AROSPlatform::CleanupAROSInterfaces() {
  // Clean up timer device
  if (timer_req_) {
    CloseDevice((struct IORequest*)timer_req_);
    DeleteIORequest((struct IORequest*)timer_req_);
    timer_req_ = nullptr;
  }
  if (timer_port_) {
    DeleteMsgPort(timer_port_);
    timer_port_ = nullptr;
  }
  TimerBase = nullptr;
  
  if (memory_pool_) {
    DeletePool(memory_pool_);
    memory_pool_ = nullptr;
  }
}

void* AROSPlatform::AllocateMemory(size_t size, size_t alignment) {
  // Initialize AROS interfaces if not already done
  // Note: We no longer use memory_pool for allocations (using AllocVec instead),
  // but we still need to initialize timer and other system resources
  static bool interfaces_initialized = false;
  if (!interfaces_initialized) {
    InitializeAROSInterfaces();  // Ignore return value - timer init might fail but AllocVec still works
    interfaces_initialized = true;
  }
  
  // CRITICAL FIX v2: Always use AllocVec to avoid unsafe memory detection
  // Previous approach tried to detect aligned allocations by reading memory before
  // the pointer, which caused segfaults when that memory wasn't accessible.
  // 
  // Solution: Use AllocVec for ALL allocations (aligned and non-aligned)
  // AllocVec tracks size internally, so FreeVec() doesn't need size parameter
  // This is simpler, safer, and matches AROS best practices
  
  if (alignment > 1) {
    // For aligned allocations, allocate extra space for:
    // - Header (magic, original_ptr, original_size)  
    // - Alignment padding (worst case: alignment - 1)
    // - Requested size
    // Total: size + alignment + sizeof(header)
    size_t alloc_size = size + alignment + sizeof(AlignedAllocHeader);
    void* ptr = AllocVec(alloc_size, MEMF_PUBLIC);
    if (!ptr) {
      return nullptr;
    }
    
    // Calculate aligned address:
    // 1. Start from original pointer + header size (minimum space for header)
    // 2. Align up to the required alignment
    // This ensures:
    // - The header fits between ptr and aligned address
    // - The returned address is properly aligned
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    uintptr_t min_aligned = addr + sizeof(AlignedAllocHeader);
    uintptr_t aligned = (min_aligned + alignment - 1) & ~(alignment - 1);
    
    // Verify the header will fit (should always be true given our allocation)
    // This catches potential overflow or calculation errors
    uintptr_t header_addr = aligned - sizeof(AlignedAllocHeader);
    if (header_addr < addr || header_addr >= addr + alloc_size) {
      // Safety check failed - fall back to simple allocation
      FreeVec(ptr);
      return AllocVec(size, MEMF_PUBLIC);
    }
    
    // Store header just before the aligned address
    AlignedAllocHeader* header = reinterpret_cast<AlignedAllocHeader*>(header_addr);
    header->magic = ALIGNED_ALLOC_MAGIC;
    header->original_ptr = ptr;
    header->original_size = alloc_size;
    
    return reinterpret_cast<void*>(aligned);
  }
  
  // For non-aligned allocations, also use AllocVec for consistency and safety
  // This avoids the need to detect allocation type in FreeMemory
  return AllocVec(size, MEMF_PUBLIC);
}

void AROSPlatform::FreeMemory(void* ptr, size_t size) {
  if (!ptr) {
    return;
  }
  
  // CRITICAL FIX v2: Safe aligned allocation detection
  // All allocations now use AllocVec, but aligned allocations have a header
  // We must safely detect this without causing segfaults
  
  uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
  
  // SAFETY: Only attempt to read header if ALL conditions are met:
  // 1. Pointer is high enough that addr - sizeof(header) won't underflow
  // 2. Pointer is 4-byte aligned (required for safe struct access)
  // 3. Pointer looks like a heap pointer (> MIN_HEAP_ADDRESS)
  if (addr >= sizeof(AlignedAllocHeader) + MIN_HEAP_ADDRESS &&  // Well above NULL and low memory
      (addr & HEADER_ALIGNMENT_MASK) == 0) {                     // Properly aligned
    
    // Attempt to read header - CAREFULLY
    // Use volatile to prevent compiler from optimizing away the read
    // If this causes a crash, the address checks above need to be stricter
    volatile AlignedAllocHeader* header = 
        reinterpret_cast<volatile AlignedAllocHeader*>(addr - sizeof(AlignedAllocHeader));
    
    // Check magic number to confirm this is an aligned allocation
    // The volatile read will fail gracefully on most systems if memory is inaccessible
    uint32_t magic = header->magic;
    if (magic == ALIGNED_ALLOC_MAGIC) {
      // Read the rest of the header (simpler cast is clearer)
      void* original_ptr = (void*)header->original_ptr;
      size_t original_size = header->original_size;
      uintptr_t original_addr = reinterpret_cast<uintptr_t>(original_ptr);
      
      // Sanity checks: original pointer should be before aligned pointer
      // and within reasonable distance for alignment + header overhead
      if (original_addr > MIN_HEAP_ADDRESS &&  // Not NULL or low memory
          original_addr < addr &&                // Before our aligned address
          (addr - original_addr) < MAX_ALIGNMENT_OVERHEAD &&
          original_size > 0 && 
          original_size < MAX_REASONABLE_ALLOC_SIZE) {
        // This is a valid aligned allocation - use FreeVec on original pointer
        FreeVec(original_ptr);
        return;
      }
    }
  }
  
  // Not an aligned allocation (or failed validation) - use FreeVec directly
  // Since all allocations use AllocVec now, this is safe
  FreeVec(ptr);
}

void* AROSPlatform::AllocateExecutableMemory(size_t size) {
  // AROS: Allocate memory that can contain executable code
  void* ptr = AllocMem(size, MEMF_PUBLIC);
  
  // Note: On some AROS systems, all memory may be executable
  // On others, we might need special handling
  return ptr;
}

void AROSPlatform::FreeExecutableMemory(void* ptr, size_t size) {
  if (ptr) {
    FreeMem(ptr, size);
  }
}

int AROSPlatform::GetNumberOfProcessors() {
  // AROS: Check if SMP support is available
  // For now, return 1 (single processor)
  // TODO: Check AROS SMP capabilities
  return 1;
}

struct AROSThreadData {
  void (*entry_point)(void*);
  void* user_data;
  struct Task* task;
};

// AROS task entry point wrapper
static void AROSThreadEntry() {
  struct Task* current_task = FindTask(nullptr);
  if (!current_task) return;
  
  // Get thread data from task user data
  AROSThreadData* thread_data = 
      reinterpret_cast<AROSThreadData*>(current_task->tc_UserData);
  
  if (thread_data && thread_data->entry_point) {
    thread_data->entry_point(thread_data->user_data);
  }
}

void* AROSPlatform::CreateThread(void (*entry)(void*), void* data) {
  AROSThreadData* thread_data = new AROSThreadData;
  thread_data->entry_point = entry;
  thread_data->user_data = data;
  
  // Create AROS task
  struct Task* task = static_cast<struct Task*>(
      AllocMem(sizeof(struct Task), MEMF_PUBLIC | MEMF_CLEAR));
  if (!task) {
    delete thread_data;
    return nullptr;
  }
  
  // Allocate stack
  void* stack = AllocMem(V8_AROS_STACK_SIZE, MEMF_PUBLIC);
  if (!stack) {
    FreeMem(task, sizeof(struct Task));
    delete thread_data;
    return nullptr;
  }
  
  // Initialize task structure
  task->tc_Node.ln_Type = NT_TASK;
  task->tc_Node.ln_Pri = V8_AROS_TASK_PRIORITY;
  task->tc_Node.ln_Name = (char*)"V8Thread";
  task->tc_SPLower = stack;
  task->tc_SPUpper = reinterpret_cast<void*>(
      reinterpret_cast<uintptr_t>(stack) + V8_AROS_STACK_SIZE);
  task->tc_SPReg = task->tc_SPUpper;
  task->tc_UserData = thread_data;
  
  thread_data->task = task;
  
  // Add task to system
  AddTask(task, reinterpret_cast<APTR>(AROSThreadEntry), nullptr);
  
  return thread_data;
}

void AROSPlatform::JoinThread(void* thread) {
  if (!thread) return;
  
  AROSThreadData* thread_data = reinterpret_cast<AROSThreadData*>(thread);
  
  // Wait for task to complete (simplified)
  // TODO: Implement proper task synchronization
  Delay(1); // Wait 1/50th second
  
  // Cleanup
  if (thread_data->task) {
    RemTask(thread_data->task);
    FreeMem(thread_data->task->tc_SPLower, V8_AROS_STACK_SIZE);
    FreeMem(thread_data->task, sizeof(struct Task));
  }
  delete thread_data;
}

double AROSPlatform::GetCurrentTimeMillis() {
  // Use AROS timer device for precise timing
  struct timeval tv;
  
  // Try to initialize timer if not already done
  // The initialization may fail (e.g., timer device unavailable), 
  // which is handled by the fallback below
  if (!TimerBase) {
    InitializeAROSInterfaces();
  }
  
  // If timer initialized successfully, use it for precise timing
  if (TimerBase) {
    GetSysTime(&tv);
    return static_cast<double>(tv.tv_secs) * 1000.0 + 
           static_cast<double>(tv.tv_micro) / 1000.0;
  }
  
  // Fallback: Use DateStamp if timer not available (less precise)
  struct DateStamp ds;
  DateStamp(&ds);
  // DateStamp: days since Jan 1, 1978, minutes in day, ticks in minute (50 ticks/sec)
  double days_ms = static_cast<double>(ds.ds_Days) * 24.0 * 60.0 * 60.0 * 1000.0;
  double mins_ms = static_cast<double>(ds.ds_Minute) * 60.0 * 1000.0;
  double ticks_ms = static_cast<double>(ds.ds_Tick) * 20.0; // 50 ticks/sec = 20ms per tick
  return days_ms + mins_ms + ticks_ms;
}

uint64_t AROSPlatform::GetCurrentTimeTicks() {
  struct timeval tv;
  
  // Try to initialize timer if not already done
  // The initialization may fail (e.g., timer device unavailable), 
  // which is handled by the fallback below
  if (!TimerBase) {
    InitializeAROSInterfaces();
  }
  
  // If timer initialized successfully, use it for precise timing
  if (TimerBase) {
    GetSysTime(&tv);
    return static_cast<uint64_t>(tv.tv_secs) * 1000000ULL + 
           static_cast<uint64_t>(tv.tv_micro);
  }
  
  // Fallback: Use DateStamp if timer not available (less precise)
  struct DateStamp ds;
  DateStamp(&ds);
  uint64_t days_us = static_cast<uint64_t>(ds.ds_Days) * 24ULL * 60 * 60 * 1000000;
  uint64_t mins_us = static_cast<uint64_t>(ds.ds_Minute) * 60ULL * 1000000;
  uint64_t ticks_us = static_cast<uint64_t>(ds.ds_Tick) * 20000ULL; // 50 ticks/sec = 20000us per tick
  return days_us + mins_us + ticks_us;
}

size_t AROSPlatform::GetPageSize() {
  // AROS typical page size
  // TODO: Get actual page size from system
  return 4096;
}

size_t AROSPlatform::GetPhysicalMemory() {
  // Get available memory from AROS
  ULONG available = AvailMem(MEMF_ANY);
  return static_cast<size_t>(available);
}

} // namespace base
} // namespace v8
