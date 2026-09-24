// REF_GITHUB_ISSUE: #112
// AROS_IMPL: Provides framework for V8 C++ API integration with fallback

#include "v8-embedder.h"

#include <exec/exec.h>
#include <proto/exec.h>
#include <proto/arossupport.h>  /* kprintf */
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <memory>

// Undefine AROS macros that conflict with V8 method names
// AROS exec.h defines these as macros, but V8 uses them as method names
#ifdef Allocate
#undef Allocate
#endif
#ifdef Exception
#undef Exception
#endif

// Conditionally include V8 headers when V8 library is available
#ifdef V8_USE_REAL_ENGINE
// The makefile supplies Chromium's pinned v8/include directory.
#include <v8.h>
#include <libplatform/libplatform.h>
#endif

namespace v8 {
namespace aros {

// Static member initialization
bool V8Embedder::initialized_ = false;
AROSV8Platform* V8Embedder::platform_ = nullptr;
char V8Embedder::last_error_[1024] = {0};
bool V8Embedder::jit_enabled_ = true;

#ifdef V8_USE_REAL_ENGINE
// Real V8 platform and allocator (only when V8 is available)
static std::unique_ptr<v8::Platform> v8_platform = nullptr;
static std::unique_ptr<v8::ArrayBuffer::Allocator> array_buffer_allocator = nullptr;

/*
 * V8 12+ permanently forbids InitializePlatform after DisposePlatform in
 * the same loaded image (V8StartupState CHECK - boot 174248: second test
 * program died at step 4 with "current_state != kPlatformDisposed").
 * v8.library persists across openers, so process-global V8 init happens
 * ONCE per library load and is NEVER disposed; only library expunge
 * (which unloads the image, giving the next load fresh statics) truly
 * ends it.  Per-opener state (AROSV8Platform wrapper: its timer port is
 * task-affine) is still torn down and rebuilt per Initialize/Shutdown.
 */
static bool v8_process_initialized = false;
#endif

//
// V8Embedder Implementation
//

bool V8Embedder::Initialize() {
  if (initialized_) {
    return true;
  }
  
#ifdef V8_USE_REAL_ENGINE
  // Real V8 initialization
  try {
    if (!v8_process_initialized) {
    kprintf("[V8-INIT] step 1: InitializeICUDefaultLocation\n");
    v8::V8::InitializeICUDefaultLocation("");
    kprintf("[V8-INIT] step 2: InitializeExternalStartupData\n");
    v8::V8::InitializeExternalStartupData("");

    kprintf("[V8-INIT] step 3: NewDefaultPlatform (creates worker threads)\n");
    v8_platform = v8::platform::NewDefaultPlatform();
    if (!v8_platform) {
      SetLastError("Initialize: Failed to create V8 platform");
      return false;
    }
    kprintf("[V8-INIT] step 3 done: platform=%p\n", v8_platform.get());

    // Initialize V8 with the platform
    kprintf("[V8-INIT] step 4: InitializePlatform + Initialize\n");
    v8::V8::InitializePlatform(v8_platform.get());
    v8::V8::Initialize();
    kprintf("[V8-INIT] step 4 done\n");
    v8_process_initialized = true;
    } else {
      kprintf("[V8-INIT] steps 1-4 skipped: V8 already live in this library image\n");
    }
    
    // Create AROS platform wrapper for additional functionality
    kprintf("[V8-INIT] step 5: new AROSV8Platform\n");
    platform_ = new AROSV8Platform();
    if (!platform_) {
      SetLastError("Initialize: Failed to create AROS V8 platform wrapper");
      /* keep process-global V8 alive: re-init after DisposePlatform is
       * impossible in this image; a later Initialize() can still succeed */
      return false;
    }
    kprintf("[V8-INIT] step 5 done: platform_=%p\n", platform_);
    
    kprintf("[V8-INIT] step 6: platform_->Initialize()\n");
    if (!platform_->Initialize()) {
      kprintf("[V8-INIT] step 6 FAILED\n");
      SetLastError("Initialize: Failed to initialize AROS V8 platform wrapper");
      delete platform_;
      platform_ = nullptr;
      /* keep process-global V8 alive: re-init after DisposePlatform is
       * impossible in this image; a later Initialize() can still succeed */
      return false;
    }
    kprintf("[V8-INIT] step 6 done\n");
    
    // Create array buffer allocator (required for isolates)
    kprintf("[V8-INIT] step 7: NewDefaultAllocator\n");
    array_buffer_allocator.reset(v8::ArrayBuffer::Allocator::NewDefaultAllocator());
    if (!array_buffer_allocator) {
      kprintf("[V8-INIT] step 7 FAILED\n");
      SetLastError("Initialize: Failed to create array buffer allocator");
      if (platform_) {
        platform_->Shutdown();
        delete platform_;
        platform_ = nullptr;
      }
      /* keep process-global V8 alive: re-init after DisposePlatform is
       * impossible in this image; a later Initialize() can still succeed */
      return false;
    }
    
    kprintf("[V8-INIT] Initialize() COMPLETE\n");
    initialized_ = true;
    ClearLastError();  // Success
    return true;
  }
  catch (const std::exception& e) {
    char error_msg[512];
    // Safely concatenate exception message to avoid format string vulnerabilities
    strncpy(error_msg, "Initialize: Exception during V8 initialization: ", sizeof(error_msg) - 1);
    error_msg[sizeof(error_msg) - 1] = '\0';
    size_t len = strlen(error_msg);
    if (len < sizeof(error_msg) - 1) {
      strncat(error_msg, e.what() ? e.what() : "unknown", sizeof(error_msg) - len - 1);
      error_msg[sizeof(error_msg) - 1] = '\0';
    }
    SetLastError(error_msg);
    
    // Cleanup on exception
    array_buffer_allocator.reset();
    if (platform_) {
      platform_->Shutdown();
      delete platform_;
      platform_ = nullptr;
    }
    /* keep process-global V8 alive (see v8_process_initialized) */
    return false;
  }
  catch (...) {
    SetLastError("Initialize: Unknown exception during V8 initialization");
    
    // Cleanup on exception
    array_buffer_allocator.reset();
    if (platform_) {
      platform_->Shutdown();
      delete platform_;
      platform_ = nullptr;
    }
    /* keep process-global V8 alive (see v8_process_initialized) */
    return false;
  }
#else
  // Fallback to stub implementation (Phase 2.2)
  platform_ = new AROSV8Platform();
  if (!platform_) {
    SetLastError("Initialize: Failed to create V8 platform");
    return false;
  }
  
  if (!platform_->Initialize()) {
    SetLastError("Initialize: Failed to initialize V8 platform");
    delete platform_;
    platform_ = nullptr;
    return false;
  }
  
  initialized_ = true;
  ClearLastError();  // Success
  return true;
#endif
}

void V8Embedder::Shutdown() {
  if (!initialized_) {
    return;
  }
  
#ifdef V8_USE_REAL_ENGINE
  // Real V8 shutdown
  try {
    // Dispose of array buffer allocator
    array_buffer_allocator.reset();
    
    // Shutdown AROS platform wrapper
    if (platform_) {
      platform_->Shutdown();
      delete platform_;
      platform_ = nullptr;
    }
    
    /* Process-global V8 (v8_platform, V8::Initialize state) intentionally
     * stays live: V8 cannot be re-initialized after DisposePlatform in the
     * same loaded image, and v8.library outlives individual openers. */
  }
  catch (...) {
    // Ignore exceptions during shutdown
  }
#else
  // Fallback shutdown
  if (platform_) {
    platform_->Shutdown();
    delete platform_;
    platform_ = nullptr;
  }
#endif
  
  initialized_ = false;
}

//
// Isolate Management
//

void* V8Embedder::CreateIsolate() {
  if (!initialized_) {
    SetLastError("CreateIsolate: V8 not initialized - call V8Initialize() first");
    return nullptr;
  }
  
#ifdef V8_USE_REAL_ENGINE
  // Real V8 isolate creation
  try {
    if (!array_buffer_allocator) {
      SetLastError("CreateIsolate: Array buffer allocator not available");
      return nullptr;
    }
    
    kprintf("[V8-ISOLATE] CreateIsolate: calling Isolate::New\n");
    
    // Create isolate with default parameters
    v8::Isolate::CreateParams create_params;
    create_params.array_buffer_allocator = array_buffer_allocator.get();
    
    v8::Isolate* isolate = v8::Isolate::New(create_params);
    if (!isolate) {
      kprintf("[V8-ISOLATE] Isolate::New returned NULL\n");
      SetLastError("CreateIsolate: Failed to allocate V8 isolate - out of memory");
      return nullptr;
    }
    
    kprintf("[V8-ISOLATE] isolate=%p SUCCESS\n", isolate);
    ClearLastError();  // Success
    return static_cast<void*>(isolate);
  }
  catch (const std::exception& e) {
    char error_msg[512];
    // Safely concatenate exception message
    strncpy(error_msg, "CreateIsolate: Exception: ", sizeof(error_msg) - 1);
    error_msg[sizeof(error_msg) - 1] = '\0';
    size_t len = strlen(error_msg);
    if (len < sizeof(error_msg) - 1) {
      strncat(error_msg, e.what() ? e.what() : "unknown", sizeof(error_msg) - len - 1);
      error_msg[sizeof(error_msg) - 1] = '\0';
    }
    SetLastError(error_msg);
    return nullptr;
  }
  catch (...) {
    SetLastError("CreateIsolate: Unknown exception during isolate creation");
    return nullptr;
  }
#else
  // Fallback to Phase 2.2 implementation
  void* isolate = v8::internal::AROSIsolateManager::CreateIsolate();
  if (!isolate) {
    SetLastError("CreateIsolate: Failed to create isolate - out of memory or resource limit reached");
    return nullptr;
  }
  
  ClearLastError();  // Success
  return isolate;
#endif
}

void V8Embedder::DestroyIsolate(void* isolate_handle) {
  if (!isolate_handle) {
    return;
  }
  
#ifdef V8_USE_REAL_ENGINE
  // Real V8 isolate disposal
  try {
    v8::Isolate* isolate = static_cast<v8::Isolate*>(isolate_handle);
    isolate->Dispose();
  }
  catch (...) {
    // Ignore exceptions during cleanup
  }
#else
  // Fallback disposal
  v8::internal::AROSIsolateManager::DisposeIsolate(static_cast<v8::Isolate*>(isolate_handle));
#endif
}

//
// Context Management
//

void* V8Embedder::CreateContext(void* isolate_handle) {
  if (!isolate_handle) {
    SetLastError("CreateContext: isolate_handle is NULL - call V8CreateIsolate() first");
    return nullptr;
  }
  
#ifdef V8_USE_REAL_ENGINE
  // Real V8 context creation
  try {
    v8::Isolate* isolate = static_cast<v8::Isolate*>(isolate_handle);
    
    // Enter isolate scope
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    
    // Create a new context
    v8::Local<v8::Context> context = v8::Context::New(isolate);
    if (context.IsEmpty()) {
      SetLastError("CreateContext: Failed to create V8 context - out of memory");
      return nullptr;
    }
    
    // Create a persistent handle to keep the context alive
    v8::Persistent<v8::Context>* persistent_context = 
        new v8::Persistent<v8::Context>(isolate, context);
    
    ClearLastError();  // Success
    return static_cast<void*>(persistent_context);
  }
  catch (const std::exception& e) {
    char error_msg[512];
    // Safely concatenate exception message
    strncpy(error_msg, "CreateContext: Exception: ", sizeof(error_msg) - 1);
    error_msg[sizeof(error_msg) - 1] = '\0';
    size_t len = strlen(error_msg);
    if (len < sizeof(error_msg) - 1) {
      strncat(error_msg, e.what() ? e.what() : "unknown", sizeof(error_msg) - len - 1);
      error_msg[sizeof(error_msg) - 1] = '\0';
    }
    SetLastError(error_msg);
    return nullptr;
  }
  catch (...) {
    SetLastError("CreateContext: Unknown exception during context creation");
    return nullptr;
  }
#else
  // Fallback to Phase 2.2
  void* context = v8::internal::AROSIsolateManager::CreateContext(static_cast<v8::Isolate*>(isolate_handle));
  if (!context) {
    SetLastError("CreateContext: Failed to create context - out of memory or resource limit reached");
    return nullptr;
  }
  
  ClearLastError();  // Success
  return context;
#endif
}

void V8Embedder::DestroyContext(void* isolate_handle, void* context_handle) {
  if (!context_handle) {
    return;
  }
  
#ifdef V8_USE_REAL_ENGINE
  // Real V8 context disposal
  try {
    v8::Persistent<v8::Context>* persistent_context = 
        static_cast<v8::Persistent<v8::Context>*>(context_handle);
    
    // Reset the persistent handle
    persistent_context->Reset();
    delete persistent_context;
  }
  catch (...) {
    // Ignore exceptions during cleanup
  }
#else
  // Fallback disposal
  v8::internal::AROSIsolateManager::DisposeContext(static_cast<v8::Context*>(context_handle));
#endif
}

bool V8Embedder::EnterContext(void* isolate_handle, void* context_handle) {
  if (!isolate_handle || !context_handle) {
    SetLastError("Invalid isolate or context");
    return false;
  }
  
  // TODO: When V8 is integrated:
  // v8::Isolate* isolate = static_cast<v8::Isolate*>(isolate_handle);
  // v8::Persistent<v8::Context>* persistent = static_cast<v8::Persistent<v8::Context>*>(context_handle);
  // v8::Local<v8::Context> context = persistent->Get(isolate);
  // context->Enter();
  
  return true;
}

void V8Embedder::ExitContext(void* isolate_handle, void* context_handle) {
  if (!isolate_handle || !context_handle) {
    return;
  }
  
  // TODO: When V8 is integrated:
  // v8::Isolate* isolate = static_cast<v8::Isolate*>(isolate_handle);
  // v8::Persistent<v8::Context>* persistent = static_cast<v8::Persistent<v8::Context>*>(context_handle);
  // v8::Local<v8::Context> context = persistent->Get(isolate);
  // context->Exit();
}

//
// Script Execution
//

int V8Embedder::EvaluateScript(void* isolate_handle,
                              void* context_handle,
                              const char* script,
                              const char* script_name,
                              char* result_buffer,
                              size_t result_buffer_size) {
  // Enhanced parameter validation with detailed error messages
  if (!isolate_handle) {
    SetLastError("EvaluateScript: isolate_handle is NULL - call V8Initialize() and V8CreateIsolate() first");
    return -1;
  }
  
  if (!context_handle) {
    SetLastError("EvaluateScript: context_handle is NULL - call V8CreateContext() first");
    return -1;
  }
  
  if (!script) {
    SetLastError("EvaluateScript: script is NULL - provide valid JavaScript code");
    return -1;
  }
  
  if (!script[0]) {
    SetLastError("EvaluateScript: script is empty - provide non-empty JavaScript code");
    return -2;
  }
  
  // Store script name for error reporting
  const char* name_for_errors = script_name ? script_name : "<anonymous>";
  
#ifdef V8_USE_REAL_ENGINE
  // Real V8 script evaluation
  try {
    v8::Isolate* isolate = static_cast<v8::Isolate*>(isolate_handle);
    v8::Persistent<v8::Context>* persistent_context = 
        static_cast<v8::Persistent<v8::Context>*>(context_handle);
    
    // Enter isolate and context
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = persistent_context->Get(isolate);
    v8::Context::Scope context_scope(context);
    
    // Create a try-catch block for error handling
    v8::TryCatch try_catch(isolate);
    
    // Create V8 string for the script source
    v8::Local<v8::String> source;
    if (!v8::String::NewFromUtf8(isolate, script, v8::NewStringType::kNormal)
             .ToLocal(&source)) {
      SetLastError("EvaluateScript: Failed to create source string");
      return -2;
    }
    
    // Create V8 string for the script name (origin)
    v8::Local<v8::String> name;
    if (!v8::String::NewFromUtf8(isolate, name_for_errors, v8::NewStringType::kNormal)
             .ToLocal(&name)) {
      SetLastError("EvaluateScript: Failed to create script name");
      return -2;
    }
    
    // Create script origin for better error messages
    v8::ScriptOrigin origin(isolate, name);
    
    // Compile the script
    v8::Local<v8::Script> compiled_script;
    if (!v8::Script::Compile(context, source, &origin).ToLocal(&compiled_script)) {
      // Compilation error
      if (try_catch.HasCaught()) {
        v8::String::Utf8Value error(isolate, try_catch.Exception());
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Compilation error in %s: %s", 
                 name_for_errors, *error ? *error : "unknown error");
        SetLastError(error_msg);
      } else {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Script compilation failed in %s", name_for_errors);
        SetLastError(error_msg);
      }
      return -3;  // Compile error
    }
    
    // Run the script
    v8::Local<v8::Value> result;
    if (!compiled_script->Run(context).ToLocal(&result)) {
      // Runtime error
      if (try_catch.HasCaught()) {
        v8::String::Utf8Value error(isolate, try_catch.Exception());
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Runtime error in %s: %s", 
                 name_for_errors, *error ? *error : "unknown error");
        SetLastError(error_msg);
      } else {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Script execution failed in %s", name_for_errors);
        SetLastError(error_msg);
      }
      return -4;  // Runtime error
    }
    
