// V8 Embedder Helper Functions
// Phase 2.3 Enhancements
//
// This file provides utility functions to enhance the V8 embedder implementation
// while waiting for full V8 integration. These helpers improve error reporting,
// validation, and diagnostics for the stub implementation.

#ifndef V8_AROS_EMBEDDER_HELPERS_H_
#define V8_AROS_EMBEDDER_HELPERS_H_

#include <cstddef>
#include <cstring>

namespace v8 {
namespace aros {

//
// Error Code Definitions
//
enum V8ErrorCode {
  V8_SUCCESS = 0,
  V8_ERROR_NOT_INITIALIZED = -1,
  V8_ERROR_INVALID_PARAMETER = -2,
  V8_ERROR_OUT_OF_MEMORY = -3,
  V8_ERROR_COMPILE_ERROR = -4,
  V8_ERROR_RUNTIME_ERROR = -5,
  V8_ERROR_TYPE_ERROR = -6,
  V8_ERROR_REFERENCE_ERROR = -7,
  V8_ERROR_SYNTAX_ERROR = -8,
  V8_ERROR_INTERNAL = -9,
  V8_ERROR_UNKNOWN = -10
};

//
// Error Reporting Helpers
//
class ErrorReporter {
 public:
  // Convert error code to human-readable string
  static const char* ErrorCodeToString(int error_code);
  
  // Format detailed error message
  static void FormatError(char* buffer, size_t buffer_size,
                         int error_code, const char* context,
                         const char* details);
  
  // Create error message with location
  static void FormatErrorWithLocation(char* buffer, size_t buffer_size,
                                     const char* script_name, int line,
                                     const char* message);
};

//
// Parameter Validation Helpers
//
class ParameterValidator {
 public:
  // Validate isolate handle
  static bool ValidateIsolate(void* isolate_handle, char* error_buffer, size_t error_size);
  
  // Validate context handle
  static bool ValidateContext(void* context_handle, char* error_buffer, size_t error_size);
  
  // Validate script parameter
  static bool ValidateScript(const char* script, char* error_buffer, size_t error_size);
  
  // Validate result buffer
  static bool ValidateBuffer(char* buffer, size_t size, char* error_buffer, size_t error_size);
  
  // Validate property name
  static bool ValidatePropertyName(const char* name, char* error_buffer, size_t error_size);
};

//
// String Handling Helpers
//
class StringHelper {
 public:
  // Safe string copy with null termination
  static void SafeStrCopy(char* dest, const char* src, size_t dest_size);
  
  // Safe string concatenation
  static void SafeStrCat(char* dest, const char* src, size_t dest_size);
  
  // Format string safely
  static void SafeFormat(char* buffer, size_t buffer_size, const char* format, ...);
  
  // Trim whitespace from string
  static char* TrimWhitespace(char* str);
  
  // Check if string is empty or whitespace only
  static bool IsEmptyOrWhitespace(const char* str);
};

//
// Diagnostics and Logging
//
class DiagnosticsHelper {
 public:
  // Log diagnostic message (if enabled)
  static void LogDebug(const char* component, const char* message);
  
  // Log warning
  static void LogWarning(const char* component, const char* message);
  
  // Log error
  static void LogError(const char* component, const char* message);
  
  // Dump isolate state (for debugging)
  static void DumpIsolateState(void* isolate_handle);
  
  // Dump context state (for debugging)
  static void DumpContextState(void* context_handle);
  
  // Enable/disable debug logging
  static void SetDebugEnabled(bool enabled);
  
  // Get debug status
  static bool IsDebugEnabled();
  
 private:
  static bool debug_enabled_;
};

//
// Performance Monitoring
//
class PerformanceMonitor {
 public:
  // Simple timer for profiling
  class Timer {
   public:
    Timer(const char* operation_name);
    ~Timer();
    
    // Get elapsed time in microseconds
    unsigned long GetElapsedMicros() const;
    
   private:
    const char* operation_name_;
    unsigned long start_time_;
  };
  
  // Reset performance statistics
  static void ResetStats();
  
  // Get statistics
  static void GetStats(unsigned long* total_evals,
                      unsigned long* total_time_micros,
                      unsigned long* avg_time_micros);
  
  // Record evaluation timing
  static void RecordEvaluation(unsigned long duration_micros);
  
 private:
  static unsigned long total_evaluations_;
  static unsigned long total_time_;
};

//
// Memory Tracking
//
// LIMITATIONS:
// - RecordDeallocation does not update current_allocated_ (size not tracked)
// - For accurate current memory tracking, use a hash map to store sizes
// - Current implementation tracks: total, peak, and allocation count
//
class MemoryTracker {
 public:
  // Record allocation
  static void RecordAllocation(void* ptr, size_t size, const char* type);
  
  // Record deallocation
  // NOTE: Does not update current_allocated_ as size is not tracked
  static void RecordDeallocation(void* ptr);
  
  // Get memory statistics
  // NOTE: current_allocated will not decrease on deallocations
  static void GetMemoryStats(size_t* total_allocated,
                            size_t* current_allocated,
                            size_t* peak_allocated,
                            unsigned long* allocation_count);
  
  // Reset tracking
  static void ResetTracking();
  
  // Enable/disable tracking
  static void SetTrackingEnabled(bool enabled);
  
 private:
  static bool tracking_enabled_;
  static size_t total_allocated_;
  static size_t current_allocated_;
  static size_t peak_allocated_;
  static unsigned long allocation_count_;
};

}  // namespace aros
}  // namespace v8

#endif  // V8_AROS_EMBEDDER_HELPERS_H_
