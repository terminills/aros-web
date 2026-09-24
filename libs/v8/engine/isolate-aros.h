// REF_GITHUB_ISSUE: #112
// AROS_IMPL: Integrates V8 isolate lifecycle with AROS memory and threading systems

#ifndef V8_ISOLATE_AROS_H_
#define V8_ISOLATE_AROS_H_

#include "platform-aros.h"
#include "memory-aros.h"
#include "threading-aros.h"

#include <stdint.h>
#include <stddef.h>

namespace v8 {

// Forward declarations for V8 core types
class Isolate;
class Context;
class Value;
class String;
class Script;
class Platform;

namespace internal {

// AROS-specific V8 isolate manager
class AROSIsolateManager {
 public:
  // Isolate lifecycle
  static bool Initialize();
  static void Shutdown();
  
  // Isolate creation and management
  static Isolate* CreateIsolate();
  static void DisposeIsolate(Isolate* isolate);
  
  // Context management
  static Context* CreateContext(Isolate* isolate);
  static void DisposeContext(Context* context);
  
  // JavaScript execution
  static bool ExecuteScript(Isolate* isolate, Context* context, 
                           const char* source, const char* name = nullptr);
  static String* CreateString(Isolate* isolate, const char* data, int length = -1);
  
  // Error handling
  static void ReportException(Isolate* isolate, const char* exception);
  static const char* GetLastError() { return last_error_; }
  
  // Resource management
  static size_t GetHeapStatistics(Isolate* isolate);
  static void RequestGarbageCollection(Isolate* isolate);
  
  // AROS-specific functionality
  static void SetAROSErrorHandler(void (*handler)(const char*));
  static bool IsAROSPlatformReady() { return platform_ready_; }
  
 private:
  static Platform* platform_;
  static bool platform_ready_;
  static void (*error_handler_)(const char*);
  static char last_error_[512];
  
  // Internal helpers
  static void InitializeV8Platform();
  static void ConfigureIsolateParams(void* create_params);
  static void HandleV8Message(int level, const char* message);
};

// AROS V8 execution context wrapper
class AROSExecutionContext {
 public:
  AROSExecutionContext();
  ~AROSExecutionContext();
  
  // Initialize execution environment
  bool Initialize();
  void Cleanup();
  
  // JavaScript execution
  bool ExecuteString(const char* source, const char* name = nullptr);
  bool ExecuteFile(const char* filename);
  
  // Value conversion helpers
  const char* ValueToString(Value* value);
  int32_t ValueToInt32(Value* value);
  double ValueToNumber(Value* value);
  bool ValueToBoolean(Value* value);
  
  // Error handling
  bool HasException() const { return has_exception_; }
  const char* GetExceptionString() const { return exception_string_; }
  void ClearException();
  
  // Context access
  Isolate* GetIsolate() const { return isolate_; }
  Context* GetContext() const { return context_; }
  
 private:
  Isolate* isolate_;
  Context* context_;
  bool initialized_;
  bool has_exception_;
  char exception_string_[256];
  
  void HandleException();
};

// Simple V8 JavaScript engine wrapper for AROS
class AROSJavaScriptEngine {
 public:
  static AROSJavaScriptEngine* Create();
  ~AROSJavaScriptEngine();
  
  // Engine lifecycle
  bool Initialize();
  void Shutdown();
  
  // Script execution
  bool ExecuteScript(const char* source, const char* name = nullptr);
  bool LoadScript(const char* filename);
  
  // Global object manipulation
  bool SetGlobalProperty(const char* name, const char* value);
  bool SetGlobalProperty(const char* name, int32_t value);
  bool SetGlobalProperty(const char* name, double value);
  const char* GetGlobalProperty(const char* name);
  
  // Built-in functions
  void RegisterGlobalFunction(const char* name, void (*callback)(void*));
  
  // Error handling and debugging
  bool HasError() const;
  const char* GetErrorMessage() const;
  void SetErrorHandler(void (*handler)(const char*));
  
  // Memory and performance
  void RequestGarbageCollection();
  size_t GetMemoryUsage();
  
  // AROS-specific features
  void EnableAROSConsoleOutput(bool enable);
  void SetAROSWorkingDirectory(const char* path);
  
 private:
  AROSJavaScriptEngine();
  
  AROSExecutionContext* context_;
  bool initialized_;
  void (*error_handler_)(const char*);
  bool console_output_enabled_;
  char working_directory_[256];
  
  // Built-in function support
  struct GlobalFunction {
    char name[64];
    void (*callback)(void*);
    GlobalFunction* next;
  };
  GlobalFunction* global_functions_;
  
  void RegisterBuiltinFunctions();
  void CleanupGlobalFunctions();
};

} // namespace internal

// Public API for AROS applications
namespace aros {

// Simple JavaScript execution API
class JavaScriptRunner {
 public:
  static bool Initialize();
  static void Shutdown();
  
  // Execute JavaScript code
  static bool Run(const char* code, const char* name = nullptr);
  static bool RunFile(const char* filename);
  
  // Error handling
  static bool HasError();
  static const char* GetError();
  
  // Global variables
  static void SetGlobal(const char* name, const char* value);
  static void SetGlobal(const char* name, int value);
  static void SetGlobal(const char* name, double value);
  static const char* GetGlobal(const char* name);
  
 private:
  static internal::AROSJavaScriptEngine* engine_;
};

} // namespace aros
} // namespace v8

#endif // V8_ISOLATE_AROS_H_