    // Convert result to string if buffer provided
    if (result_buffer && result_buffer_size > 0) {
      v8::String::Utf8Value utf8_result(isolate, result);
      if (*utf8_result) {
        strncpy(result_buffer, *utf8_result, result_buffer_size - 1);
        result_buffer[result_buffer_size - 1] = '\0';
      } else {
        result_buffer[0] = '\0';
      }
    }
    
    ClearLastError();  // Success
    return 0;
  }
  catch (const std::exception& e) {
    char error_msg[512];
    // Safely concatenate script name and exception message
    strncpy(error_msg, "EvaluateScript: Exception in ", sizeof(error_msg) - 1);
    error_msg[sizeof(error_msg) - 1] = '\0';
    size_t len = strlen(error_msg);
    if (len < sizeof(error_msg) - 3) {
      strncat(error_msg, name_for_errors, sizeof(error_msg) - len - 3);
      strcat(error_msg, ": ");
      len = strlen(error_msg);
      if (len < sizeof(error_msg) - 1) {
        strncat(error_msg, e.what() ? e.what() : "unknown", sizeof(error_msg) - len - 1);
      }
      error_msg[sizeof(error_msg) - 1] = '\0';
    }
    SetLastError(error_msg);
    return -4;
  }
  catch (...) {
    char error_msg[256];
    strncpy(error_msg, "EvaluateScript: Unknown exception in ", sizeof(error_msg) - 1);
    error_msg[sizeof(error_msg) - 1] = '\0';
    size_t len = strlen(error_msg);
    if (len < sizeof(error_msg) - 1) {
      strncat(error_msg, name_for_errors, sizeof(error_msg) - len - 1);
      error_msg[sizeof(error_msg) - 1] = '\0';
    }
    SetLastError(error_msg);
    return -4;
  }
#else
  // Fallback to Phase 2.2 implementation
  bool success = v8::internal::AROSIsolateManager::ExecuteScript(
                     static_cast<v8::Isolate*>(isolate_handle), 
                     static_cast<v8::Context*>(context_handle), 
                     script);
  
