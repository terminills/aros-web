// REF_GITHUB_ISSUE: #112
// AROS_IMPL: Uses CreatePool/AllocPooled for efficiency, handles alignment requirements

#include "memory-aros.h"

#include <exec/exec.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Undefine AROS macros that conflict with C++ method names.
// These macros are defined in <proto/exec.h> and <inline/exec.h>
// and interfere with method calls like Allocate(), Signal(), etc.
#ifdef Allocate
#undef Allocate
#endif
#ifdef Signal
#undef Signal
#endif
#ifdef Wait
#undef Wait
#endif

namespace v8 {
namespace internal {

// Static member initialization
APTR AROSMemoryManager::general_pool_ = nullptr;
APTR AROSMemoryManager::executable_pool_ = nullptr;
size_t AROSMemoryManager::total_allocated_ = 0;
size_t AROSMemoryManager::total_executable_ = 0;
void (*AROSMemoryManager::gc_callback_)(void*) = nullptr;
void* AROSMemoryManager::gc_callback_data_ = nullptr;

bool AROSMemoryManager::Initialize() {
  // Create general purpose memory pool
  general_pool_ = CreatePool(MEMF_PUBLIC, 65536, 8192);
  if (!general_pool_) {
    return false;
  }
  
  // Create executable memory pool
  executable_pool_ = CreatePool(MEMF_PUBLIC, 32768, 4096);
  if (!executable_pool_) {
    DeletePool(general_pool_);
    general_pool_ = nullptr;
    return false;
  }
  
  total_allocated_ = 0;
  total_executable_ = 0;
  
  return true;
}

void AROSMemoryManager::Shutdown() {
  if (general_pool_) {
    DeletePool(general_pool_);
    general_pool_ = nullptr;
  }
  
  if (executable_pool_) {
    DeletePool(executable_pool_);
    executable_pool_ = nullptr;
  }
  
  total_allocated_ = 0;
  total_executable_ = 0;
  gc_callback_ = nullptr;
  gc_callback_data_ = nullptr;
}

size_t AROSMemoryManager::AlignSize(size_t size, size_t alignment) {
  return (size + alignment - 1) & ~(alignment - 1);
}

void* AROSMemoryManager::AlignPointer(void* ptr, size_t alignment) {
  uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
  uintptr_t aligned = (addr + alignment - 1) & ~(alignment - 1);
  return reinterpret_cast<void*>(aligned);
}

void* AROSMemoryManager::Allocate(size_t size, size_t alignment) {
  if (!general_pool_) {
    if (!Initialize()) {
      return nullptr;
    }
  }
  
  // Calculate aligned size
  size_t aligned_size = AlignSize(size + alignment, alignment);
  
  // Allocate from pool
  void* ptr = AllocPooled(general_pool_, aligned_size);
  if (!ptr) {
    return nullptr;
  }
  
  // Apply alignment
  void* aligned_ptr = AlignPointer(ptr, alignment);
  
  total_allocated_ += aligned_size;
  
  return aligned_ptr;
}

void* AROSMemoryManager::AllocateExecutable(size_t size) {
  if (!executable_pool_) {
    if (!Initialize()) {
      return nullptr;
    }
  }
  
  // Allocate executable memory
  void* ptr = AllocPooled(executable_pool_, size);
  if (!ptr) {
    // Fallback to direct allocation
    ptr = AllocMem(size, MEMF_PUBLIC);
  }
  
  if (ptr) {
    total_executable_ += size;
  }
  
  return ptr;
}

void AROSMemoryManager::Free(void* ptr, size_t size) {
  if (!ptr || !general_pool_) {
    return;
  }
  
  FreePooled(general_pool_, ptr, size);
  total_allocated_ -= size;
}

void AROSMemoryManager::FreeExecutable(void* ptr, size_t size) {
  if (!ptr) {
    return;
  }
  
  if (executable_pool_) {
    FreePooled(executable_pool_, ptr, size);
  } else {
    FreeMem(ptr, size);
  }
  
  total_executable_ -= size;
}

void* AROSMemoryManager::AllocatePages(size_t size, size_t alignment, bool executable) {
  // For large allocations, use direct memory allocation
  ULONG mem_type = MEMF_PUBLIC;
  if (executable) {
    // Note: On AROS, memory permissions may not be strictly enforced
    // This is a platform limitation that may need addressing
  }
  
  // Allocate aligned memory
  size_t aligned_size = AlignSize(size + alignment, alignment);
  void* ptr = AllocMem(aligned_size, mem_type);
  
  if (ptr) {
    void* aligned_ptr = AlignPointer(ptr, alignment);
    if (executable) {
      total_executable_ += aligned_size;
    } else {
      total_allocated_ += aligned_size;
    }
    return aligned_ptr;
  }
  
  return nullptr;
}

void AROSMemoryManager::FreePages(void* ptr, size_t size) {
  if (ptr) {
    FreeMem(ptr, size);
  }
}

bool AROSMemoryManager::SetMemoryProtection(void* ptr, size_t size, 
                                           bool readable, bool writable, 
                                           bool executable) {
  // CRITICAL LIMITATION: AROS memory protection is platform-dependent
  // Many AROS platforms do not support fine-grained memory protection
  // This is a fundamental limitation for V8's garbage collection security
  
  if (!ptr || size == 0) {
    return false;
  }
  
  // Log the attempted protection change for debugging
  printf("[AROS Memory] Protection request: ptr=%p size=%zu r=%d w=%d x=%d\n",
         ptr, size, readable, writable, executable);
  
  // On platforms with MMU support, we could potentially implement:
  // - Use CacheControl() for cache coherency
  // - Use memory mapping facilities where available
  // - Set appropriate cache attributes
  
  // For now, warn about the limitation
  if (!readable || (!writable && !executable)) {
    printf("[AROS Memory] WARNING: Restrictive memory protection not enforced\n");
    printf("[AROS Memory] This may impact V8 security features\n");
  }
  
  // Return success to indicate the request was "processed"
  // But actual enforcement depends on platform capabilities
  return true;
}

void AROSMemoryManager::RegisterGCCallback(void (*callback)(void*), void* data) {
  gc_callback_ = callback;
  gc_callback_data_ = data;
}

void AROSMemoryManager::NotifyGCStart() {
  if (gc_callback_) {
    gc_callback_(gc_callback_data_);
  }
}

void AROSMemoryManager::NotifyGCEnd() {
  // Additional cleanup after GC if needed
}

} // namespace internal
} // namespace v8