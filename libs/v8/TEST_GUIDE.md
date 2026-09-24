# V8.library Test Guide

This guide explains how to build and run the comprehensive test suite for V8.library on AROS.

## Overview

The `test_v8_library` program is a comprehensive test suite that validates all core functionality of V8.library. It ensures that the library is properly built, integrated with the V8 JavaScript engine, and ready for production use on AROS.

## Test Coverage

The test suite validates:

1. **Library Initialization** - V8Initialize() and platform setup
2. **Isolate Lifecycle** - Creating and destroying V8 isolates
3. **Context Lifecycle** - Creating and destroying JavaScript contexts
4. **Basic Numbers** - Integer arithmetic operations
5. **String Operations** - String literals and concatenation
6. **Boolean Operations** - Boolean values and logical operators
7. **Variables** - Property assignment and retrieval
8. **Functions** - Function definition and execution
9. **Arrays** - Array literals and operations
10. **Error Handling** - Syntax, reference, and runtime errors
11. **Script Compilation** - Compiling and caching JavaScript code
12. **Multiple Contexts** - Context isolation
13. **Complex Expressions** - Nested operations and operator precedence
14. **Stress Test** - Multiple context creation/destruction

## Prerequisites

### Required

- AROS development environment with working build system
- v8.library built and installed in LIBS:
- V8 JavaScript engine (libv8_monolith.a) built or available

### Optional (for full V8 integration)

If you want to test with the real V8 engine (not just the stub evaluator):

1. Build V8 using the AROS cross-compiler:
   ```bash
   cd /path/to/AROS-OLD
   mmake external-v8
   ```

2. This will create `libv8_monolith.a` which v8.library will automatically detect and use.

## Building the Test

### Using mmake (recommended)

```bash
# Build just the comprehensive test
mmake workbench-libs-v8-test-library

# Or build all V8 tests
mmake workbench-libs-v8-test
```

The test program will be built to:
```
$(AROS_TESTS)/test_v8_library
```

### Manual Build

If you need to build manually:

```bash
cd /path/to/AROS-OLD/workbench/libs/v8
gcc -o test_v8_library test_v8_library.c -I../../.. -lv8
```

## Running the Test

### On Native AROS

1. Copy test_v8_library to your AROS system
2. Ensure v8.library is in LIBS:
3. Run from shell:
   ```
   test_v8_library
   ```

### On Hosted AROS (Linux, etc.)

```bash
./test_v8_library
```

### Expected Output

A successful run looks like:

```
================================================
    V8.library Comprehensive Test Suite
================================================

This test validates that V8.library is properly
built and integrated with the V8 JavaScript
engine for use on AROS.

✓ v8.library opened successfully

--- Test 1: Library Initialization ---
  ✓ PASS: V8Initialize() returns valid platform handle
  ✓ PASS: V8Initialize() is idempotent

--- Test 2: Isolate Lifecycle ---
  ✓ PASS: V8CreateIsolate() creates isolate
  ✓ PASS: V8DestroyIsolate() completes without crash

[... more test output ...]

✓ V8Cleanup() completed
✓ v8.library closed

================================================
           TEST SUMMARY
================================================
Total tests:    XX
Passed:         XX
Failed:         0

✓✓✓ ALL TESTS PASSED ✓✓✓

V8.library is working correctly on AROS!
================================================
```

## Understanding Test Results

### All Tests Pass

If all tests pass, v8.library is working correctly and ready for use.

### Some Tests Fail

If tests fail, check:

1. **Library not found** - Ensure v8.library is in LIBS:
2. **Initialization fails** - Check that V8 platform is properly initialized
3. **Evaluation fails** - Verify V8 engine integration:
   - With real V8: All JavaScript features should work
   - With stubs: Only basic expressions are supported

### Test Categories

- **Basic tests (1-3)** - Must pass for any v8.library build
- **Expression tests (4-6, 13)** - Work with both real V8 and stubs
- **Advanced tests (7-12, 14)** - May require real V8 engine