  if (!success) {
    char error_msg[256];
    snprintf(error_msg, sizeof(error_msg), 
             "Script execution failed in %s", 
             name_for_errors);
    SetLastError(error_msg);
  }
  
  if (result_buffer && result_buffer_size > 0) {
    strncpy(result_buffer, success ? "true" : "false", result_buffer_size - 1);
    result_buffer[result_buffer_size - 1] = '\0';
  }
  
  return success ? 0 : -4;
#endif
}

void* V8Embedder::CompileScript(void* isolate_handle,
                               void* context_handle,
                               const char* script,
                               const char* script_name) {
  // TODO: When V8 is integrated:
  // Similar to EvaluateScript but return compiled script
  
  // For now, just store the script source
  if (!script) {
    return nullptr;
  }
  
  char* script_copy = (char*)AllocVec(strlen(script) + 1, MEMF_PUBLIC | MEMF_CLEAR);
  if (script_copy) {
    strcpy(script_copy, script);
  }
  
  return script_copy;
}

int V8Embedder::RunScript(void* isolate_handle,
                         void* context_handle,
                         void* script_handle,
                         char* result_buffer,
                         size_t result_buffer_size) {
  if (!script_handle) {
    SetLastError("Invalid script");
    return -1;
  }
  
  // TODO: When V8 is integrated:
  // Run the compiled script
  
  // For now, just evaluate the stored source
  const char* script = (const char*)script_handle;
  return EvaluateScript(isolate_handle, context_handle, script, "script", 
                       result_buffer, result_buffer_size);
}

