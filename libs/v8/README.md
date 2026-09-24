# V8 JavaScript Engine Library for AROS

## Overview

The V8 library provides a system-wide JavaScript execution engine for AROS applications. It wraps Google's V8 JavaScript engine in a standard AROS shared library interface, making JavaScript execution available to all AROS programs through a simple C API.

## Current Status

**Framework Complete with Basic Evaluator**

This library provides a complete framework for V8 integration, including:
- Full AROS library structure
- Complete C API with all essential functions
- Memory management using exec.library
- Thread-safe isolate and context management
- Comprehensive error handling
- **Simple JavaScript expression evaluator for basic operations**
- **Property storage and retrieval system**

All core functions now support basic JavaScript operations through an enhanced simple evaluator:
- **Arithmetic**: Addition, subtraction, multiplication, division, modulo
- **String concatenation**: Using the + operator
- **Property storage**: Set and get properties by name
- **Property references**: Use stored properties in expressions
- **Parentheses**: Grouping for expression evaluation
- **Operator precedence**: Standard mathematical precedence

The simple evaluator handles expressions like:
- `2 + 2` → `4`
- `"Hello" + "World"` → `"HelloWorld"`
- `x * 5` (where x is a stored property)
- `5 > 3` → `true`
- `true && false` → `false`
- `!true` → `false`
- `10 == 10` → `true`
- `(2 + 3) * 4` → `20` (parentheses)
- `2 + 3 * 4` → `14` (precedence)
- `10 % 3` → `1` (modulo)

When V8 is fully ported to AROS, these stub implementations will be replaced with actual V8 C++ API calls for full JavaScript support including functions, objects, control flow, and all ES6+ features.

## Features

### Core Functionality
- JavaScript code evaluation (basic expressions supported)
- Property storage and retrieval
- Script compilation and caching (framework ready)
- Multiple execution contexts (isolates)
- Object property manipulation (string-based)
- Function calling (framework ready)
- Error handling and reporting

**Currently Supported JavaScript Operations:**
- Arithmetic operations: `+`, `-`, `*`, `/`, `%` (modulo)
- String concatenation with `+`
- Comparison operators: `==`, `!=`, `<`, `>`, `<=`, `>=`
- Strict equality operators: `===`, `!==`
- Logical operators: `&&`, `||`, `!`
- Bitwise operators: `&`, `|`, `^`, `~`
- Shift operators: `<<`, `>>`, `>>>` (unsigned right shift)
- Boolean literals: `true`, `false`
- Special values: `null`, `undefined`, `NaN`
- **Parentheses for grouping**: `(2 + 3) * 4`
- **Operator precedence**: Follows standard JavaScript precedence rules
- **Ternary operator**: `condition ? trueValue : falseValue`
- **Comma operator**: `expr1, expr2` (evaluates both, returns last)
- **Increment/decrement**: `++x`, `x++`, `--x`, `x--`
- **Assignment**: `x = value`
- **Compound assignments**: `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=`, `>>>=`
- Property references in expressions
- Number and string literals
- Property storage and retrieval

### AROS Integration
- Standard AROS .library with LVO table
- Compatible with exec.library memory management
- Thread-safe with SignalSemaphore protection
- Follows AROS library conventions
- Proper initialization and cleanup

### Design Goals
- Simple C API for easy integration
- Support for multiple concurrent contexts
- Efficient script caching
- Minimal memory footprint
- JIT compilation support (when V8 is ported)

## API Reference

### Initialization

```c
APTR V8Initialize(void);
```
Initialize the V8 platform. Must be called before using any other V8 functions.

```c
void V8Cleanup(void);
```
Cleanup the V8 platform and free resources.

### Isolate Management

```c
V8IsolateHandle V8CreateIsolate(void);
```
Create a new V8 isolate (independent JavaScript VM instance).

```c
void V8DestroyIsolate(V8IsolateHandle isolate);
```
Destroy an isolate and free all associated resources.

### Context Management

```c
V8ContextHandle V8CreateContext(V8IsolateHandle isolate);
```
Create a new JavaScript execution context within an isolate.

```c
void V8DestroyContext(V8IsolateHandle isolate, V8ContextHandle context);
```
Destroy a context.

### Script Execution

```c
LONG V8Eval(V8IsolateHandle isolate, V8ContextHandle context,
            CONST_STRPTR script, STRPTR result, ULONG resultSize);
```
Evaluate JavaScript code and return the result as a string.

```c
V8ScriptHandle V8CompileScript(V8IsolateHandle isolate, V8ContextHandle context,
                               CONST_STRPTR script, CONST_STRPTR scriptName);
```
Compile JavaScript code for repeated execution.

```c
LONG V8RunScript(V8IsolateHandle isolate, V8ContextHandle context,
                 V8ScriptHandle script, STRPTR result, ULONG resultSize);
```
Execute a previously compiled script.

```c
void V8FreeScript(V8ScriptHandle script);
```
Free a compiled script.

### Object Manipulation

```c
APTR V8GetGlobalObject(V8IsolateHandle isolate, V8ContextHandle context);
```
Get the global object of a context.

