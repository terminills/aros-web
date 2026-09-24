// REF_GITHUB_ISSUE: #112
// AROS_IMPL: Integrates with AROS memory management and provides JS execution capabilities

#include "isolate-aros.h"

#include <exec/exec.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <new>

// Undefine AROS macros that conflict with C++ method names.
// These macros are defined in <proto/exec.h> and <inline/exec.h>
// and interfere with method calls like Allocate().
#ifdef Allocate
#undef Allocate
#endif

namespace v8 {
namespace internal {

// Static member initialization
Platform* AROSIsolateManager::platform_ = nullptr;
bool AROSIsolateManager::platform_ready_ = false;
void (*AROSIsolateManager::error_handler_)(const char*) = nullptr;
char AROSIsolateManager::last_error_[512] = {0};

//
// AROSIsolateManager Implementation
//

bool AROSIsolateManager::Initialize() {
  if (platform_ready_) return true;
  
  // Initialize AROS memory management
  if (!AROSMemoryManager::Initialize()) {
    strncpy(last_error_, "Failed to initialize AROS memory manager", sizeof(last_error_) - 1);
    return false;
  }
  
  // Initialize V8 platform layer
  InitializeV8Platform();
  
  platform_ready_ = true;
  return true;
}

void AROSIsolateManager::Shutdown() {
  if (!platform_ready_) return;
  
  // Cleanup V8 platform
  if (platform_) {
    // Note: In a full V8 implementation, we would call V8::Dispose()
    platform_ = nullptr;
  }
  
  // Cleanup AROS memory management
  AROSMemoryManager::Shutdown();
  
  platform_ready_ = false;
}

void AROSIsolateManager::InitializeV8Platform() {
  // Note: In a full V8 integration, this would create the actual V8 platform
  // For Phase 2.2, we're creating the foundation that would integrate with V8
  
  // Create AROS-specific V8 platform (placeholder)
  platform_ = reinterpret_cast<Platform*>(0x1); // Non-null marker
  
  strncpy(last_error_, "V8 platform initialized (AROS foundation)", sizeof(last_error_) - 1);
}

Isolate* AROSIsolateManager::CreateIsolate() {
  if (!platform_ready_) {
    strncpy(last_error_, "Platform not initialized", sizeof(last_error_) - 1);
    return nullptr;
  }
  
  // CRITICAL: This is a Phase 2.2 placeholder implementation
  // DO NOT USE IN PRODUCTION - would crash with real V8 operations
  // Real implementation must call v8::Isolate::New() with proper parameters
  
  // Create a safe placeholder structure that won't be dereferenced
  struct IsolatePlaceholder {
    uint32_t magic_marker;     // 0xDEADBEEF to detect invalid usage
    bool is_placeholder;       // Always true
    char padding[64];          // Safe memory space
  };
  
  IsolatePlaceholder* placeholder = reinterpret_cast<IsolatePlaceholder*>(
      AROSMemoryManager::Allocate(sizeof(IsolatePlaceholder), 8));
  if (!placeholder) {
    strncpy(last_error_, "Failed to allocate isolate placeholder", sizeof(last_error_) - 1);
    return nullptr;
  }
  
  placeholder->magic_marker = 0xDEADBEEF;
  placeholder->is_placeholder = true;
  memset(placeholder->padding, 0, sizeof(placeholder->padding));
  
  // WARNING: This cast is only safe because we never dereference as V8::Isolate
  Isolate* isolate = reinterpret_cast<Isolate*>(placeholder);
  
  snprintf(last_error_, sizeof(last_error_), 
           "Created isolate placeholder (Phase 2.2) - NOT FUNCTIONAL");
  
  return isolate;
}

void AROSIsolateManager::DisposeIsolate(Isolate* isolate) {
  if (!isolate) return;
  
  // CRITICAL: This is a Phase 2.2 placeholder implementation
  // Real implementation must call isolate->Dispose() before this
  
  // Validate this is our placeholder structure
  struct IsolatePlaceholder {
    uint32_t magic_marker;
    bool is_placeholder;
    char padding[64];
  };
  
  IsolatePlaceholder* placeholder = reinterpret_cast<IsolatePlaceholder*>(isolate);
  
  // Basic sanity check to detect invalid usage
  if (placeholder->magic_marker == 0xDEADBEEF && placeholder->is_placeholder) {
    AROSMemoryManager::Free(placeholder, sizeof(IsolatePlaceholder));
  } else {
    // This suggests someone tried to pass a real V8 isolate or corrupted memory
    strncpy(last_error_, "ERROR: Invalid isolate passed to DisposeIsolate - possible crash avoided", 
            sizeof(last_error_) - 1);
  }
}

Context* AROSIsolateManager::CreateContext(Isolate* isolate) {
  if (!isolate) {
    strncpy(last_error_, "Invalid isolate for context creation", sizeof(last_error_) - 1);
    return nullptr;
  }
  
  // CRITICAL: This is a Phase 2.2 placeholder implementation
  // Real implementation must call v8::Context::New(isolate) with proper setup
  
  // Create safe placeholder that won't be dereferenced as V8::Context
  struct ContextPlaceholder {
    uint32_t magic_marker;     // 0xC0DEFEED to detect invalid usage
    bool is_placeholder;       // Always true
    Isolate* parent_isolate;   // Track parent for validation
    char padding[64];          // Safe memory space
  };
  
  ContextPlaceholder* placeholder = reinterpret_cast<ContextPlaceholder*>(
      AROSMemoryManager::Allocate(sizeof(ContextPlaceholder), 8));
  if (!placeholder) {
    strncpy(last_error_, "Failed to allocate context placeholder", sizeof(last_error_) - 1);
    return nullptr;
  }
  
  placeholder->magic_marker = 0xC0DEFEED;
  placeholder->is_placeholder = true;
  placeholder->parent_isolate = isolate;
  memset(placeholder->padding, 0, sizeof(placeholder->padding));
  
  // WARNING: This cast is only safe because we never dereference as V8::Context
  Context* context = reinterpret_cast<Context*>(placeholder);
  
  snprintf(last_error_, sizeof(last_error_), 
           "Created context placeholder (Phase 2.2) - NOT FUNCTIONAL");
  
  return context;
}

void AROSIsolateManager::DisposeContext(Context* context) {
  if (!context) return;
  
  // CRITICAL: This is a Phase 2.2 placeholder implementation
  // Real implementation must call context cleanup before this
  
  struct ContextPlaceholder {
    uint32_t magic_marker;
    bool is_placeholder;
    Isolate* parent_isolate;
    char padding[64];
  };
  
  ContextPlaceholder* placeholder = reinterpret_cast<ContextPlaceholder*>(context);
  
  // Basic sanity check to detect invalid usage
  if (placeholder->magic_marker == 0xC0DEFEED && placeholder->is_placeholder) {
    AROSMemoryManager::Free(placeholder, sizeof(ContextPlaceholder));
  } else {
    // This suggests someone tried to pass a real V8 context or corrupted memory
    strncpy(last_error_, "ERROR: Invalid context passed to DisposeContext - possible crash avoided", 
            sizeof(last_error_) - 1);
  }
}

bool AROSIsolateManager::ExecuteScript(Isolate* isolate, Context* context,
                                      const char* source, const char* name) {
  if (!isolate || !context || !source) {
    strncpy(last_error_, "Invalid parameters for script execution", sizeof(last_error_) - 1);
    return false;
  }
  
  // CRITICAL: This is a Phase 2.2 simulation - does NOT execute JavaScript
  // Real implementation must use V8's compilation and execution pipeline:
  // 1. v8::String::NewFromUtf8(isolate, source)
  // 2. v8::Script::Compile(context, source_string) 
  // 3. script->Run(context)
  
  // Validate our placeholder structures
  struct IsolatePlaceholder {
    uint32_t magic_marker;
    bool is_placeholder;
    char padding[64];
  };
  
  struct ContextPlaceholder {
    uint32_t magic_marker;
    bool is_placeholder;
    Isolate* parent_isolate;
    char padding[64];
  };
  
  IsolatePlaceholder* iso_placeholder = reinterpret_cast<IsolatePlaceholder*>(isolate);
  ContextPlaceholder* ctx_placeholder = reinterpret_cast<ContextPlaceholder*>(context);
  
  // Safety checks to prevent crashes with real V8 objects
  if (iso_placeholder->magic_marker != 0xDEADBEEF || !iso_placeholder->is_placeholder) {
    strncpy(last_error_, "ERROR: Real V8 isolate passed to placeholder function - execution aborted", 
            sizeof(last_error_) - 1);
    return false;
  }
  
  if (ctx_placeholder->magic_marker != 0xC0DEFEED || !ctx_placeholder->is_placeholder) {
    strncpy(last_error_, "ERROR: Real V8 context passed to placeholder function - execution aborted", 
            sizeof(last_error_) - 1);
    return false;
  }
  
  // Phase 2.2: Basic script execution simulation
  printf("[AROS V8 SIMULATION] Executing script: %s\n", name ? name : "<anonymous>");
  printf("[AROS V8 SIMULATION] Source length: %zu characters\n", strlen(source));
  printf("[AROS V8 SIMULATION] WARNING: This is NOT real JavaScript execution\n");
  
  // Simple validation
  if (strlen(source) == 0) {
    strncpy(last_error_, "Empty script source", sizeof(last_error_) - 1);
    return false;
  }
  
  // Simulate successful execution
  snprintf(last_error_, sizeof(last_error_), 
           "Script '%s' simulated successfully (Phase 2.2 placeholder)", 
           name ? name : "<anonymous>");
  
  return true;
}

String* AROSIsolateManager::CreateString(Isolate* isolate, const char* data, int length) {
  if (!isolate || !data) return nullptr;
  
  if (length < 0) length = strlen(data);
  
  // Allocate string structure
  size_t string_size = sizeof(void*) * 4 + length + 1;
  void* string_memory = AROSMemoryManager::Allocate(string_size, 4);
  if (!string_memory) return nullptr;
  
  memset(string_memory, 0, string_size);
  
  // Copy string data
  char* string_data = reinterpret_cast<char*>(
      reinterpret_cast<uintptr_t>(string_memory) + sizeof(void*) * 4);
  memcpy(string_data, data, length);
  string_data[length] = '\0';
  
  return reinterpret_cast<String*>(string_memory);
}

void AROSIsolateManager::ReportException(Isolate* isolate, const char* exception) {
  snprintf(last_error_, sizeof(last_error_), "JavaScript Exception: %s", 
           exception ? exception : "Unknown error");
  
  if (error_handler_) {
    error_handler_(last_error_);
  } else {
    printf("[AROS V8 Error] %s\n", last_error_);
  }
}

size_t AROSIsolateManager::GetHeapStatistics(Isolate* isolate) {
  if (!isolate) return 0;
  
  // Return current memory usage from AROS memory manager
  return AROSMemoryManager::GetTotalAllocated();
}

void AROSIsolateManager::RequestGarbageCollection(Isolate* isolate) {
  if (!isolate) return;
  
  // Trigger GC callback if registered
  AROSMemoryManager::NotifyGCStart();
  // In full implementation, would call isolate->RequestGarbageCollection()
  AROSMemoryManager::NotifyGCEnd();
}

void AROSIsolateManager::SetAROSErrorHandler(void (*handler)(const char*)) {
  error_handler_ = handler;
}

//
// AROSExecutionContext Implementation
//

AROSExecutionContext::AROSExecutionContext()
    : isolate_(nullptr), context_(nullptr), initialized_(false),
      has_exception_(false) {
  exception_string_[0] = '\0';
}

AROSExecutionContext::~AROSExecutionContext() {
  Cleanup();
}

bool AROSExecutionContext::Initialize() {
  if (initialized_) return true;
  
  // Create isolate
  isolate_ = AROSIsolateManager::CreateIsolate();
  if (!isolate_) {
    return false;
  }
  
  // Create context
  context_ = AROSIsolateManager::CreateContext(isolate_);
  if (!context_) {
    AROSIsolateManager::DisposeIsolate(isolate_);
    isolate_ = nullptr;
    return false;
  }
  
  initialized_ = true;
  return true;
}

void AROSExecutionContext::Cleanup() {
  if (!initialized_) return;
  
  if (context_) {
    AROSIsolateManager::DisposeContext(context_);
    context_ = nullptr;
  }
  
  if (isolate_) {
    AROSIsolateManager::DisposeIsolate(isolate_);
    isolate_ = nullptr;
  }
  
  initialized_ = false;
}

bool AROSExecutionContext::ExecuteString(const char* source, const char* name) {
  if (!initialized_ || !source) {
    return false;
  }
  
  ClearException();
  
  bool result = AROSIsolateManager::ExecuteScript(isolate_, context_, source, name);
  if (!result) {
    has_exception_ = true;
    strncpy(exception_string_, AROSIsolateManager::GetLastError(), 
            sizeof(exception_string_) - 1);
  }
  
  return result;
}

bool AROSExecutionContext::ExecuteFile(const char* filename) {
  if (!filename) return false;
  
  // Read file content
  BPTR file = Open(filename, MODE_OLDFILE);
  if (!file) {
    has_exception_ = true;
    snprintf(exception_string_, sizeof(exception_string_),
             "Could not open file: %s", filename);
    return false;
  }
  
  // Get file size
  Seek(file, 0, OFFSET_END);
  LONG file_size = Seek(file, 0, OFFSET_BEGINNING);
  
  if (file_size <= 0) {
    Close(file);
    has_exception_ = true;
    strncpy(exception_string_, "File is empty or could not read size", 
            sizeof(exception_string_) - 1);
    return false;
  }
  
  // Allocate buffer
  char* buffer = reinterpret_cast<char*>(
      AROSMemoryManager::Allocate(file_size + 1, 1));
  if (!buffer) {
    Close(file);
    has_exception_ = true;
    strncpy(exception_string_, "Could not allocate memory for file", 
            sizeof(exception_string_) - 1);
    return false;
  }
  
  // Read file
  LONG bytes_read = Read(file, buffer, file_size);
  Close(file);
  
  if (bytes_read != file_size) {
    AROSMemoryManager::Free(buffer, file_size + 1);
    has_exception_ = true;
    strncpy(exception_string_, "Could not read complete file", 
            sizeof(exception_string_) - 1);
    return false;
  }
  
  buffer[file_size] = '\0';
  
  // Execute script
  bool result = ExecuteString(buffer, filename);
  
  // Cleanup
  AROSMemoryManager::Free(buffer, file_size + 1);
  
  return result;
}

const char* AROSExecutionContext::ValueToString(Value* value) {
  // Simplified value conversion for Phase 2.2
  if (!value) return nullptr;
  
  // In full implementation, would convert V8 value to string
  return "<value>"; // Placeholder
}

int32_t AROSExecutionContext::ValueToInt32(Value* value) {
  if (!value) return 0;
  // In full implementation, would convert V8 value to int32
  return 0; // Placeholder
}

double AROSExecutionContext::ValueToNumber(Value* value) {
  if (!value) return 0.0;
  // In full implementation, would convert V8 value to number
  return 0.0; // Placeholder
}

bool AROSExecutionContext::ValueToBoolean(Value* value) {
  if (!value) return false;
  // In full implementation, would convert V8 value to boolean
  return false; // Placeholder
}

void AROSExecutionContext::ClearException() {
  has_exception_ = false;
  exception_string_[0] = '\0';
}

void AROSExecutionContext::HandleException() {
  // In full implementation, would extract exception information from V8
  has_exception_ = true;
  strncpy(exception_string_, "JavaScript execution error", 
          sizeof(exception_string_) - 1);
}

//
// AROSJavaScriptEngine Implementation
//

AROSJavaScriptEngine* AROSJavaScriptEngine::Create() {
  return new(std::nothrow) AROSJavaScriptEngine();
}

AROSJavaScriptEngine::AROSJavaScriptEngine()
    : context_(nullptr), initialized_(false), error_handler_(nullptr),
      console_output_enabled_(true), global_functions_(nullptr) {
  working_directory_[0] = '\0';
}

AROSJavaScriptEngine::~AROSJavaScriptEngine() {
  Shutdown();
}

bool AROSJavaScriptEngine::Initialize() {
  if (initialized_) return true;
  
  // Initialize isolate manager
  if (!AROSIsolateManager::Initialize()) {
    return false;
  }
  
  // Create execution context
  context_ = new(std::nothrow) AROSExecutionContext();
  if (!context_) {
    return false;
  }
  
  if (!context_->Initialize()) {
    delete context_;
    context_ = nullptr;
    return false;
  }
  
  // Register built-in functions
  RegisterBuiltinFunctions();
  
  initialized_ = true;
  return true;
}

void AROSJavaScriptEngine::Shutdown() {
  if (!initialized_) return;
  
  CleanupGlobalFunctions();
  
  if (context_) {
    delete context_;
    context_ = nullptr;
  }
  
  AROSIsolateManager::Shutdown();
  
  initialized_ = false;
}

bool AROSJavaScriptEngine::ExecuteScript(const char* source, const char* name) {
  if (!initialized_ || !context_) return false;
  
  if (console_output_enabled_) {
    printf("[AROS JS Engine] Executing: %s\n", name ? name : "<script>");
  }
  
  return context_->ExecuteString(source, name);
}

bool AROSJavaScriptEngine::LoadScript(const char* filename) {
  if (!initialized_ || !context_) return false;
  
  if (console_output_enabled_) {
    printf("[AROS JS Engine] Loading script: %s\n", filename);
  }
  
  return context_->ExecuteFile(filename);
}

bool AROSJavaScriptEngine::HasError() const {
  return context_ ? context_->HasException() : true;
}

const char* AROSJavaScriptEngine::GetErrorMessage() const {
  return context_ ? context_->GetExceptionString() : "Engine not initialized";
}

void AROSJavaScriptEngine::SetErrorHandler(void (*handler)(const char*)) {
  error_handler_ = handler;
  AROSIsolateManager::SetAROSErrorHandler(handler);
}

void AROSJavaScriptEngine::RequestGarbageCollection() {
  if (initialized_ && context_) {
    AROSIsolateManager::RequestGarbageCollection(context_->GetIsolate());
  }
}

size_t AROSJavaScriptEngine::GetMemoryUsage() {
  if (initialized_ && context_) {
    return AROSIsolateManager::GetHeapStatistics(context_->GetIsolate());
  }
  return 0;
}

void AROSJavaScriptEngine::EnableAROSConsoleOutput(bool enable) {
  console_output_enabled_ = enable;
}

void AROSJavaScriptEngine::SetAROSWorkingDirectory(const char* path) {
  if (path) {
    strncpy(working_directory_, path, sizeof(working_directory_) - 1);
    working_directory_[sizeof(working_directory_) - 1] = '\0';
  }
}

void AROSJavaScriptEngine::RegisterBuiltinFunctions() {
  // Phase 2.2: Register basic built-in functions
  // In full implementation, these would be actual V8 function templates
  
  if (console_output_enabled_) {
    printf("[AROS JS Engine] Registering built-in functions\n");
  }
}

void AROSJavaScriptEngine::CleanupGlobalFunctions() {
  GlobalFunction* current = global_functions_;
  while (current) {
    GlobalFunction* next = current->next;
    delete current;
    current = next;
  }
  global_functions_ = nullptr;
}

} // namespace internal

//
// Public API Implementation
//

namespace aros {

internal::AROSJavaScriptEngine* JavaScriptRunner::engine_ = nullptr;

bool JavaScriptRunner::Initialize() {
  if (engine_) return true;
  
  engine_ = internal::AROSJavaScriptEngine::Create();
  if (!engine_) return false;
  
  return engine_->Initialize();
}

void JavaScriptRunner::Shutdown() {
  if (engine_) {
    delete engine_;
    engine_ = nullptr;
  }
}

bool JavaScriptRunner::Run(const char* code, const char* name) {
  if (!engine_) return false;
  return engine_->ExecuteScript(code, name);
}

bool JavaScriptRunner::RunFile(const char* filename) {
  if (!engine_) return false;
  return engine_->LoadScript(filename);
}

bool JavaScriptRunner::HasError() {
  if (!engine_) return true;
  return engine_->HasError();
}

const char* JavaScriptRunner::GetError() {
  if (!engine_) return "Engine not initialized";
  return engine_->GetErrorMessage();
}

// Placeholder implementations for global variable access
void JavaScriptRunner::SetGlobal(const char* name, const char* value) {
  printf("[AROS JS] Set global %s = %s\n", name, value);
}

void JavaScriptRunner::SetGlobal(const char* name, int value) {
  printf("[AROS JS] Set global %s = %d\n", name, value);
}

void JavaScriptRunner::SetGlobal(const char* name, double value) {
  printf("[AROS JS] Set global %s = %f\n", name, value);
}

const char* JavaScriptRunner::GetGlobal(const char* name) {
  printf("[AROS JS] Get global %s\n", name);
  return "<undefined>"; // Placeholder
}

} // namespace aros
} // namespace v8