void V8Embedder::FreeScript(void* script_handle) {
  if (script_handle) {
    FreeVec(script_handle);
  }
}

//
// Object Manipulation
//

#ifdef V8_USE_REAL_ENGINE
/*
 * Resolve an object handle to a Local<Object>.  Handles are either the
 * context handle itself (legacy placeholder convention, still used by
 * V8Bridge_CallFunction's object==NULL path) meaning "the global
 * object", or a heap-allocated Persistent<Object>* as returned by
 * GetGlobalObject().
 */
static v8::Local<v8::Object> resolve_object(v8::Isolate* isolate,
                                            v8::Local<v8::Context> context,
                                            void* object_handle,
                                            void* context_handle) {
  if (object_handle == context_handle)
    return context->Global();
  return static_cast<v8::Persistent<v8::Object>*>(object_handle)
      ->Get(isolate);
}
#endif

void* V8Embedder::GetGlobalObject(void* isolate_handle, void* context_handle) {
#ifdef V8_USE_REAL_ENGINE
  if (!isolate_handle || !context_handle)
    return nullptr;
  try {
    v8::Isolate* isolate = static_cast<v8::Isolate*>(isolate_handle);
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context =
        static_cast<v8::Persistent<v8::Context>*>(context_handle)
            ->Get(isolate);
    v8::Context::Scope context_scope(context);
    /* Heap Persistent so the handle survives this scope.  Lifetime is
     * tied to the context (freed implicitly at context destruction /
     * library expunge) - one leak-free-enough handle per context. */
    return new v8::Persistent<v8::Object>(isolate, context->Global());
  }
  catch (...) {
    SetLastError("GetGlobalObject: exception");
    return nullptr;
  }
#else
  // For now, return a placeholder
  return context_handle;
#endif
}