```c
LONG V8SetProperty(V8IsolateHandle isolate, V8ContextHandle context,
                   APTR object, CONST_STRPTR name, CONST_STRPTR value);
```
Set a property on a JavaScript object.

```c
LONG V8GetProperty(V8IsolateHandle isolate, V8ContextHandle context,
                   APTR object, CONST_STRPTR name, STRPTR result, ULONG resultSize);
```
Get a property from a JavaScript object.

```c
LONG V8CallFunction(V8IsolateHandle isolate, V8ContextHandle context,
                    APTR object, CONST_STRPTR functionName,
                    STRPTR result, ULONG resultSize);
```
Call a JavaScript function by name.

## Example Usage

```c
#include <proto/v8.h>
#include <proto/exec.h>
#include <stdio.h>

int main(void)
{
    struct Library *V8Base;
    V8IsolateHandle isolate;
    V8ContextHandle context;
    char result[256];
    LONG rc;
    APTR global;
    
    /* Open the V8 library */
    V8Base = OpenLibrary("v8.library", 1);
    if (!V8Base) {
        printf("Failed to open v8.library\n");
        return 1;
    }
    
    /* Initialize V8 */
    if (!V8Initialize()) {
        printf("Failed to initialize V8\n");
        CloseLibrary(V8Base);
        return 1;
    }
    
    /* Create isolate and context */
    isolate = V8CreateIsolate();
    if (!isolate) {
        printf("Failed to create isolate\n");
        V8Cleanup();
        CloseLibrary(V8Base);
        return 1;
    }
    
    context = V8CreateContext(isolate);
    if (!context) {
        printf("Failed to create context\n");
        V8DestroyIsolate(isolate);
        V8Cleanup();
        CloseLibrary(V8Base);
        return 1;
    }
    
    global = V8GetGlobalObject(isolate, context);
    
    /* Evaluate simple JavaScript expressions */
    rc = V8Eval(isolate, context, "2 + 2", result, sizeof(result));
    if (rc == V8_SUCCESS) {
        printf("2 + 2 = %s\n", result);  // Output: 2 + 2 = 4
    }
    
    rc = V8Eval(isolate, context, "10 * 5", result, sizeof(result));
    if (rc == V8_SUCCESS) {
        printf("10 * 5 = %s\n", result);  // Output: 10 * 5 = 50
    }
    
    /* Set and get properties */
    V8SetProperty(isolate, context, global, "myNumber", "42");
    V8GetProperty(isolate, context, global, "myNumber", result, sizeof(result));
    printf("myNumber: %s\n", result);  // Output: myNumber: 42
    
    /* Use properties in expressions */
    V8SetProperty(isolate, context, global, "x", "7");
    rc = V8Eval(isolate, context, "x * 6", result, sizeof(result));
    if (rc == V8_SUCCESS) {
        printf("x * 6 = %s\n", result);  // Output: x * 6 = 42
    }
    
    /* String concatenation */
    rc = V8Eval(isolate, context, "\"Hello\" + \"World\"", result, sizeof(result));
    if (rc == V8_SUCCESS) {
        printf("Result: %s\n", result);  // Output: Result: HelloWorld
    }
    
    /* Comparison operators */
    rc = V8Eval(isolate, context, "5 > 3", result, sizeof(result));
    if (rc == V8_SUCCESS) {
        printf("5 > 3 = %s\n", result);  // Output: 5 > 3 = true
    }
    
    /* Logical operators */
    rc = V8Eval(isolate, context, "true && false", result, sizeof(result));
    if (rc == V8_SUCCESS) {
        printf("true && false = %s\n", result);  // Output: true && false = false
    }
    
    rc = V8Eval(isolate, context, "!false", result, sizeof(result));
    if (rc == V8_SUCCESS) {
        printf("!false = %s\n", result);  // Output: !false = true
    }
    
    /* Boolean values in properties */
    V8SetProperty(isolate, context, global, "isReady", "true");
    rc = V8Eval(isolate, context, "isReady && true", result, sizeof(result));
    if (rc == V8_SUCCESS) {
        printf("isReady && true = %s\n", result);  // Output: isReady && true = true
    }
    
    /* Cleanup */
    V8DestroyContext(isolate, context);
    V8DestroyIsolate(isolate);
    V8Cleanup();
    CloseLibrary(V8Base);
    
    return 0;
}
```

**Current Limitations:**
- No control flow (if, while, for)
- No function definitions
- No objects or arrays beyond simple properties
- No ES6+ features

These limitations will be removed when the full V8 engine is integrated.
See the repository root `V8_PORTING_ROADMAP.md` for the complete porting plan.

## Building

The library uses the standard AROS mmake build system:

```bash
# Build the V8 library
mmake workbench-libs-v8

# Build and install includes
mmake workbench-libs-v8-includes

# Build test suite
mmake workbench-libs-v8-test
```

## Testing

A comprehensive test suite is provided to validate V8.library functionality:

### Comprehensive Test Suite

