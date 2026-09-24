# V8 JavaScript Engine Porting Guide for AROS

## Overview

This document provides guidance for porting the V8 JavaScript engine to AROS and integrating it with the v8.library framework.

## Current Status

The v8.library framework is complete with:
- ✅ Full AROS library structure
- ✅ Complete C API (14 functions)
- ✅ Internal memory management structures
- ✅ Thread safety mechanisms
- ✅ Stub implementations with integration points
- ⏳ V8 engine not yet ported

## V8 Architecture Overview

### Core Components

1. **Platform**: Low-level OS abstraction layer
2. **Isolate**: Independent JavaScript VM instance
3. **Context**: JavaScript execution environment
4. **HandleScope**: Automatic memory management
5. **Persistent**: Long-lived object references

### Key Classes

```cpp
v8::Platform       // OS abstraction
v8::Isolate        // VM instance
v8::Context        // Execution environment
v8::Script         // Compiled code
v8::Value          // JavaScript value
v8::Object         // JavaScript object
v8::Function       // JavaScript function
```

## Integration Strategy

### Phase 1: Platform Implementation

Create AROS platform backend for V8:

**File**: `src/libplatform/aros/aros-platform.cc`

```cpp
class AROSPlatform : public v8::Platform {
public:
    AROSPlatform();
    virtual ~AROSPlatform();
    
    // Thread management using exec.library
    std::shared_ptr<v8::TaskRunner> GetForegroundTaskRunner(v8::Isolate* isolate) override;
    
    // Time functions using timer.device
    double MonotonicallyIncreasingTime() override;
    double CurrentClockTimeMillis() override;
    
    // Tracing (can be stubbed initially)
    v8::TracingController* GetTracingController() override;
};
```

**AROS Integration Points**:
- Use `exec.library` for thread management
- Use `timer.device` for time functions
- Use `dos.library` for file I/O
- Use `intuition.library` for GUI debugging

### Phase 2: Memory Management

Implement custom allocator using exec.library:

**File**: `v8_aros_allocator.cc`

```cpp
class AROSArrayBufferAllocator : public v8::ArrayBuffer::Allocator {
public:
    void* Allocate(size_t length) override {
        return AllocVec(length, MEMF_PUBLIC | MEMF_CLEAR);
    }
    
    void* AllocateUninitialized(size_t length) override {
        return AllocVec(length, MEMF_PUBLIC);
    }
    
    void Free(void* data, size_t length) override {
        FreeVec(data);
    }
};
```

### Phase 3: Build System

Adapt V8 build to AROS:

**Requirements**:
- GCC C++11 or later
- Python 3 (for build scripts)
- GN or Make build system
- ICU library (for internationalization)

**Build Steps**:
```bash
# 1. Configure for AROS
./configure --target=aros-i386 --without-intl

# 2. Build V8 static library
make -j4

# 3. Link with v8.library
ar rcs libv8_base.a obj/*.o
```

### Phase 4: Library Integration

Replace stub implementations with V8 calls:

#### Example: v8initialize.c

**Before** (current stub):
```c
V8Base->v8_Platform = (APTR)0xDEADBEEF;
V8Base->v8_Initialized = TRUE;
```

**After** (V8 integrated):
```c
// Initialize ICU
v8::V8::InitializeICU();

// Create AROS platform
v8::Platform* platform = new AROSPlatform();
v8::V8::InitializePlatform(platform);
v8::V8::Initialize();

// Create allocator
v8::ArrayBuffer::Allocator* allocator = new AROSArrayBufferAllocator();

V8Base->v8_Platform = platform;
V8Base->v8_ArrayBuffer = allocator;
V8Base->v8_Initialized = TRUE;
```

#### Example: v8createisolate.c

**Before** (current stub):
```c
isolate->vi_Isolate = (APTR)isolate;
```

**After** (V8 integrated):
```c
v8::Isolate::CreateParams params;
params.array_buffer_allocator = 
    static_cast<v8::ArrayBuffer::Allocator*>(V8Base->v8_ArrayBuffer);

v8::Isolate* v8isolate = v8::Isolate::New(params);
isolate->vi_Isolate = v8isolate;
```

#### Example: v8eval.c

**Before** (current stub):
```c
strncpy(result, "V8 not yet ported", resultSize - 1);
return V8_SUCCESS;
```

**After** (V8 integrated):
```c
v8::Isolate* v8isolate = static_cast<v8::Isolate*>(v8isolate->vi_Isolate);
v8::HandleScope handle_scope(v8isolate);

v8::Local<v8::Context> v8context = 
    static_cast<v8::Persistent<v8::Context>*>(v8context->vc_Context)->Get(v8isolate);
v8::Context::Scope context_scope(v8context);

// Compile and run
v8::TryCatch try_catch(v8isolate);
v8::Local<v8::String> source = 
    v8::String::NewFromUtf8(v8isolate, script).ToLocalChecked();
    
v8::Local<v8::Script> compiled_script;
if (!v8::Script::Compile(v8context, source).ToLocal(&compiled_script)) {
    return V8_ERROR_COMPILE;
}

v8::Local<v8::Value> result_value;
if (!compiled_script->Run(v8context).ToLocal(&result_value)) {
    return V8_ERROR_RUNTIME;
}

// Convert to string
if (result && resultSize > 0) {
    v8::String::Utf8Value utf8(v8isolate, result_value);
    strncpy(result, *utf8, resultSize - 1);
    result[resultSize - 1] = '\0';
}

return V8_SUCCESS;
```

## Integration Checklist