int V8Embedder::SetObjectProperty(void* isolate_handle,
                                 void* context_handle,
                                 void* object_handle,
                                 const char* property_name,
                                 const char* value_string) {
  // Enhanced parameter validation
  if (!isolate_handle) {
    SetLastError("SetObjectProperty: isolate_handle is NULL");
    return -1;
  }
  
  if (!context_handle) {
    SetLastError("SetObjectProperty: context_handle is NULL");
    return -1;
  }
  
  if (!object_handle) {
    SetLastError("SetObjectProperty: object_handle is NULL");
    return -1;
  }
  
  if (!property_name || !property_name[0]) {
    SetLastError("SetObjectProperty: property_name is NULL or empty");
    return -1;
  }
  
  if (!value_string) {
    SetLastError("SetObjectProperty: value_string is NULL");
    return -1;
  }
  
#ifdef V8_USE_REAL_ENGINE
  try {
    v8::Isolate* isolate = static_cast<v8::Isolate*>(isolate_handle);
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context =
        static_cast<v8::Persistent<v8::Context>*>(context_handle)
            ->Get(isolate);
    v8::Context::Scope context_scope(context);
    v8::Local<v8::Object> object =
        resolve_object(isolate, context, object_handle, context_handle);

    v8::Local<v8::String> key, value;
    if (!v8::String::NewFromUtf8(isolate, property_name,
                                 v8::NewStringType::kNormal).ToLocal(&key) ||
        !v8::String::NewFromUtf8(isolate, value_string,
                                 v8::NewStringType::kNormal).ToLocal(&value)) {
      SetLastError("SetObjectProperty: string creation failed");
      return -2;
    }
    if (!object->Set(context, key, value).FromMaybe(false)) {
      SetLastError("SetObjectProperty: Failed to set property");
      return -2;
    }
  }
  catch (...) {
    SetLastError("SetObjectProperty: exception");
    return -2;
  }
#endif

  ClearLastError();  // Success
  return 0;
}

