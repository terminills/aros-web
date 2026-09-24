// REF_GITHUB_ISSUE: #112
// AROS_IMPL: Direct integration with exec.library, timer.device, dos.library

#include "v8-platform-backend.h"

#include <exec/exec.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>
#include <proto/alib.h>
#include <proto/arossupport.h>  /* kprintf */

#include <cstdio>
#include <cstdlib>
#include <cstring>

// Undefine AROS dos.library macros that conflict with C++ method names
// Lock() is a DOS library function that conflicts with AROSMutex::Lock()
#ifdef Lock
#undef Lock
#endif
#ifdef Unlock
#undef Unlock
#endif

namespace v8 {
namespace aros {

// Helper structure for thread entry wrapper
struct ThreadEntryData {
  void (*entry_function)(void*);
  void* user_data;
};

// Wrapper to convert void (*)(void*) to void* (*)(void*)
static void* ThreadEntryWrapper(void* data) {
  ThreadEntryData* entry_data = static_cast<ThreadEntryData*>(data);
  void (*entry_func)(void*) = entry_data->entry_function;
  void* user_data = entry_data->user_data;
  
  // Call the actual entry function
  entry_func(user_data);
  
  // Clean up after function returns
  delete entry_data;
  
  return nullptr;
}

// Static singleton instance
AROSPlatformBackend* AROSPlatformBackend::instance_ = nullptr;

//
// AROSPlatformBackend Implementation
//

AROSPlatformBackend::AROSPlatformBackend()
    : initialized_(false),
      timer_base_(nullptr),
      timer_port_(nullptr),
      timer_io_(nullptr),
      num_background_threads_(0),
      foreground_task_head_(nullptr),
      foreground_task_tail_(nullptr),
      foreground_mutex_(nullptr),
      start_ticks_(0),
      executable_memory_allocated_(0),
      executable_memory_limit_(kDefaultExecutableMemoryLimit),
      tracing_enabled_(false) {
  // Initialize background thread array
  for (int i = 0; i < kMaxBackgroundThreads; i++) {
    background_threads_[i] = nullptr;
  }
}

AROSPlatformBackend::~AROSPlatformBackend() {
  Shutdown();
}

// static
AROSPlatformBackend* AROSPlatformBackend::Create() {
  if (instance_ == nullptr) {
    instance_ = new AROSPlatformBackend();
  }
  return instance_;
}

// static
AROSPlatformBackend* AROSPlatformBackend::Get() {
  return instance_;
}

// static
void AROSPlatformBackend::Destroy() {
  if (instance_ != nullptr) {
    delete instance_;
    instance_ = nullptr;
  }
}

bool AROSPlatformBackend::Initialize() {
  if (initialized_) {
    return true;
  }
  
  kprintf("[V8-BACKEND] Initialize() start\n");
  
  // Create timer.device connection for time functions
  timer_port_ = CreateMsgPort();
  if (!timer_port_) {
    kprintf("[V8-BACKEND] CreateMsgPort FAILED\n");
    PrintError("Failed to create timer message port");
    return false;
  }
  kprintf("[V8-BACKEND] timer_port_=%p\n", timer_port_);
  
  timer_io_ = (struct timerequest*)CreateIORequest(timer_port_, 
                                                   sizeof(struct timerequest));
  if (!timer_io_) {
    kprintf("[V8-BACKEND] CreateIORequest FAILED\n");
    PrintError("Failed to create timer IO request");
    DeleteMsgPort(timer_port_);
    timer_port_ = nullptr;
    return false;
  }
  kprintf("[V8-BACKEND] timer_io_=%p\n", timer_io_);
  
  if (OpenDevice(TIMERNAME, UNIT_MICROHZ, (struct IORequest*)timer_io_, 0) != 0) {
    kprintf("[V8-BACKEND] OpenDevice timer FAILED\n");
    PrintError("Failed to open timer.device");
    DeleteIORequest((struct IORequest*)timer_io_);
    DeleteMsgPort(timer_port_);
    timer_io_ = nullptr;
    timer_port_ = nullptr;
    return false;
  }
  
  timer_base_ = (struct Library*)timer_io_->tr_node.io_Device;
  kprintf("[V8-BACKEND] timer_base_=%p\n", timer_base_);
  
  // Initialize monotonic time base
  // Shadow the global TimerBase with our local — the AROS inline stubs
  // from proto/timer.h reference "TimerBase" by name, and the global is NULL
  // because nobody assigned it.  Using a local variable of the same name
  // makes GetSysTime/GetUpTime use OUR timer_base_ without touching the global.
  struct timeval tv;
  {
    struct Library *TimerBase = timer_base_;
    kprintf("[V8-BACKEND] calling GetSysTime (local TimerBase=%p)\n", TimerBase);
    GetSysTime(&tv);
  }
  start_ticks_ = tv.tv_secs * 1000000ULL + tv.tv_micro;
  kprintf("[V8-BACKEND] start_ticks_=%llu\n", (unsigned long long)start_ticks_);
  
  // Create mutex for foreground task queue
  kprintf("[V8-BACKEND] creating foreground mutex\n");
  foreground_mutex_ = new base::AROSMutex();
  if (!foreground_mutex_) {
    PrintError("Failed to create foreground mutex");
    CloseDevice((struct IORequest*)timer_io_);
    DeleteIORequest((struct IORequest*)timer_io_);
    DeleteMsgPort(timer_port_);
    return false;
  }
  
  initialized_ = true;
  kprintf("[V8-BACKEND] Initialize() DONE\n");
  
  return true;
}

void AROSPlatformBackend::Shutdown() {
  if (!initialized_) {
    return;
  }
  
  // Stop background threads
  // Note: We cannot delete AROSThread objects because the destructor is private.
  // The threads are terminated (which stops execution) and joined (which waits for completion).
  // The AROSThread wrapper objects remain allocated, but the underlying AROS Task resources
  // (stack, task structure) are freed by the AROSThread destructor when appropriate.
  // 
  // TODO: Consider refactoring AROSThread to use NewCreateTaskA() which would provide
  // automatic cleanup, or make the destructor public/add a Destroy() method for explicit cleanup.
  for (int i = 0; i < num_background_threads_; i++) {
    if (background_threads_[i]) {
      background_threads_[i]->Terminate();
      background_threads_[i]->Join();
      background_threads_[i] = nullptr;
    }
  }
  num_background_threads_ = 0;
  
  // Clean up foreground task queue
  if (foreground_mutex_) {
    foreground_mutex_->Lock();
    ForegroundTask* task = foreground_task_head_;
    while (task) {
      ForegroundTask* next = task->next;
      delete task;
      task = next;
    }
    foreground_task_head_ = nullptr;
    foreground_task_tail_ = nullptr;
    foreground_mutex_->Unlock();
    delete foreground_mutex_;
    foreground_mutex_ = nullptr;
  }
  
  // Close timer.device
  if (timer_io_) {
    CloseDevice((struct IORequest*)timer_io_);
    DeleteIORequest((struct IORequest*)timer_io_);
    timer_io_ = nullptr;
  }
  
  if (timer_port_) {
    DeleteMsgPort(timer_port_);
    timer_port_ = nullptr;
  }
  
  timer_base_ = nullptr;
  
  initialized_ = false;
  Print("V8 AROS Platform Backend shutdown");
}

//
// Thread Management
//

bool AROSPlatformBackend::CreateThread(void (*entry)(void*), void* data) {
  if (!initialized_) {
    return false;
  }
  
  // Create wrapper data
  ThreadEntryData* entry_data = new ThreadEntryData();
  if (!entry_data) {
    return false;
  }
  entry_data->entry_function = entry;
  entry_data->user_data = data;
  
  // Use AROSThread from Phase 2.2
  base::AROSThread* thread = base::AROSThread::Create(ThreadEntryWrapper, entry_data);
  if (!thread) {
    delete entry_data;
    return false;
  }
  
  // Start the thread
  if (!thread->Start()) {
    // Note: Cannot delete thread object as destructor is private.
    // If Start() fails, it cleans up its own internal resources (task structure, stack).
    // The AROSThread wrapper object is leaked here, but this should be rare (only on failure).
    delete entry_data;
    return false;
  }
  
  // Store in background thread pool if there's space
  if (num_background_threads_ < kMaxBackgroundThreads) {
    background_threads_[num_background_threads_++] = thread;
  }
  
  return true;
}

uintptr_t AROSPlatformBackend::GetCurrentThreadId() {
  // AROS task address is unique thread ID
  // Use AROS exec Task type, not v8::Task
  struct ::Task* task = FindTask(nullptr);
  return reinterpret_cast<uintptr_t>(task);
}

//
// Time Functions
//

double AROSPlatformBackend::MonotonicallyIncreasingTime() {
  if (!initialized_) {
    return 0.0;
  }
  
  struct timeval tv;
  struct Library *TimerBase = timer_base_;
  GetSysTime(&tv);
  
  uint64_t current_ticks = tv.tv_secs * 1000000ULL + tv.tv_micro;
  uint64_t elapsed_micros = current_ticks - start_ticks_;
  
  // Convert to seconds as double
  return static_cast<double>(elapsed_micros) / 1000000.0;
}

double AROSPlatformBackend::CurrentClockTimeMillis() {
  if (!initialized_) {
    return 0.0;
  }
  
  struct timeval tv;
  struct Library *TimerBase = timer_base_;
  GetSysTime(&tv);
  
  // Convert to milliseconds since Unix epoch
  // Note: AROS uses Amiga epoch (Jan 1, 1978), need to adjust to Unix epoch (Jan 1, 1970)
  // Offset: 8 years including 2 leap years (1972, 1976) = 2922 days = 252460800 seconds
  const uint64_t UNIX_TO_AMIGA_OFFSET = 252460800ULL;  // seconds
  
  uint64_t unix_secs = tv.tv_secs + UNIX_TO_AMIGA_OFFSET;
  uint64_t millis = unix_secs * 1000ULL + tv.tv_micro / 1000ULL;
  
  return static_cast<double>(millis);
}

//
// Memory Management
//

void* AROSPlatformBackend::AllocateExecutableMemory(size_t size) {
  if (!initialized_) {
    return nullptr;
  }
  
  // Check memory limit
  if (executable_memory_allocated_ + size > executable_memory_limit_) {
    PrintError("Executable memory limit reached");
    return nullptr;
  }
  
  // Allocate memory that can be made executable
  // AROS doesn't have strict W^X, so regular allocation works
  // In future, could use MEMF_EXECUTABLE if available
  void* memory = AllocVec(size, MEMF_PUBLIC | MEMF_CLEAR);
  
  if (memory) {
    executable_memory_allocated_ += size;
  }
  
  return memory;
}

void AROSPlatformBackend::FreeExecutableMemory(void* address, size_t size) {
  if (!address) {
    return;
  }
  
  FreeVec(address);
  
  if (executable_memory_allocated_ >= size) {
    executable_memory_allocated_ -= size;
  }
}

bool AROSPlatformBackend::SetMemoryExecutable(void* address, size_t size) {
  // On AROS, memory is already executable by default
  // This function is a no-op but included for API compatibility
  (void)address;
  (void)size;
  return true;
}

//
// Task Scheduling
//

void AROSPlatformBackend::PostForegroundTask(void* isolate, 
                                             void (*task)(void*), 
                                             void* data) {
  PostDelayedForegroundTask(isolate, task, data, 0.0);
}

void AROSPlatformBackend::PostDelayedForegroundTask(void* isolate,
                                                    void (*task)(void*),
                                                    void* data,
                                                    double delay_in_seconds) {
  if (!initialized_ || !foreground_mutex_) {
    return;
  }
  
  // Create task entry
  ForegroundTask* new_task = new ForegroundTask();
  if (!new_task) {
    PrintError("Failed to allocate foreground task");
    return;
  }
  
  new_task->function = task;
  new_task->data = data;
  new_task->delay_until = (delay_in_seconds > 0.0) 
                          ? MonotonicallyIncreasingTime() + delay_in_seconds 
                          : 0.0;
  new_task->next = nullptr;
  
  // Add to queue
  foreground_mutex_->Lock();
  
  if (foreground_task_tail_) {
    foreground_task_tail_->next = new_task;
    foreground_task_tail_ = new_task;
  } else {
    foreground_task_head_ = new_task;
    foreground_task_tail_ = new_task;
  }
  
  foreground_mutex_->Unlock();
}

void AROSPlatformBackend::PostBackgroundTask(void (*task)(void*), void* data) {
  if (!initialized_) {
    return;
  }
  
  // Create a new thread for this task
  // In a production system, this would use a thread pool
  CreateThread(task, data);
}

void AROSPlatformBackend::PumpForegroundTasks() {
  if (!initialized_ || !foreground_mutex_) {
    return;
  }
  
  double current_time = MonotonicallyIncreasingTime();
  
  foreground_mutex_->Lock();
  
  ForegroundTask* prev = nullptr;
  ForegroundTask* task = foreground_task_head_;
  
  while (task) {
    // Check if task is ready to run
    bool ready = (task->delay_until == 0.0) || (current_time >= task->delay_until);
    
    if (ready) {
      // Remove from queue
      ForegroundTask* to_run = task;
      task = task->next;
      
      if (prev) {
        prev->next = task;
      } else {
        foreground_task_head_ = task;
      }
      
      if (to_run == foreground_task_tail_) {
        foreground_task_tail_ = prev;
      }
      
      // Unlock mutex while running task
      foreground_mutex_->Unlock();
      
      // Run the task
      if (to_run->function) {
        to_run->function(to_run->data);
      }
      
      delete to_run;
      
      // Re-lock for next iteration
      foreground_mutex_->Lock();
      
      // Restart from beginning since queue may have changed
      prev = nullptr;
      task = foreground_task_head_;
    } else {
      prev = task;
      task = task->next;
    }
  }
  
  foreground_mutex_->Unlock();
}

//
// File I/O
//

char* AROSPlatformBackend::ReadFile(const char* filename, size_t* size) {
  if (!filename || !size) {
    return nullptr;
  }
  
  BPTR file = Open(filename, MODE_OLDFILE);
  if (!file) {
    return nullptr;
  }
  
  // Get file size
  Seek(file, 0, OFFSET_END);
  LONG file_size = Seek(file, 0, OFFSET_BEGINNING);
  
  if (file_size < 0) {
    Close(file);
    return nullptr;
  }
  
  // Check for unreasonably large files (safety check)
  if (file_size > static_cast<LONG>(kMaxFileSize)) {
    Close(file);
    return nullptr;
  }
  
  // Allocate buffer
  char* buffer = (char*)AllocVec(file_size + 1, MEMF_PUBLIC | MEMF_CLEAR);
  if (!buffer) {
    Close(file);
    return nullptr;
  }
  
  // Read file
  LONG bytes_read = Read(file, buffer, file_size);
  Close(file);
  
  if (bytes_read != file_size) {
    FreeVec(buffer);
    return nullptr;
  }
  
  buffer[file_size] = '\0';
  *size = file_size;
  
  return buffer;
}

bool AROSPlatformBackend::WriteFile(const char* filename, 
                                   const char* data, 
                                   size_t size) {
  if (!filename || !data) {
    return false;
  }
  
  BPTR file = Open(filename, MODE_NEWFILE);
  if (!file) {
    return false;
  }
  
  LONG bytes_written = Write(file, data, size);
  Close(file);
  
  return bytes_written == static_cast<LONG>(size);
}

//
// Debugging and Tracing
//

void AROSPlatformBackend::Print(const char* message) {
  if (message) {
    printf("[V8 AROS] %s\n", message);
    Flush(Output());
  }
}

void AROSPlatformBackend::PrintError(const char* message) {
  if (message) {
    fprintf(stderr, "[V8 AROS ERROR] %s\n", message);
    BPTR error_fh = ErrorOutput();
    if (error_fh) {
      Flush(error_fh);
    }
  }
}

void AROSPlatformBackend::SetTracingEnabled(bool enabled) {
  tracing_enabled_ = enabled;
  if (enabled) {
    Print("V8 tracing enabled");
  }
}

//
// AROSV8Platform Implementation
//

AROSV8Platform::AROSV8Platform() : backend_(nullptr) {
}

AROSV8Platform::~AROSV8Platform() {
  Shutdown();
}

bool AROSV8Platform::Initialize() {
  kprintf("[V8-PLATFORM] AROSV8Platform::Initialize() start\n");
  backend_ = AROSPlatformBackend::Create();
  if (!backend_) {
    kprintf("[V8-PLATFORM] Create() returned NULL!\n");
    return false;
  }
  kprintf("[V8-PLATFORM] backend_=%p, calling backend_->Initialize()\n", backend_);
  
  return backend_->Initialize();
}

void AROSV8Platform::Shutdown() {
  if (backend_) {
    backend_->Shutdown();
    AROSPlatformBackend::Destroy();
    backend_ = nullptr;
  }
}

void AROSV8Platform::CallOnWorkerThread(void (*function)(void*), void* data) {
  if (backend_) {
    backend_->PostBackgroundTask(function, data);
  }
}

double AROSV8Platform::MonotonicallyIncreasingTime() {
  if (backend_) {
    return backend_->MonotonicallyIncreasingTime();
  }
  return 0.0;
}

double AROSV8Platform::CurrentClockTimeMillis() {
  if (backend_) {
    return backend_->CurrentClockTimeMillis();
  }
  return 0.0;
}

void AROSV8Platform::CallOnForegroundThread(void* isolate, 
                                           void (*task)(void*), 
                                           void* data) {
  if (backend_) {
    backend_->PostForegroundTask(isolate, task, data);
  }
}

void AROSV8Platform::CallDelayedOnForegroundThread(void* isolate,
                                                  void (*task)(void*),
                                                  void* data,
                                                  double delay_in_seconds) {
  if (backend_) {
    backend_->PostDelayedForegroundTask(isolate, task, data, delay_in_seconds);
  }
}

}  // namespace aros
}  // namespace v8
