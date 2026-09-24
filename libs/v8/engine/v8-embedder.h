// REF_GITHUB_ISSUE: #112
// AROS_IMPL: Provides C-compatible interface to V8 C++ engine

#ifndef V8_AROS_EMBEDDER_H_
#define V8_AROS_EMBEDDER_H_

#include "v8-platform-backend.h"
#include "isolate-aros.h"

// Forward declaration for StringBuffer (implementation in .cc file)
// This avoids including AROS headers in this header file
#include <cstddef>
#include <cstring>

namespace v8 {
namespace aros {

//
// V8 Embedder for AROS
//
// This class provides a high-level interface for embedding V8 in AROS
// applications. It wraps the complex V8 C++ API in a simpler interface
// that can be called from the C-based v8.library.
//
class V8Embedder {
 public:
  // Initialize the V8 engine with AROS platform
  static bool Initialize();
  
  // Shutdown the V8 engine
  static void Shutdown();
  
  // Check if V8 is initialized
  static bool IsInitialized() { return initialized_; }
  
  // Get the platform instance
  static AROSV8Platform* GetPlatform() { return platform_; }
  
  //
  // Isolate Management
  //
  
  // Create a new V8 isolate
  // Returns opaque handle that can be passed to other functions
  static void* CreateIsolate();
  
  // Destroy an isolate and free all resources
  static void DestroyIsolate(void* isolate_handle);
  
  //
  // Context Management
  //
  
  // Create a new JavaScript context in an isolate
  static void* CreateContext(void* isolate_handle);
  
  // Destroy a context
  static void DestroyContext(void* isolate_handle, void* context_handle);
  
  // Enter a context (make it current for the isolate)
  static bool EnterContext(void* isolate_handle, void* context_handle);
  
  // Exit the current context
  static void ExitContext(void* isolate_handle, void* context_handle);
  
  //
  // Script Execution
  //
  
  // Evaluate JavaScript code and return result as string
  // Returns 0 on success, negative error code on failure
  static int EvaluateScript(void* isolate_handle,
                           void* context_handle,
                           const char* script,
                           const char* script_name,
                           char* result_buffer,
                           size_t result_buffer_size);
  
  // Compile JavaScript code to a script object
  static void* CompileScript(void* isolate_handle,
                            void* context_handle,
                            const char* script,
                            const char* script_name);
  
  // Run a compiled script
  static int RunScript(void* isolate_handle,
                      void* context_handle,
                      void* script_handle,
                      char* result_buffer,
                      size_t result_buffer_size);
  
  // Free a compiled script
  static void FreeScript(void* script_handle);
  
  //
  // Object Manipulation
  //
  
  // Get the global object of a context
  static void* GetGlobalObject(void* isolate_handle, void* context_handle);
  
  // Set a property on an object
  static int SetObjectProperty(void* isolate_handle,
                              void* context_handle,
                              void* object_handle,
                              const char* property_name,
                              const char* value_string);
  
  // Get a property from an object
  static int GetObjectProperty(void* isolate_handle,
                              void* context_handle,
                              void* object_handle,
                              const char* property_name,
                              char* result_buffer,
                              size_t result_buffer_size);
  
  // Call a function on an object
  static int CallFunction(void* isolate_handle,
                         void* context_handle,
                         void* object_handle,
                         const char* function_name,
                         const char** arguments,
                         int argument_count,
                         char* result_buffer,
                         size_t result_buffer_size);
  
  //
  // Error Handling
  //
  
  // Get the last error message
  static const char* GetLastError();
  
  // Clear the last error
  static void ClearLastError();
  
  //
  // Advanced Features
  //
  
  // Enable/disable JIT compilation
  static void SetJITEnabled(bool enabled);
  
  // Set memory limits for an isolate
  static void SetMemoryLimits(void* isolate_handle, 
                             size_t max_heap_size,
                             size_t max_stack_size);
  
  // Trigger garbage collection
  static void CollectGarbage(void* isolate_handle, bool full_gc);
  
  // Get memory usage statistics
  struct MemoryStats {
    size_t total_heap_size;
    size_t used_heap_size;
    size_t external_memory;
    size_t malloced_memory;
  };
  
  static bool GetMemoryStats(void* isolate_handle, MemoryStats* stats);
  
 private:
  // Private constructor - this is a static-only class
  V8Embedder() = delete;
  
  // Helper functions for V8 value conversions
  static bool ConvertV8ValueToString(void* value_handle,
                                     char* buffer,
                                     size_t buffer_size);
  
  static void* ConvertStringToV8Value(void* isolate_handle,
                                     void* context_handle,
                                     const char* string);
  
  // Error handling
  static void SetLastError(const char* error_message);
  
  // State
  static bool initialized_;
  static AROSV8Platform* platform_;
  static char last_error_[1024];
  static bool jit_enabled_;
};

//
// V8 Embedder Helper Classes
//

// RAII wrapper for entering/exiting contexts
class ScopedContext {
 public:
  ScopedContext(void* isolate_handle, void* context_handle)
      : isolate_(isolate_handle), context_(context_handle), entered_(false) {
    entered_ = V8Embedder::EnterContext(isolate_, context_);
  }
  
  ~ScopedContext() {
    if (entered_) {
      V8Embedder::ExitContext(isolate_, context_);
    }
  }
  
  bool IsEntered() const { return entered_; }
  
 private:
  void* isolate_;
  void* context_;
  bool entered_;
  
  // Prevent copying
  ScopedContext(const ScopedContext&) = delete;
  ScopedContext& operator=(const ScopedContext&) = delete;
};

// Simple string buffer helper
// Note: Implementation uses AROS AllocVec/FreeVec, defined in .cc file
class StringBuffer {
 public:
  explicit StringBuffer(size_t initial_capacity = 256);
  ~StringBuffer();
  
  bool Append(const char* str);
  
  const char* GetString() const { return buffer_; }
  size_t GetSize() const { return size_; }
  
  void Clear();
  
 private:
  char* buffer_;
  size_t capacity_;
  size_t size_;
  
  // Prevent copying
  StringBuffer(const StringBuffer&) = delete;
  StringBuffer& operator=(const StringBuffer&) = delete;
};

}  // namespace aros
}  // namespace v8

#endif  // V8_AROS_EMBEDDER_H_
