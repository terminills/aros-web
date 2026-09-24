// REF_GITHUB_ISSUE: #112
// AROS_IMPL: Uses memory pools for efficiency, integrates with garbage collector

#ifndef V8_MEMORY_AROS_H_
#define V8_MEMORY_AROS_H_

#include <exec/memory.h>
#include <exec/types.h>
#include <stddef.h>
#include <stdint.h>

// Undefine AROS exec.library macros that conflict with C++ method names.
// These macros are defined in <proto/exec.h> or <inline/exec.h> and interfere
// with the Allocate(), CreatePool(), and DeletePool() method declarations.
#ifdef Allocate
#undef Allocate
#endif
#ifdef CreatePool
#undef CreatePool
#endif
#ifdef DeletePool
#undef DeletePool
#endif

namespace v8 {
namespace internal {

// AROS-specific memory manager for V8
class AROSMemoryManager {
 public:
  // Initialize memory management system
  static bool Initialize();
  static void Shutdown();
  
  // Core allocation functions
  static void* Allocate(size_t size, size_t alignment = sizeof(void*));
  static void* AllocateExecutable(size_t size);
  static void Free(void* ptr, size_t size);
  static void FreeExecutable(void* ptr, size_t size);
  
  // Page-based allocation for large objects
  static void* AllocatePages(size_t size, size_t alignment, bool executable = false);
  static void FreePages(void* ptr, size_t size);
  
  // Memory protection (for garbage collection)
  static bool SetMemoryProtection(void* ptr, size_t size, bool readable, 
                                 bool writable, bool executable);
  
  // Memory statistics
  static size_t GetTotalAllocated() { return total_allocated_; }
  static size_t GetTotalExecutable() { return total_executable_; }
  
  // Garbage collection support
  static void RegisterGCCallback(void (*callback)(void*), void* data);
  static void NotifyGCStart();
  static void NotifyGCEnd();
  
 private:
  static APTR general_pool_;
  static APTR executable_pool_;
  static size_t total_allocated_;
  static size_t total_executable_;
  static void (*gc_callback_)(void*);
  static void* gc_callback_data_;
  
  // Alignment utilities
  static void* AlignPointer(void* ptr, size_t alignment);
  static size_t AlignSize(size_t size, size_t alignment);
};

// AROS-specific allocator class for V8 containers
template<typename T>
class AROSAllocator {
 public:
  typedef T value_type;
  typedef T* pointer;
  typedef const T* const_pointer;
  typedef T& reference;
  typedef const T& const_reference;
  typedef size_t size_type;
  typedef ptrdiff_t difference_type;
  
  template<typename U>
  struct rebind {
    typedef AROSAllocator<U> other;
  };
  
  AROSAllocator() {}
  template<typename U>
  AROSAllocator(const AROSAllocator<U>&) {}
  
  pointer allocate(size_type n) {
    return static_cast<pointer>(
        AROSMemoryManager::Allocate(n * sizeof(T), alignof(T)));
  }
  
  void deallocate(pointer p, size_type n) {
    AROSMemoryManager::Free(p, n * sizeof(T));
  }
  
  size_type max_size() const {
    return SIZE_MAX / sizeof(T);
  }
  
  template<typename U>
  bool operator==(const AROSAllocator<U>&) const { return true; }
  
  template<typename U>
  bool operator!=(const AROSAllocator<U>&) const { return false; }
};

} // namespace internal
} // namespace v8

#endif // V8_MEMORY_AROS_H_