int V8Embedder::GetObjectProperty(void* isolate_handle,
                                 void* context_handle,
                                 void* object_handle,
                                 const char* property_name,
                                 char* result_buffer,
                                 size_t result_buffer_size) {
  // Enhanced parameter validation
  if (!isolate_handle) {
    SetLastError("GetObjectProperty: isolate_handle is NULL");
    return -1;
  }
  
  if (!context_handle) {
    SetLastError("GetObjectProperty: context_handle is NULL");
    return -1;
  }
  
  if (!object_handle) {
    SetLastError("GetObjectProperty: object_handle is NULL");
    return -1;
  }
  
  if (!property_name || !property_name[0]) {
    SetLastError("GetObjectProperty: property_name is NULL or empty");
    return -1;
  }
  
  if (!result_buffer || result_buffer_size == 0) {
    SetLastError("GetObjectProperty: result_buffer is NULL or size is 0");
    return -1;
  }
  
#ifdef V8_USE_REAL_ENGINE
  try {
    v8::Isolate* isolate = static_cast<v8::Isolate*>(isolate_handle);
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context =
        static_cast<v8::Persistent<v8::Context>*>(context_handle)
            ->Get(isolate);
    v8::Context::Scope context_scope(context);
    v8::Local<v8::Object> object =
        resolve_object(isolate, context, object_handle, context_handle);

    v8::Local<v8::String> key;
    if (!v8::String::NewFromUtf8(isolate, property_name,
                                 v8::NewStringType::kNormal).ToLocal(&key)) {
      SetLastError("GetObjectProperty: string creation failed");
      result_buffer[0] = '\0';
      return -2;
    }
    v8::Local<v8::Value> value;
    if (!object->Get(context, key).ToLocal(&value)) {
      SetLastError("GetObjectProperty: Failed to get property");
      result_buffer[0] = '\0';
      return -2;
    }
    v8::String::Utf8Value utf8_value(isolate, value);
    if (*utf8_value) {
      strncpy(result_buffer, *utf8_value, result_buffer_size - 1);
      result_buffer[result_buffer_size - 1] = '\0';
    } else {
      result_buffer[0] = '\0';
    }
  }
  catch (...) {
    SetLastError("GetObjectProperty: exception");
    result_buffer[0] = '\0';
    return -2;
  }
#else
  // For now, return empty string
  result_buffer[0] = '\0';
#endif

  ClearLastError();  // Success
  return 0;
}

