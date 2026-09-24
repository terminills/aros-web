// REF_GITHUB_ISSUE: #112
// AROS_IMPL: Uses AROS tasks, semaphores, and message ports for synchronization

#ifndef V8_THREADING_AROS_H_
#define V8_THREADING_AROS_H_

#include <exec/types.h>
#include <exec/tasks.h>
#include <exec/semaphores.h>
#include <exec/ports.h>
#include <stdint.h>

// Undefine AROS exec.library macros that conflict with C++ method names.
// These macros are defined in <proto/exec.h> or <inline/exec.h> and interfere
// with the Signal() and Wait() method declarations in this file.
#ifdef Signal
#undef Signal
#endif
#ifdef Wait
#undef Wait
#endif

namespace v8 {
namespace base {

// Forward declarations
class Mutex;
class Semaphore;
class Thread;

// AROS-specific threading implementation
class AROSThread {
 public:
  typedef void* (*ThreadFunction)(void*);
  
  // Thread creation and management
  static AROSThread* Create(ThreadFunction function, void* data);
  bool Start();
  void Join();
  void Terminate();
  
  // Thread properties
  bool IsRunning() const { return running_; }
  struct Task* GetTask() const { return task_; }
  
  // Static helpers
  static AROSThread* Current();
  static void Sleep(uint32_t milliseconds);
  static void Yield();
  
 private:
  AROSThread(ThreadFunction function, void* data);
  ~AROSThread();
  
  ThreadFunction function_;
  void* user_data_;
  struct Task* task_;
  void* stack_;
  bool running_;
  bool joined_;
  bool should_exit_;
  struct SignalSemaphore exit_semaphore_;
  
  static void TaskEntry();
  static AROSThread* current_thread_;
};

// AROS mutex implementation using semaphores
class AROSMutex {
 public:
  AROSMutex();
  ~AROSMutex();
  
  void Lock();
  bool TryLock();
  void Unlock();
  void EnsureInit();
  
  // For V8 compatibility
  struct SignalSemaphore* GetSemaphore() { return &semaphore_; }
  
 private:
  struct SignalSemaphore semaphore_;
  bool initialized_;
};

// AROS semaphore wrapper with proper counting semaphore implementation
class AROSSemaphore {
 public:
  void EnsureInit();
  explicit AROSSemaphore(int initial_count);
  ~AROSSemaphore();
  
  void Wait();
  bool TryWait();
  void Signal();
  
 private:
  struct SignalSemaphore mutex_;
  int32_t count_;
  struct List waiting_tasks_;
  bool initialized_;
  
  struct WaitingTask {
    struct Node node;
    struct Task* task;
    struct SignalSemaphore wake_sem;
  };
};

// Condition variable implementation using message ports
class AROSConditionVariable {
 public:
  AROSConditionVariable();
  ~AROSConditionVariable();
  
  void Wait(AROSMutex* mutex);
  bool WaitFor(AROSMutex* mutex, uint32_t timeout_ms);
  void NotifyOne();
  void NotifyAll();
  
 private:
  struct MsgPort* port_;
  struct List waiting_tasks_;
  AROSMutex list_mutex_;
  bool initialized_;
  
  struct WaitingTask {
    struct Node node;
    struct Task* task;
    bool notified;
  };
};

// Thread-local storage for AROS using native task data
class AROSThreadLocal {
 public:
  AROSThreadLocal();
  ~AROSThreadLocal();
  
  void* Get();
  void Set(void* value);
  
 private:
  int slot_index_;  // AROS task data slot index
  
  static int next_slot_index_;
  static AROSMutex slot_mutex_;
};

// Atomic operations for AROS
class AROSAtomic {
 public:
  // Atomic integer operations
  static int32_t AtomicIncrement(volatile int32_t* value);
  static int32_t AtomicDecrement(volatile int32_t* value);
  static int32_t AtomicAdd(volatile int32_t* value, int32_t delta);
  static int32_t AtomicExchange(volatile int32_t* target, int32_t value);
  static int32_t AtomicCompareExchange(volatile int32_t* target, 
                                      int32_t expected, int32_t desired);
  
  // Atomic pointer operations
  static void* AtomicExchangePointer(void* volatile* target, void* value);
  static void* AtomicCompareExchangePointer(void* volatile* target,
                                           void* expected, void* desired);
  
  // Memory barriers
  static void MemoryBarrier();
  static void ReadBarrier();
  static void WriteBarrier();
  
 private:
  static AROSMutex atomic_mutex_; // Fallback for non-atomic platforms
};

} // namespace base
} // namespace v8

#endif // V8_THREADING_AROS_H_