## Test Modes

### Stub Mode (Phase 2.2)

When V8.library is built without libv8_monolith.a, it uses a simple expression evaluator:
- Basic arithmetic: ✓
- String concatenation: ✓
- Boolean operations: ✓
- Functions: Limited
- Arrays: Limited
- Objects: Limited

### Full V8 Mode (Phase 2.3+)

When V8.library is built with libv8_monolith.a, full JavaScript support is available:
- All ES6+ features: ✓
- Functions and closures: ✓
- Arrays and objects: ✓
- Async/await: ✓
- Classes: ✓
- Modules: ✓

## Troubleshooting

### "Could not open v8.library"

Solution:
```bash
# Check if library exists
ls -l LIBS:v8.library

# If missing, build and install it
mmake workbench-libs-v8
```

### "V8Initialize() failed"

This indicates a problem with V8 platform initialization. Check:
- Memory availability (V8 needs ~100MB minimum)
- File system access (V8 may need temp directory)

### Test crashes or hangs

This may indicate:
- Memory corruption (check V8 memory management)
- Threading issues (check isolate/context lifecycle)
- Stack overflow (check recursion depth)

## Comparing with Other Tests

### test_v8_real.c

Focused on real V8 engine integration:
- Tests specific V8 features
- Requires libv8_monolith.a
- Smaller test set (5 tests)

### test_v8_library.c (this test)

Comprehensive test suite:
- Tests all v8.library API functions
- Works with or without real V8
- Complete test coverage (50+ individual checks)
- Production validation

## Adding New Tests

To add tests to this suite:

1. Create a new test function:
   ```c
   static void test_my_feature(void)
   {
       TEST_START("My Feature");
       
       APTR isolate = V8CreateIsolate();
       APTR context = V8CreateContext(isolate);
       
       // Your test code here
       TEST_ASSERT(condition, "description");
       
       V8DestroyContext(context);
       V8DestroyIsolate(isolate);
   }
   ```

2. Call it from main():
   ```c
   test_my_feature();
   ```

3. Rebuild:
   ```bash
   mmake workbench-libs-v8-test-library
   ```

## CI/CD Integration

For continuous integration:

```yaml
- name: Build V8 Library Test
  run: |
    cd /path/to/AROS-OLD
    mmake workbench-libs-v8-test-library

- name: Run V8 Library Test
  run: |
    $(AROS_TESTS)/test_v8_library
```

## Performance Benchmarking

To measure V8 performance, add timing to tests:

```c
#include <proto/timer.h>

struct timeval start, end;
GetSysTime(&start);

// Run test

GetSysTime(&end);
SubTime(&end, &start);
printf("Time: %ld.%06ld seconds\n", end.tv_secs, end.tv_micro);
```

## Related Documentation

- [V8.library README](README.md) - Library overview
- [INTEGRATION_GUIDE.md](INTEGRATION_GUIDE.md) - Using V8.library in applications
- [V8 Build Guide](../../../external/electron/v8/V8_BUILD_GUIDE.md) - Building V8 engine
- [V8 Embedder's Guide](https://v8.dev/docs/embed) - Official V8 documentation

## Support

If you encounter issues with this test:

1. Check the library is built: `ls -l LIBS:v8.library`
2. Verify V8 engine status: Check for libv8_monolith.a
3. Review build logs: Check mmake output
4. Test simple evaluation: Try `test_v8_real` first

## License

This test program is part of AROS and uses the same license (AROS Public License).

## Conclusion

This comprehensive test suite ensures V8.library is working correctly on AROS. Running it successfully confirms that:

✓ The library is properly built and linked
✓ The V8 JavaScript engine is integrated correctly  
✓ All API functions work as expected
✓ Memory management is functioning
✓ Error handling is robust
✓ The library is ready for production use

Thank you for testing V8.library on AROS!
