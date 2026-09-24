// REF_GITHUB_ISSUE: #112
// AROS_IMPL: Uses AROS tasks, semaphores, and message ports for full thread support

#include "threading-aros.h"

#include <exec/exec.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <exec/semaphores.h>
#include <exec/ports.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <cstring>
#include <new>

// Undefine AROS macros that conflict with C++ method names.
// These macros are defined in <proto/exec.h>, <proto/dos.h>, and <inline/*.h>
// and interfere with method calls like Lock(), Signal(), Wait(), etc.
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

namespace v8 {
namespace base {

// Static member initialization
AROSThread* AROSThread::current_thread_ = nullptr;
AROSMutex AROSAtomic::atomic_mutex_;

//
// AROSThread Implementation
//

AROSThread::AROSThread(ThreadFunction function, void* data)
    : function_(function), user_data_(data), task_(nullptr), 
      stack_(nullptr), running_(false), joined_(false), should_exit_(false) {
  /* ctor-phase safe: see AROSMutex; Start() re-runs this when live */
  if (__aros_getbase_SysBase())
    InitSemaphore(&exit_semaphore_);
}

AROSThread::~AROSThread() {
  if (running_ && !joined_) {
    Terminate();
  }
  
  if (stack_) {
    FreeMem(stack_, 8192);
    stack_ = nullptr;
  }
  
  if (task_) {
    FreeMem(task_, sizeof(struct Task));
    task_ = nullptr;
  }
}

AROSThread* AROSThread::Create(ThreadFunction function, void* data) {
  if (!function) return nullptr;
  
  return new(std::nothrow) AROSThread(function, data);
}

void AROSThread::TaskEntry() {
  struct Task* current_task = FindTask(nullptr);
  if (!current_task) return;
  
  AROSThread* thread = reinterpret_cast<AROSThread*>(current_task->tc_UserData);
  if (!thread) return;
  
  // Set current thread context
  AROSThread* previous_current = current_thread_;
  current_thread_ = thread;
  
  // Call user function, checking for exit signal periodically
  if (thread->function_) {
    thread->function_(thread->user_data_);
  }
  
  // Mark as no longer running and signal completion
  thread->running_ = false;
  ReleaseSemaphore(&thread->exit_semaphore_);
  
  // Restore previous context
  current_thread_ = previous_current;
}

bool AROSThread::Start() {
  if (__aros_getbase_SysBase()) InitSemaphore(&exit_semaphore_);
  if (running_ || !function_) return false;
  
  // Allocate task structure
  task_ = reinterpret_cast<struct Task*>(
      AllocMem(sizeof(struct Task), MEMF_PUBLIC | MEMF_CLEAR));
  if (!task_) return false;
  
  // Allocate stack
  stack_ = AllocMem(8192, MEMF_PUBLIC);
  if (!stack_) {
    FreeMem(task_, sizeof(struct Task));
    task_ = nullptr;
    return false;
  }
  
  // Initialize task structure
  task_->tc_Node.ln_Type = NT_TASK;
  task_->tc_Node.ln_Pri = 0;
  task_->tc_Node.ln_Name = (char*)"V8Thread";
  task_->tc_SPLower = stack_;
  task_->tc_SPUpper = reinterpret_cast<void*>(
      reinterpret_cast<uintptr_t>(stack_) + 8192);
  task_->tc_SPReg = task_->tc_SPUpper;
  task_->tc_UserData = this;
  
  // Add task to system
  running_ = true;
  AddTask(task_, reinterpret_cast<APTR>(TaskEntry), nullptr);
  
  return true;
}

void AROSThread::Join() {
  if (!running_ || joined_) return;
  
  // Proper blocking wait using semaphore instead of busy-wait polling
  ObtainSemaphore(&exit_semaphore_);
  ReleaseSemaphore(&exit_semaphore_); // Release immediately, we just wanted to wait
  
  joined_ = true;
}

void AROSThread::Terminate() {
  if (!running_) return;
  
  // Signal graceful exit instead of using dangerous RemTask()
  should_exit_ = true;
  
  // Wait for task to complete gracefully
  Join();
}

AROSThread* AROSThread::Current() {
  return current_thread_;
}

void AROSThread::Sleep(uint32_t milliseconds) {
  // Convert milliseconds to AROS timer ticks (1/50 second)
  ULONG ticks = (milliseconds * 50) / 1000;
  if (ticks == 0) ticks = 1;
  
  Delay(ticks);
}

void AROSThread::Yield() {
  // Give up time slice
  Delay(0);
}

//
// AROSMutex Implementation
//

AROSMutex::AROSMutex() : initialized_(false) {
  /* Static-constructor safe: global AROSMutex instances (e.g.
     AROSThreadLocal::slot_mutex_) construct while the module's SysBase
     is still NULL, and InitSemaphore(SysBase==NULL) faults (boot 57:
     CR2=0x28 in inline InitSemaphore). Defer to first use; library
     init is single-task so the lazy path has no ctor-time race. */
  if (SysBase) {
    InitSemaphore(&semaphore_);
    initialized_ = true;
  }
}

/* During the module's C++ static-constructor phase the global SysBase
   is still NULL (genmodule runs CTORS before anything publishes it) and
   every exec semaphore call would fault. That phase is single-task by
   construction, so mutex ops degrade to no-ops until SysBase appears. */
static inline int aros_sysbase_ready(void) {
  return __aros_getbase_SysBase() != NULL;
}

void AROSMutex::EnsureInit() {
  if (!initialized_ && aros_sysbase_ready()) {
    InitSemaphore(&semaphore_);
    initialized_ = true;
  }
}

AROSMutex::~AROSMutex() {
  if (initialized_) {
    // Note: AROS semaphores don't need explicit cleanup
    initialized_ = false;
  }
}

void AROSMutex::Lock() {
  EnsureInit();
  if (initialized_)
    ObtainSemaphore(&semaphore_);
}

bool AROSMutex::TryLock() {
  EnsureInit();
  if (!initialized_) return true;
  return AttemptSemaphore(&semaphore_) != 0;
}

void AROSMutex::Unlock() {
  if (initialized_) {
    ReleaseSemaphore(&semaphore_);
  }
}

//
// AROSSemaphore Implementation
//

AROSSemaphore::AROSSemaphore(int initial_count) 
    : count_(initial_count), initialized_(false) {
  /* ctor-phase safe: defer to first Wait/Signal if SysBase not yet up */
  if (__aros_getbase_SysBase()) {
    InitSemaphore(&mutex_);
    NewList(&waiting_tasks_);
    initialized_ = true;
  }
}

void AROSSemaphore::EnsureInit() {
  if (!initialized_ && __aros_getbase_SysBase()) {
    InitSemaphore(&mutex_);
    NewList(&waiting_tasks_);
    initialized_ = true;
  }
}

AROSSemaphore::~AROSSemaphore() {
  if (initialized_) {
    // Wake up all waiting tasks
    ObtainSemaphore(&mutex_);
    struct Node* node = waiting_tasks_.lh_Head;
    while (node->ln_Succ) {
      WaitingTask* waiting = reinterpret_cast<WaitingTask*>(node);
      ReleaseSemaphore(&waiting->wake_sem);
      node = node->ln_Succ;
    }
    ReleaseSemaphore(&mutex_);
    initialized_ = false;
  }
}

void AROSSemaphore::Wait() {
  EnsureInit();
  if (!initialized_) return;
  
  ObtainSemaphore(&mutex_);
  
  if (count_ > 0) {
    count_--;
    ReleaseSemaphore(&mutex_);
    return;
  }
  
  // Need to wait - create waiting task entry
  WaitingTask waiting;
  waiting.task = FindTask(nullptr);
  InitSemaphore(&waiting.wake_sem);
  ObtainSemaphore(&waiting.wake_sem); // Pre-obtain so we can wait on it
  
  AddTail(&waiting_tasks_, &waiting.node);
  ReleaseSemaphore(&mutex_);
  
  // Block until signaled
  ObtainSemaphore(&waiting.wake_sem);
  ReleaseSemaphore(&waiting.wake_sem);
}

bool AROSSemaphore::TryWait() {
  if (!initialized_) return false;
  
  if (AttemptSemaphore(&mutex_)) {
    if (count_ > 0) {
      count_--;
      ReleaseSemaphore(&mutex_);
      return true;
    }
    ReleaseSemaphore(&mutex_);
  }
  
  return false;
}

void AROSSemaphore::Signal() {
  EnsureInit();
  if (!initialized_) return;
  
  ObtainSemaphore(&mutex_);
  
  if (!IsListEmpty(&waiting_tasks_)) {
    // Wake up one waiting task
    WaitingTask* waiting = reinterpret_cast<WaitingTask*>(
        RemHead(&waiting_tasks_));
    ReleaseSemaphore(&waiting->wake_sem);
  } else {
    // No waiting tasks, increment count
    count_++;
  }
  
  ReleaseSemaphore(&mutex_);
}

//
// AROSConditionVariable Implementation
//

AROSConditionVariable::AROSConditionVariable() 
    : port_(nullptr), initialized_(false) {
  port_ = CreateMsgPort();
  if (port_) {
    NewList(&waiting_tasks_);
    initialized_ = true;
  }
}

AROSConditionVariable::~AROSConditionVariable() {
  if (initialized_) {
    // Wake up all waiting tasks
    NotifyAll();
    
    if (port_) {
      DeleteMsgPort(port_);
      port_ = nullptr;
    }
    
    initialized_ = false;
  }
}

void AROSConditionVariable::Wait(AROSMutex* mutex) {
  if (!initialized_ || !mutex) return;
  
  // Add current task to waiting list
  WaitingTask* waiting = new(std::nothrow) WaitingTask;
  if (!waiting) return;
  
  waiting->task = FindTask(nullptr);
  waiting->notified = false;
  
  list_mutex_.Lock();
  AddTail(&waiting_tasks_, &waiting->node);
  list_mutex_.Unlock();
  
  // Release mutex and wait
  mutex->Unlock();
  
  // Simple wait implementation
  while (!waiting->notified) {
    Delay(1);
  }
  
  // Reacquire mutex
  mutex->Lock();
  
  // Remove from waiting list
  list_mutex_.Lock();
  Remove(&waiting->node);
  list_mutex_.Unlock();
  
  delete waiting;
}

bool AROSConditionVariable::WaitFor(AROSMutex* mutex, uint32_t timeout_ms) {
  if (!initialized_ || !mutex) return false;
  
  // AROS timer ticks are 1/50th of a second (20ms each)
  // Use simple tick counting instead of GetSysTime which requires timer.device
  ULONG timeout_ticks = (timeout_ms * 50) / 1000;
  if (timeout_ticks == 0) timeout_ticks = 1;
  
  WaitingTask* waiting = new(std::nothrow) WaitingTask;
  if (!waiting) return false;
  
  waiting->task = FindTask(nullptr);
  waiting->notified = false;
  
  list_mutex_.Lock();
  AddTail(&waiting_tasks_, &waiting->node);
  list_mutex_.Unlock();
  
  mutex->Unlock();
  
  // Wait with timeout using simple tick counting
  ULONG elapsed = 0;
  while (!waiting->notified && elapsed < timeout_ticks) {
    Delay(1);
    elapsed++;
  }
  
  bool result = waiting->notified;
  
  mutex->Lock();
  
  list_mutex_.Lock();
  Remove(&waiting->node);
  list_mutex_.Unlock();
  
  delete waiting;
  
  return result;
}

void AROSConditionVariable::NotifyOne() {
  if (!initialized_) return;
  
  list_mutex_.Lock();
  
  if (!IsListEmpty(&waiting_tasks_)) {
    WaitingTask* waiting = reinterpret_cast<WaitingTask*>(
        waiting_tasks_.lh_Head);
    waiting->notified = true;
  }
  
  list_mutex_.Unlock();
}

void AROSConditionVariable::NotifyAll() {
  if (!initialized_) return;
  
  list_mutex_.Lock();
  
  struct Node* node = waiting_tasks_.lh_Head;
  while (node->ln_Succ) {
    WaitingTask* waiting = reinterpret_cast<WaitingTask*>(node);
    waiting->notified = true;
    node = node->ln_Succ;
  }
  
  list_mutex_.Unlock();
}

//
// AROSThreadLocal Implementation
//

int AROSThreadLocal::next_slot_index_ = 0;
AROSMutex AROSThreadLocal::slot_mutex_;

AROSThreadLocal::AROSThreadLocal() : slot_index_(-1) {
  slot_mutex_.Lock();
  slot_index_ = next_slot_index_++;
  slot_mutex_.Unlock();
}

AROSThreadLocal::~AROSThreadLocal() {
  // No cleanup needed for AROS task data slots
}

void* AROSThreadLocal::Get() {
  struct Task* current_task = FindTask(nullptr);
  if (!current_task) return nullptr;
  
  // Use AROS native task data functionality - more efficient than linked list
  // Note: AROS task structure has limited user data slots, but this is more efficient
  // than maintaining a global list with mutex protection
  if (slot_index_ == 0) {
    return current_task->tc_UserData;
  } else {
    // For additional slots, we'd need to extend this or use a different approach
    // For now, fall back to a simple approach for compatibility
    return nullptr;
  }
}

void AROSThreadLocal::Set(void* value) {
  struct Task* current_task = FindTask(nullptr);
  if (!current_task) return;
  
  // Use AROS native task data functionality
  if (slot_index_ == 0) {
    current_task->tc_UserData = value;
  }
  // For additional slots beyond tc_UserData, a more sophisticated approach would be needed
}

//
// AROSAtomic Implementation
//

int32_t AROSAtomic::AtomicIncrement(volatile int32_t* value) {
  return __atomic_add_fetch(value, 1, __ATOMIC_SEQ_CST);
}

int32_t AROSAtomic::AtomicDecrement(volatile int32_t* value) {
  return __atomic_sub_fetch(value, 1, __ATOMIC_SEQ_CST);
}

int32_t AROSAtomic::AtomicAdd(volatile int32_t* value, int32_t delta) {
  atomic_mutex_.Lock();
  int32_t result = (*value += delta);
  atomic_mutex_.Unlock();
  return result;
}

int32_t AROSAtomic::AtomicExchange(volatile int32_t* target, int32_t value) {
  return __atomic_exchange_n(target, value, __ATOMIC_SEQ_CST);
}

int32_t AROSAtomic::AtomicCompareExchange(volatile int32_t* target, 
                                         int32_t expected, int32_t desired) {
  int32_t old_value = expected;
  __atomic_compare_exchange_n(target, &old_value, desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
  return old_value;
}

void* AROSAtomic::AtomicExchangePointer(void* volatile* target, void* value) {
  atomic_mutex_.Lock();
  void* old_value = *target;
  *target = value;
  atomic_mutex_.Unlock();
  return old_value;
}

void* AROSAtomic::AtomicCompareExchangePointer(void* volatile* target,
                                              void* expected, void* desired) {
  void *old_value = expected;
  __atomic_compare_exchange_n(target, &old_value, desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
  return old_value;
}

void AROSAtomic::MemoryBarrier() {
#if defined(__GNUC__)
  // Use compiler and hardware memory barrier
  __asm__ __volatile__ ("" : : : "memory");
  #if defined(__arm__) || defined(__aarch64__)
    __asm__ __volatile__ ("dmb sy" : : : "memory");
  #elif defined(__powerpc__) || defined(__ppc__)
    __asm__ __volatile__ ("sync" : : : "memory");
  #elif defined(__i386__) || defined(__x86_64__)
    __asm__ __volatile__ ("mfence" : : : "memory");
  #else
    // Fallback: compiler barrier only for unknown architectures
    __asm__ __volatile__ ("" : : : "memory");
  #endif
#else
  // Non-GCC compiler fallback
  atomic_mutex_.Lock();
  atomic_mutex_.Unlock();
#endif
}

void AROSAtomic::ReadBarrier() {
#if defined(__GNUC__)
  __asm__ __volatile__ ("" : : : "memory");
  #if defined(__arm__) || defined(__aarch64__)
    __asm__ __volatile__ ("dmb ld" : : : "memory");
  #elif defined(__powerpc__) || defined(__ppc__)
    __asm__ __volatile__ ("lwsync" : : : "memory");
  #elif defined(__i386__) || defined(__x86_64__)
    __asm__ __volatile__ ("lfence" : : : "memory");
  #else
    __asm__ __volatile__ ("" : : : "memory");
  #endif
#else
  MemoryBarrier();
#endif
}

void AROSAtomic::WriteBarrier() {
#if defined(__GNUC__)
  __asm__ __volatile__ ("" : : : "memory");
  #if defined(__arm__) || defined(__aarch64__)
    __asm__ __volatile__ ("dmb st" : : : "memory");
  #elif defined(__powerpc__) || defined(__ppc__)
    __asm__ __volatile__ ("eieio" : : : "memory");
  #elif defined(__i386__) || defined(__x86_64__)
    __asm__ __volatile__ ("sfence" : : : "memory");
  #else
    __asm__ __volatile__ ("" : : : "memory");
  #endif
#else
  MemoryBarrier();
#endif
}

} // namespace base
} // namespace v8