int V8Embedder::CallFunction(void* isolate_handle,
                            void* context_handle,
                            void* object_handle,
                            const char* function_name,
                            const char** arguments,
                            int argument_count,
                            char* result_buffer,
                            size_t result_buffer_size) {
  // Enhanced parameter validation
  if (!isolate_handle) {
    SetLastError("CallFunction: isolate_handle is NULL");
    return -1;
  }
  
  if (!context_handle) {
    SetLastError("CallFunction: context_handle is NULL");
    return -1;
  }
  
  if (!object_handle) {
    SetLastError("CallFunction: object_handle is NULL");
    return -1;
  }
  
  if (!function_name || !function_name[0]) {
    SetLastError("CallFunction: function_name is NULL or empty");
    return -1;
  }
  
  if (argument_count < 0) {
    SetLastError("CallFunction: argument_count is negative");
    return -1;
  }
  
  if (argument_count > 0 && !arguments) {
    SetLastError("CallFunction: arguments is NULL but argument_count > 0");
    return -1;
  }
  
  if (!result_buffer || result_buffer_size == 0) {
    SetLastError("CallFunction: result_buffer is NULL or size is 0");
    return -1;
  }
  
#ifdef V8_USE_REAL_ENGINE
  try {
    v8::Isolate* isolate = static_cast<v8::Isolate*>(isolate_handle);
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context =
        static_cast<v8::Persistent<v8::Context>*>(context_handle)
            ->Get(isolate);
    v8::Context::Scope context_scope(context);
    v8::Local<v8::Object> object =
        resolve_object(isolate, context, object_handle, context_handle);

    v8::Local<v8::String> key;
    if (!v8::String::NewFromUtf8(isolate, function_name,
                                 v8::NewStringType::kNormal).ToLocal(&key)) {
      SetLastError("CallFunction: string creation failed");
      result_buffer[0] = '\0';
      return -2;
    }
    v8::Local<v8::Value> func_value;
    if (!object->Get(context, key).ToLocal(&func_value) ||
        !func_value->IsFunction()) {
      SetLastError("CallFunction: Property is not a function");
      result_buffer[0] = '\0';
      return -2;
    }
    v8::Local<v8::Function> func = v8::Local<v8::Function>::Cast(func_value);

    /* string arguments only - the C API's convention */
    v8::Local<v8::Value> args_local[8];
    if (argument_count > 8)
      argument_count = 8;
    for (int i = 0; i < argument_count; i++) {
      v8::Local<v8::String> arg;
      if (!v8::String::NewFromUtf8(isolate, arguments[i],
                                   v8::NewStringType::kNormal).ToLocal(&arg)) {
        SetLastError("CallFunction: argument string creation failed");
        result_buffer[0] = '\0';
        return -2;
      }
      args_local[i] = arg;
    }

    v8::TryCatch try_catch(isolate);
    v8::Local<v8::Value> result;
    if (!func->Call(context, object, argument_count, args_local)
             .ToLocal(&result)) {
      if (try_catch.HasCaught()) {
        v8::String::Utf8Value error(isolate, try_catch.Exception());
        SetLastError(*error ? *error : "CallFunction: runtime error");
      } else {
        SetLastError("CallFunction: call failed");
      }
      result_buffer[0] = '\0';
      return -3;
    }
    v8::String::Utf8Value utf8_result(isolate, result);
    if (*utf8_result) {
      strncpy(result_buffer, *utf8_result, result_buffer_size - 1);
      result_buffer[result_buffer_size - 1] = '\0';
    } else {
      result_buffer[0] = '\0';
    }
    ClearLastError();
    return 0;
  }
  catch (...) {
    SetLastError("CallFunction: exception");
    result_buffer[0] = '\0';
    return -3;
  }
#else
  // For now, return empty string
  result_buffer[0] = '\0';
  SetLastError("CallFunction: Function calls not yet implemented in simple evaluator");

  return -5;  // Not implemented
#endif
}