### Prerequisites
- [ ] AROS development environment set up
- [ ] V8 source code downloaded
- [ ] Build tools installed (GCC, Python)
- [ ] ICU library available

### Platform Layer
- [ ] Implement AROSPlatform class
- [ ] Thread management with exec.library
- [ ] Time functions with timer.device
- [ ] File I/O with dos.library
- [ ] Test platform functionality

### Memory Management
- [ ] Implement AROSArrayBufferAllocator
- [ ] Test allocation/deallocation
- [ ] Verify memory leak detection
- [ ] Test with exec.library pools

### Build System
- [ ] Configure V8 for AROS target
- [ ] Build V8 static library
- [ ] Link with v8.library
- [ ] Test library loading

### Function Integration
- [ ] V8Initialize/Cleanup
- [ ] V8CreateIsolate/DestroyIsolate
- [ ] V8CreateContext/DestroyContext
- [ ] V8Eval
- [ ] V8CompileScript/RunScript/FreeScript
- [ ] V8GetGlobalObject
- [ ] V8SetProperty/GetProperty
- [ ] V8CallFunction

### Testing
- [ ] Unit tests for each function
- [ ] Integration tests
- [ ] Performance benchmarks
- [ ] Memory leak tests
- [ ] Stress tests

### Optimization
- [ ] Enable JIT compilation
- [ ] Snapshot support
- [ ] Optimize for AROS architecture
- [ ] Profile and tune performance

## Key Challenges

### 1. Architecture Support

V8 JIT requires:
- x86/x86-64: Full support expected
- ARM: Should work with adaptation
- PowerPC: May require significant work

### 2. Memory Constraints

AROS systems may have limited memory:
- Implement heap size limits
- Configure conservative GC settings
- Test with low-memory scenarios

### 3. Threading Model

V8 assumes POSIX threads:
- Map to AROS processes/tasks
- Implement synchronization primitives
- Test concurrent isolates

### 4. Exception Handling

V8 uses C++ exceptions:
- Ensure AROS GCC supports C++ exceptions
- Test exception propagation
- Handle OOM conditions gracefully

## Testing Strategy

### Unit Tests

Test each library function:

```c
// test_v8_isolate.c
void test_create_destroy_isolate(void) {
    V8Initialize();
    V8IsolateHandle isolate = V8CreateIsolate();
    assert(isolate != NULL);
    V8DestroyIsolate(isolate);
    V8Cleanup();
}
```

### Integration Tests

Test realistic scenarios:

```c
// test_v8_script.c
void test_script_execution(void) {
    V8Initialize();
    V8IsolateHandle isolate = V8CreateIsolate();
    V8ContextHandle context = V8CreateContext(isolate);
    
    char result[256];
    LONG rc = V8Eval(isolate, context, "2 + 2", result, sizeof(result));
    
    assert(rc == V8_SUCCESS);
    assert(strcmp(result, "4") == 0);
    
    V8DestroyContext(isolate, context);
    V8DestroyIsolate(isolate);
    V8Cleanup();
}
```

### Performance Tests

Benchmark JavaScript execution:

```c
// benchmark_v8.c
void benchmark_eval(void) {
    // Measure eval performance
    // Compare with other JS engines
    // Test JIT warmup
}
```

## Debugging

### Enable V8 Debugging

```cpp
// In platform initialization
v8::V8::SetFlagsFromString("--expose-gc --trace-gc");
```

### AROS Debug Output

```c
// In v8 library code
#ifdef DEBUG
    kprintf("V8: Creating isolate %p\n", isolate);
#endif
```

### GDB Integration

```bash
gdb ./myapp
(gdb) break V8Eval
(gdb) run
(gdb) print isolate
```

## Performance Considerations

### JIT Optimization

- Enable TurboFan for best performance
- Configure code cache size
- Tune GC parameters

### Memory Usage

- Set reasonable heap limits
- Use context snapshots
- Implement code caching

### Startup Time

- Use startup snapshots
- Lazy compilation where possible
- Precompile common scripts

## Known Issues

### Potential Problems

1. **C++ ABI Compatibility**: Ensure GCC C++ ABI matches between V8 and AROS
2. **Exception Handling**: Test C++ exceptions across library boundaries
3. **Thread-Local Storage**: May need special handling on AROS
4. **Large Memory Footprint**: V8 requires significant memory

### Workarounds

- Use older V8 versions if newer ones have issues
- Disable advanced features if needed
- Implement fallback modes

## Resources

### V8 Documentation
- V8 Embedder's Guide: https://v8.dev/docs/embed
- V8 API Reference: https://v8.dev/docs/api
- V8 Build Instructions: https://v8.dev/docs/build

### AROS Documentation
- AROS Developer Guide: http://aros.sourceforge.net/documentation/developers/
- AROS Library Development: http://aros.sourceforge.net/documentation/developers/libraries.php
- AROS exec.library: http://aros.sourceforge.net/documentation/developers/autodocs/exec.php

### Example Ports
- V8 for Haiku OS
- V8 for FreeBSD
- V8 embedded Linux examples

## Contact

For questions about the v8.library framework:
- Check IMPLEMENTATION_BREADCRUMBS.md for status
- Review source code TODO comments
- Contact AROS development team

## Contributing

When porting V8:
1. Update breadcrumb status in source files
2. Document any AROS-specific changes
3. Add test cases
4. Update this guide with lessons learned
5. Submit patches to AROS project

## License

This guide is part of the AROS V8 library project and follows AROS license terms.

V8 is licensed under the BSD-style V8 license. Ensure compatibility with AROS licensing.
