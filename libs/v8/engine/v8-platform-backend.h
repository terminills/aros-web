// REF_GITHUB_ISSUE: #112
// AROS_IMPL: Uses exec.library, timer.device, dos.library for system integration

#ifndef V8_AROS_PLATFORM_BACKEND_H_
#define V8_AROS_PLATFORM_BACKEND_H_

#include "platform-aros.h"
#include "threading-aros.h"
#include "memory-aros.h"

#include <devices/timer.h>

// Forward declarations for V8 types (when V8 is integrated)
// These will be replaced with actual V8 headers: #include "v8-platform.h"
namespace v8 {
  class Isolate;
  class Platform;
  class Task;
  class TaskRunner;
  class TracingController;
}

namespace v8 {
namespace aros {

//
// AROS Platform Backend for V8
//
// This class implements the v8::Platform interface, providing V8 with
// all the operating system services it needs to function on AROS.
//
class AROSPlatformBackend {
 public:
  // Singleton instance management
  static AROSPlatformBackend* Create();
  static AROSPlatformBackend* Get();
  static void Destroy();
  
  // Initialize the platform with AROS system integration
  bool Initialize();
  void Shutdown();
  
  //
  // Thread Management
  //
  // V8 needs to create threads for background compilation, garbage collection,
  // and other async tasks. We use AROS exec.library tasks.
  //
  
  // Create a new thread for V8 background work
  bool CreateThread(void (*entry)(void*), void* data);
  
  // Get current thread ID (AROS task address)
  uintptr_t GetCurrentThreadId();
  
  //
  // Time Functions
  //
  // V8 needs accurate time for performance measurements, timeouts, and
  // garbage collection scheduling. We use AROS timer.device.
  //
  
  // Get monotonic time in seconds (for performance measurements)
  double MonotonicallyIncreasingTime();
  
  // Get current wall clock time in milliseconds (for Date objects)
  double CurrentClockTimeMillis();
  
  //
  // Memory Management
  //
  // V8 needs to allocate executable memory for JIT-compiled code and
  // manage large memory regions efficiently.
  //
  
  // Allocate executable memory for JIT code
  void* AllocateExecutableMemory(size_t size);
  
  // Free executable memory
  void FreeExecutableMemory(void* address, size_t size);
  
  // Set memory region as executable (for code generation)
  bool SetMemoryExecutable(void* address, size_t size);
  
  //
  // Task Scheduling
  //
  // V8 uses task runners to schedule work on background threads
  // and the main thread (foreground).
  //
  
  // Post a task to run on the foreground thread (main V8 thread)
  void PostForegroundTask(void* isolate, void (*task)(void*), void* data);
  
  // Post a delayed task to run after a specified time
  void PostDelayedForegroundTask(void* isolate, void (*task)(void*), 
                                 void* data, double delay_in_seconds);
  
  // Post a task to run on a background thread (worker pool)
  void PostBackgroundTask(void (*task)(void*), void* data);
  
  // Pump the foreground message loop (call from main thread)
  void PumpForegroundTasks();
  
  //
  // File I/O
  //
  // For loading scripts and writing logs/traces
  //
  
  // Read entire file into memory
  char* ReadFile(const char* filename, size_t* size);
  
  // Write data to file
  bool WriteFile(const char* filename, const char* data, size_t size);
  
  //
  // Debugging and Tracing
  //
  
  // Print debug message to console
  void Print(const char* message);
  
  // Print error message to console
  void PrintError(const char* message);
  
  // Enable/disable V8 tracing
  void SetTracingEnabled(bool enabled);
  
 private:
  AROSPlatformBackend();
  ~AROSPlatformBackend();
  
  // Prevent copying
  AROSPlatformBackend(const AROSPlatformBackend&) = delete;
  AROSPlatformBackend& operator=(const AROSPlatformBackend&) = delete;
  
  // Singleton instance
  static AROSPlatformBackend* instance_;
  
  // Configuration constants
  static const size_t kMaxFileSize = 100 * 1024 * 1024;  // 100MB
  static const size_t kDefaultExecutableMemoryLimit = 256 * 1024 * 1024;  // 256MB
  
  // Platform state
  bool initialized_;
  
  // AROS system handles
  struct ::Library* timer_base_;
  struct ::MsgPort* timer_port_;
  struct ::timerequest* timer_io_;
  
  // Thread pool for background tasks
  static const int kMaxBackgroundThreads = 4;
  base::AROSThread* background_threads_[kMaxBackgroundThreads];
  int num_background_threads_;
  
  // Task queue for foreground tasks
  struct ForegroundTask {
    void (*function)(void*);
    void* data;
    double delay_until;  // 0 = run immediately
    ForegroundTask* next;
  };
  
  ForegroundTask* foreground_task_head_;
  ForegroundTask* foreground_task_tail_;
  base::AROSMutex* foreground_mutex_;
  
  // Timing state
  uint64_t start_ticks_;  // For monotonic time
  
  // Memory statistics
  size_t executable_memory_allocated_;
  size_t executable_memory_limit_;
  
  // Tracing state
  bool tracing_enabled_;
};

//
// V8 Platform Adapter
//
// This will be the actual v8::Platform implementation when V8 is integrated.
// For now it provides the same interface that V8 expects.
//
class AROSV8Platform {
 public:
  AROSV8Platform();
  virtual ~AROSV8Platform();
  
  // v8::Platform interface (simplified for now)
  // When V8 is integrated, this will inherit from v8::Platform
  
  // Called by V8 to initialize the platform
  bool Initialize();
  
  // Called by V8 to shutdown the platform
  void Shutdown();
  
  // Thread management
  void CallOnWorkerThread(void (*function)(void*), void* data);
  
  // Time functions
  double MonotonicallyIncreasingTime();
  double CurrentClockTimeMillis();
  
  // Task scheduling
  void CallOnForegroundThread(void* isolate, void (*task)(void*), void* data);
  void CallDelayedOnForegroundThread(void* isolate, void (*task)(void*), 
                                     void* data, double delay_in_seconds);
  
  // Get the backend implementation
  AROSPlatformBackend* GetBackend() { return backend_; }
  
 private:
  AROSPlatformBackend* backend_;
};

}  // namespace aros
}  // namespace v8

#endif  // V8_AROS_PLATFORM_BACKEND_H_