//
// Error Handling
//

const char* V8Embedder::GetLastError() {
  return last_error_;
}

void V8Embedder::ClearLastError() {
  last_error_[0] = '\0';
}

void V8Embedder::SetLastError(const char* error_message) {
  if (error_message) {
    strncpy(last_error_, error_message, sizeof(last_error_) - 1);
    last_error_[sizeof(last_error_) - 1] = '\0';
    
    // Also log to stderr for debugging purposes
    #ifdef V8_DEBUG_ERRORS
    fprintf(stderr, "[V8Embedder Error] %s\n", last_error_);
    #endif
  }
}

//
// Advanced Features
//

void V8Embedder::SetJITEnabled(bool enabled) {
  jit_enabled_ = enabled;
  
  // TODO: When V8 is integrated:
  // Configure V8 flags for JIT
}

void V8Embedder::SetMemoryLimits(void* isolate_handle,
                                size_t max_heap_size,
                                size_t max_stack_size) {
  // TODO: When V8 is integrated:
  // v8::Isolate* isolate = static_cast<v8::Isolate*>(isolate_handle);
  // isolate->SetData(max_heap_size, max_stack_size);
}

void V8Embedder::CollectGarbage(void* isolate_handle, bool full_gc) {
  // TODO: When V8 is integrated:
  // v8::Isolate* isolate = static_cast<v8::Isolate*>(isolate_handle);
  // isolate->RequestGarbageCollectionForTesting(
  //     full_gc ? v8::Isolate::kFullGarbageCollection 
  //             : v8::Isolate::kMinorGarbageCollection);
}

bool V8Embedder::GetMemoryStats(void* isolate_handle, MemoryStats* stats) {
  if (!stats) {
    return false;
  }
  
  // TODO: When V8 is integrated:
  // v8::Isolate* isolate = static_cast<v8::Isolate*>(isolate_handle);
  // v8::HeapStatistics heap_stats;
  // isolate->GetHeapStatistics(&heap_stats);
  // stats->total_heap_size = heap_stats.total_heap_size();
  // stats->used_heap_size = heap_stats.used_heap_size();
  // stats->external_memory = heap_stats.external_memory();
  // stats->malloced_memory = heap_stats.malloced_memory();
  
  // Placeholder values
  stats->total_heap_size = 0;
  stats->used_heap_size = 0;
  stats->external_memory = 0;
  stats->malloced_memory = 0;
  
  return true;
}

//
// StringBuffer Implementation
//

StringBuffer::StringBuffer(size_t initial_capacity)
    : capacity_(initial_capacity), size_(0) {
  buffer_ = (char*)AllocVec(capacity_, MEMF_PUBLIC | MEMF_CLEAR);
}

StringBuffer::~StringBuffer() {
  if (buffer_) {
    FreeVec(buffer_);
  }
}

bool StringBuffer::Append(const char* str) {
  if (!str || !buffer_) return false;
  
  size_t len = strlen(str);
  if (size_ + len >= capacity_) {
    // Need to grow buffer
    size_t new_capacity = (size_ + len + 256) * 2;
    char* new_buffer = (char*)AllocVec(new_capacity, MEMF_PUBLIC | MEMF_CLEAR);
    if (!new_buffer) return false;
    
    memcpy(new_buffer, buffer_, size_);
    FreeVec(buffer_);
    buffer_ = new_buffer;
    capacity_ = new_capacity;
  }
  
  memcpy(buffer_ + size_, str, len);
  size_ += len;
  buffer_[size_] = '\0';
  
  return true;
}

void StringBuffer::Clear() {
  if (buffer_) {
    buffer_[0] = '\0';
    size_ = 0;
  }
}

}  // namespace aros
}  // namespace v8
