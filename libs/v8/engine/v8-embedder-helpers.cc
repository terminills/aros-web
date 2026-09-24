// V8 Embedder Helper Functions Implementation
// Phase 2.3 Enhancements

#include "v8-embedder-helpers.h"
#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <sys/time.h>

namespace v8 {
namespace aros {

//
// ErrorReporter Implementation
//

const char* ErrorReporter::ErrorCodeToString(int error_code) {
  switch (error_code) {
    case V8_SUCCESS:
      return "Success";
    case V8_ERROR_NOT_INITIALIZED:
      return "V8 not initialized";
    case V8_ERROR_INVALID_PARAMETER:
      return "Invalid parameter";
    case V8_ERROR_OUT_OF_MEMORY:
      return "Out of memory";
    case V8_ERROR_COMPILE_ERROR:
      return "Script compilation error";
    case V8_ERROR_RUNTIME_ERROR:
      return "Script runtime error";
    case V8_ERROR_TYPE_ERROR:
      return "Type error";
    case V8_ERROR_REFERENCE_ERROR:
      return "Reference error";
    case V8_ERROR_SYNTAX_ERROR:
      return "Syntax error";
    case V8_ERROR_INTERNAL:
      return "Internal error";
    default:
      return "Unknown error";
  }
}

void ErrorReporter::FormatError(char* buffer, size_t buffer_size,
                               int error_code, const char* context,
                               const char* details) {
  if (!buffer || buffer_size == 0) return;
  
  const char* error_name = ErrorCodeToString(error_code);
  
  if (context && details) {
    snprintf(buffer, buffer_size, "%s in %s: %s", error_name, context, details);
  } else if (context) {
    snprintf(buffer, buffer_size, "%s in %s", error_name, context);
  } else if (details) {
    snprintf(buffer, buffer_size, "%s: %s", error_name, details);
  } else {
    snprintf(buffer, buffer_size, "%s", error_name);
  }
  
  buffer[buffer_size - 1] = '\0';
}

void ErrorReporter::FormatErrorWithLocation(char* buffer, size_t buffer_size,
                                           const char* script_name, int line,
                                           const char* message) {
  if (!buffer || buffer_size == 0) return;
  
  if (script_name && line > 0) {
    snprintf(buffer, buffer_size, "%s:%d: %s", script_name, line, 
            message ? message : "Error");
  } else if (script_name) {
    snprintf(buffer, buffer_size, "%s: %s", script_name, 
            message ? message : "Error");
  } else {
    snprintf(buffer, buffer_size, "Line %d: %s", line, 
            message ? message : "Error");
  }
  
  buffer[buffer_size - 1] = '\0';
}

//
// ParameterValidator Implementation
//

bool ParameterValidator::ValidateIsolate(void* isolate_handle, 
                                        char* error_buffer, size_t error_size) {
  if (!isolate_handle) {
    if (error_buffer && error_size > 0) {
      StringHelper::SafeStrCopy(error_buffer, 
                               "Isolate handle is NULL", error_size);
    }
    return false;
  }
  return true;
}

bool ParameterValidator::ValidateContext(void* context_handle,
                                        char* error_buffer, size_t error_size) {
  if (!context_handle) {
    if (error_buffer && error_size > 0) {
      StringHelper::SafeStrCopy(error_buffer, 
                               "Context handle is NULL", error_size);
    }
    return false;
  }
  return true;
}

bool ParameterValidator::ValidateScript(const char* script,
                                       char* error_buffer, size_t error_size) {
  if (!script) {
    if (error_buffer && error_size > 0) {
      StringHelper::SafeStrCopy(error_buffer, 
                               "Script parameter is NULL", error_size);
    }
    return false;
  }
  
  if (StringHelper::IsEmptyOrWhitespace(script)) {
    if (error_buffer && error_size > 0) {
      StringHelper::SafeStrCopy(error_buffer, 
                               "Script is empty or whitespace only", error_size);
    }
    return false;
  }
  
  return true;
}

bool ParameterValidator::ValidateBuffer(char* buffer, size_t size,
                                       char* error_buffer, size_t error_size) {
  if (!buffer) {
    if (error_buffer && error_size > 0) {
      StringHelper::SafeStrCopy(error_buffer, 
                               "Result buffer is NULL", error_size);
    }
    return false;
  }
  
  if (size == 0) {
    if (error_buffer && error_size > 0) {
      StringHelper::SafeStrCopy(error_buffer, 
                               "Result buffer size is zero", error_size);
    }
    return false;
  }
  
  return true;
}

bool ParameterValidator::ValidatePropertyName(const char* name,
                                              char* error_buffer, size_t error_size) {
  if (!name) {
    if (error_buffer && error_size > 0) {
      StringHelper::SafeStrCopy(error_buffer, 
                               "Property name is NULL", error_size);
    }
    return false;
  }
  
  if (name[0] == '\0') {
    if (error_buffer && error_size > 0) {
      StringHelper::SafeStrCopy(error_buffer, 
                               "Property name is empty", error_size);
    }
    return false;
  }
  
  return true;
}

//
// StringHelper Implementation
//

void StringHelper::SafeStrCopy(char* dest, const char* src, size_t dest_size) {
  if (!dest || dest_size == 0) return;
  if (!src) {
    dest[0] = '\0';
    return;
  }
  
  strncpy(dest, src, dest_size - 1);
  dest[dest_size - 1] = '\0';
}

void StringHelper::SafeStrCat(char* dest, const char* src, size_t dest_size) {
  if (!dest || dest_size == 0 || !src) return;
  
  size_t dest_len = strlen(dest);
  if (dest_len >= dest_size - 1) return;
  
  size_t remaining = dest_size - dest_len - 1;
  strncat(dest, src, remaining);
  dest[dest_size - 1] = '\0';
}

void StringHelper::SafeFormat(char* buffer, size_t buffer_size, 
                              const char* format, ...) {
  if (!buffer || buffer_size == 0 || !format) return;
  
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, buffer_size, format, args);
  va_end(args);
  