```bash
# Build the comprehensive test
mmake workbench-libs-v8-test-library

# Run the test
./test_v8_library
```

The test suite validates:
- Library initialization and cleanup
- Isolate and context lifecycle management
- JavaScript evaluation (all data types)
- Functions, arrays, and objects
- Error handling
- Script compilation and caching
- Multiple concurrent contexts
- Stress testing

See [TEST_GUIDE.md](TEST_GUIDE.md) for detailed testing documentation.

### Quick Test

For a quick validation that V8.library is working:

```bash
# Build the quick test
mmake workbench-libs-v8-test-real

# Run it
./test_v8_real
```

See [QUICK_TEST_EXAMPLE.md](QUICK_TEST_EXAMPLE.md) for a minimal test example.

## Directory Structure

```
workbench/libs/v8/
├── v8.conf                    # Library configuration
├── v8_intern.h                # Internal structures
├── v8_init.c                  # Library initialization
├── v8_platform.c              # Platform management
├── v8initialize.c             # V8Initialize() implementation
├── v8cleanup.c                # V8Cleanup() implementation
├── v8createisolate.c          # V8CreateIsolate() implementation
├── v8destroyisolate.c         # V8DestroyIsolate() implementation
├── v8createcontext.c          # V8CreateContext() implementation
├── v8destroycontext.c         # V8DestroyContext() implementation
├── v8eval.c                   # V8Eval() implementation
├── v8compilescript.c          # V8CompileScript() implementation
├── v8runscript.c              # V8RunScript() implementation
├── v8freescript.c             # V8FreeScript() implementation
├── v8getglobalobject.c        # V8GetGlobalObject() implementation
├── v8setproperty.c            # V8SetProperty() implementation
├── v8getproperty.c            # V8GetProperty() implementation
├── v8callfunction.c           # V8CallFunction() implementation
├── mmakefile.src              # Build configuration
├── include/
│   └── v8.h                   # Public API header
├── IMPLEMENTATION_BREADCRUMBS.md  # Implementation status
└── README.md                  # This file
```

## Implementation Notes

### Breadcrumb System

This library follows the AROS AI breadcrumb system for tracking implementation status. Each source file contains breadcrumb markers indicating:
- Implementation phase
- Current status (INITIAL, STUB, PARTIAL, IMPLEMENTED)
- Priority level
- Design notes
- References to documentation

See `IMPLEMENTATION_BREADCRUMBS.md` for detailed status.

### Memory Management

All memory allocations use exec.library:
- `AllocVec()` for structure allocation
- `FreeVec()` for cleanup
- Proper cleanup in library expunge
- No memory leaks in current stub implementation

### Thread Safety

- Library base protected by SignalSemaphore
- Each isolate has its own semaphore
- Isolates designed for single-threaded access
- Multiple isolates can run on different threads

### Error Handling

All API functions return error codes:
- `V8_SUCCESS` (0) on success
- Negative values for various error conditions
- Result buffers for string output
- Parameter validation in all functions

## Future Work / V8 Integration Status

### Current Status

The v8.library provides a working JavaScript expression evaluator. Full V8 engine integration is in progress.

| Component | Status | Notes |
|-----------|--------|-------|
| v8.library Framework | ✅ Complete | 14 API functions |
| Simple Expression Evaluator | ✅ Complete | Arithmetic, logical, arrays |
| AROS Platform Backend | ✅ Complete | Stubs ready for V8 |
| V8 Source | ✅ Downloaded | v12.4.254 |
| Full V8 Integration | 🚀 In Progress | See roadmap |

**Documentation**:
- **[ELECTRON_ROADMAP.md](../../../ELECTRON_ROADMAP.md)** - Main development roadmap
- **[V8_ACTIONABLE_PORTING_GUIDE.md](../../../V8_ACTIONABLE_PORTING_GUIDE.md)** - Step-by-step V8 guide
- **[V8_PORTING_ROADMAP.md](../../../V8_PORTING_ROADMAP.md)** - High-level overview

### Next Steps

1. Build V8 for host system (verification)
2. Cross-compile V8 for AROS
3. Replace stub implementations with real V8 calls
4. Enable full JavaScript execution

## Requirements

- AROS system with exec.library
- C standard library
- V8 JavaScript engine (when ported)

## Dependencies

- exec.library - Memory and semaphore management
- (future) V8 static library - JavaScript engine

## License

Copyright (C) 2025, The AROS Development Team. All rights reserved.

This library follows the AROS license terms.

## References

- V8 Engine: https://v8.dev/
- V8 Embedder's Guide: https://v8.dev/docs/embed
- V8 Source Code: https://github.com/v8/v8
- AROS Library Development: http://aros.sourceforge.net/documentation/developers/
- [ELECTRON_ROADMAP.md](../../../ELECTRON_ROADMAP.md) - Development roadmap
- [V8_ACTIONABLE_PORTING_GUIDE.md](../../../V8_ACTIONABLE_PORTING_GUIDE.md) - Implementation guide

## Authors

AROS Development Team with AI assistance

## Contact

For questions and contributions, see the AROS project website and GitHub repository.