  buffer[buffer_size - 1] = '\0';
}

char* StringHelper::TrimWhitespace(char* str) {
  if (!str) return nullptr;
  
  // Trim leading whitespace
  while (*str && (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r')) {
    str++;
  }
  
  // Handle empty string after leading trim
  if (*str == '\0') {
    return str;
  }
  
  // Trim trailing whitespace
  char* end = str + strlen(str) - 1;
  while (end >= str && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
    *end = '\0';
    end--;
  }
  
  return str;
}

bool StringHelper::IsEmptyOrWhitespace(const char* str) {
  if (!str) return true;
  
  while (*str) {
    if (*str != ' ' && *str != '\t' && *str != '\n' && *str != '\r') {
      return false;
    }
    str++;
  }
  
  return true;
}

//
// DiagnosticsHelper Implementation
//

bool DiagnosticsHelper::debug_enabled_ = false;

void DiagnosticsHelper::SetDebugEnabled(bool enabled) {
  debug_enabled_ = enabled;
}

bool DiagnosticsHelper::IsDebugEnabled() {
  return debug_enabled_;
}

void DiagnosticsHelper::LogDebug(const char* component, const char* message) {
  if (!debug_enabled_ || !component || !message) return;
  fprintf(stderr, "[V8-DEBUG] %s: %s\n", component, message);
}

void DiagnosticsHelper::LogWarning(const char* component, const char* message) {
  if (!component || !message) return;
  fprintf(stderr, "[V8-WARNING] %s: %s\n", component, message);
}

void DiagnosticsHelper::LogError(const char* component, const char* message) {
  if (!component || !message) return;
  fprintf(stderr, "[V8-ERROR] %s: %s\n", component, message);
}

void DiagnosticsHelper::DumpIsolateState(void* isolate_handle) {
  if (!debug_enabled_) return;
  
  fprintf(stderr, "[V8-DEBUG] Isolate State Dump:\n");
  fprintf(stderr, "  Handle: %p\n", isolate_handle);
  fprintf(stderr, "  Status: %s\n", isolate_handle ? "Valid" : "NULL");
  
  // TODO: When V8 is integrated, dump more state:
  // - Heap statistics
  // - Context count
  // - Script count
  // - Memory usage
}

void DiagnosticsHelper::DumpContextState(void* context_handle) {
  if (!debug_enabled_) return;
  
  fprintf(stderr, "[V8-DEBUG] Context State Dump:\n");
  fprintf(stderr, "  Handle: %p\n", context_handle);
  fprintf(stderr, "  Status: %s\n", context_handle ? "Valid" : "NULL");
  
  // TODO: When V8 is integrated, dump more state:
  // - Global object properties
  // - Security token
  // - Embedder data
}

//
// PerformanceMonitor Implementation
//

unsigned long PerformanceMonitor::total_evaluations_ = 0;
unsigned long PerformanceMonitor::total_time_ = 0;

PerformanceMonitor::Timer::Timer(const char* operation_name)
    : operation_name_(operation_name) {
  /* gettimeofday inside v8.library is GLIBC-ABI: writes 16 bytes
   * {int64 sec; int64 usec}, NOT the 8-byte AROS timeval. */
  long long tv[2];
  gettimeofday(reinterpret_cast<struct timeval*>(tv), nullptr);
  start_time_ = tv[0] * 1000000UL + tv[1];
}

PerformanceMonitor::Timer::~Timer() {
  unsigned long elapsed = GetElapsedMicros();
  
  if (DiagnosticsHelper::IsDebugEnabled()) {
    fprintf(stderr, "[V8-PERF] %s: %lu microseconds\n", 
            operation_name_, elapsed);
  }
}

unsigned long PerformanceMonitor::Timer::GetElapsedMicros() const {
  /* glibc-ABI gettimeofday: see Timer ctor. */
  long long tv[2];
  gettimeofday(reinterpret_cast<struct timeval*>(tv), nullptr);
  unsigned long now = tv[0] * 1000000UL + tv[1];
  return now - start_time_;
}

void PerformanceMonitor::ResetStats() {
  total_evaluations_ = 0;
  total_time_ = 0;
}

void PerformanceMonitor::GetStats(unsigned long* total_evals,
                                 unsigned long* total_time_micros,
                                 unsigned long* avg_time_micros) {
  if (total_evals) *total_evals = total_evaluations_;
  if (total_time_micros) *total_time_micros = total_time_;
  if (avg_time_micros) {
    *avg_time_micros = total_evaluations_ > 0 
                       ? (total_time_ / total_evaluations_) 
                       : 0;
  }
}

void PerformanceMonitor::RecordEvaluation(unsigned long duration_micros) {
  total_evaluations_++;
  total_time_ += duration_micros;
}

//
// MemoryTracker Implementation
//

bool MemoryTracker::tracking_enabled_ = false;
size_t MemoryTracker::total_allocated_ = 0;
size_t MemoryTracker::current_allocated_ = 0;
size_t MemoryTracker::peak_allocated_ = 0;
unsigned long MemoryTracker::allocation_count_ = 0;

void MemoryTracker::RecordAllocation(void* ptr, size_t size, const char* type) {
  if (!tracking_enabled_ || !ptr) return;
  
  total_allocated_ += size;
  current_allocated_ += size;
  allocation_count_++;
  
  if (current_allocated_ > peak_allocated_) {
    peak_allocated_ = current_allocated_;
  }
  
  if (DiagnosticsHelper::IsDebugEnabled()) {
    fprintf(stderr, "[V8-MEM] Allocated %zu bytes (%s) at %p\n", 
            size, type ? type : "unknown", ptr);
  }
}

void MemoryTracker::RecordDeallocation(void* ptr) {
  if (!tracking_enabled_ || !ptr) return;
  
  // Note: This is a simplified implementation that doesn't track deallocation sizes.
  // For accurate current_allocated_ tracking, use a map to store allocation sizes
  // keyed by pointer address, then subtract on deallocation.
  // The current implementation is sufficient for counting allocations and tracking
  // total/peak memory, but current_allocated_ will not decrease on deallocations.
  
  if (DiagnosticsHelper::IsDebugEnabled()) {
    fprintf(stderr, "[V8-MEM] Deallocated memory at %p (size not tracked)\n", ptr);
  }
}

void MemoryTracker::GetMemoryStats(size_t* total_allocated,
                                  size_t* current_allocated,
                                  size_t* peak_allocated,
                                  unsigned long* allocation_count) {
  if (total_allocated) *total_allocated = total_allocated_;
  if (current_allocated) *current_allocated = current_allocated_;
  if (peak_allocated) *peak_allocated = peak_allocated_;
  if (allocation_count) *allocation_count = allocation_count_;
}

void MemoryTracker::ResetTracking() {
  total_allocated_ = 0;
  current_allocated_ = 0;
  peak_allocated_ = 0;
  allocation_count_ = 0;
}

void MemoryTracker::SetTrackingEnabled(bool enabled) {
  tracking_enabled_ = enabled;
}

}  // namespace aros
}  // namespace v8
