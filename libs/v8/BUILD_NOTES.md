# V8 Library Build Notes

## Overview

The v8.library consists of two main components:
1. **v8.library** - C API library (workbench/libs/v8)
2. **V8 AROS Platform** - C++ platform implementation (external/electron/v8)

## Build Requirements

### Prerequisites
- AROS cross-compilation toolchain
- GCC/G++ with C++17 support
- AROS development headers
- Make/mmake build system

### Component Dependencies

```
v8.library (C)
     ├── Depends on: exec.library, dos.library
     ├── Links with: stdc, stdcio
     └── Calls: V8 AROS Platform (via v8_bridge.cc)

V8 AROS Platform (C++)
     ├── Depends on: exec.library (memory, tasks)
     ├── Provides: AROSIsolateManager, AROSJavaScriptEngine
     └── Implements: Platform, Memory, Threading, Isolate management
```

## Build Instructions

### Step 1: Build V8 AROS Platform

The V8 platform implementation in `external/electron/v8` should be built first:

```bash
cd external/electron/v8
make all
```

This creates `libv8_aros_platform.a` containing:
- platform-aros.o
- memory-aros.o
- threading-aros.o
- isolate-aros.o

### Step 2: Build v8.library

Once the platform library is available:

```bash
cd ../../..  # Back to AROS root
mmake workbench-libs-v8
```

This compiles:
- C files: v8_init.c, v8_platform.c, v8_simple_eval.c, v8*.c functions
- C++ file: v8_bridge.cc (links to V8 platform)
- Creates: v8.library

## Current Build Status

### What Works
✅ C API implementation (all 14 functions)  
✅ Simple JavaScript evaluator (fallback)  
✅ C++ bridge layer (v8_bridge.cc)  
✅ Build system configuration (mmakefile.src)

### What Needs Work
⏳ V8 platform static library build integration  
⏳ Proper linking between v8.library and libv8_aros_platform.a  
⏳ AROS cross-compilation testing  
⏳ Symbol resolution verification

## Build Configuration

### mmakefile.src Settings

```makefile
# C++ compiler flags
USER_CXXFLAGS := -std=c++20 -I$(SRCDIR)/external/electron/v8
USER_INCLUDES := -I$(SRCDIR)/external/electron/v8

# Libraries to link
uselibs="stdc stdcio"

# C++ files to compile
cxxfiles="v8_bridge"
```

### Key Features
- **C++20 Standard**: Required for V8 12.4+ platform code
- **Include Path**: Points to external/electron/v8 for platform headers
- **Standard C++ Libraries**: stdc (C++ runtime), stdcio (C++ I/O)

### V8 Engine Integration

The build system can optionally link against the full V8 JavaScript engine:

```makefile
# V8 monolith library path
V8_BUILDDIR := $(GENDIR)/external/electron/v8/build/out/aros/obj

# If libv8_monolith.a exists, include it in the build
ifneq ($(wildcard $(V8_BUILDDIR)/libv8_monolith.a),)
    USER_OBJS := $(V8_BUILDDIR)/libv8_monolith.a
    USER_LDFLAGS += -lpthread -lm
endif
```

**Important**: The V8 monolith library (~74-75MB) is linked using `USER_OBJS` rather than 
`USER_LDFLAGS` with `-l` flags. This is because AROS modules are built using relocatable 
linking (`$(AROS_LD) -Ur`), where the `-l` flag cannot properly resolve static library 
archives. Using `USER_OBJS` ensures the static library is passed directly to the linker 
as an input object.

**Build the V8 Engine**:
```bash
# Build V8 JavaScript engine (optional, ~60 minutes)
mmake external-v8
# Or full build with installation
mmake external-v8-full
```

**Verify V8 Monolith Library**:
```bash
# Check if V8 was built successfully
ls -lh bin/pc-x86_64/gen/external/electron/v8/build/out/aros/obj/libv8_monolith.a
# Should show approximately 74-75MB file

# The library will be automatically detected and included when building v8.library
mmake workbench-libs-v8
```

## Potential Build Issues

### Issue 1: Missing V8 Platform Library
**Symptom**: Undefined references to AROSIsolateManager::Initialize(), etc.  
**Solution**: Build external/electron/v8 platform library first  
**Fix**: `cd external/electron/v8 && make all`

### Issue 2: C++ Standard Library
**Symptom**: Missing C++ runtime symbols  
**Solution**: Ensure stdc library is available in AROS toolchain  
**Fix**: Add to AROS cross-compilation libraries

### Issue 3: Header File Paths
**Symptom**: Cannot find platform-aros.h, isolate-aros.h, etc.  
**Solution**: Check include path is correct  
**Fix**: Verify USER_INCLUDES points to $(SRCDIR)/external/electron/v8

### Issue 4: Exception Handling
**Symptom**: C++ exceptions not working across C/C++ boundary  
**Solution**: Bridge uses try/catch to handle all exceptions  
**Fix**: All exceptions converted to error codes in bridge

### Issue 5: V8 Monolith Not Properly Linked (FIXED)
**Symptom**: v8.library is only 800KB instead of including the ~74-75MB V8 engine  
**Cause**: Using `USER_LDFLAGS` with `-l` flags doesn't work with `AROS_LD -Ur`  
**Solution**: Use `USER_OBJS` to directly include the static library  
**Fix Applied**: Changed from `-lv8_monolith` to `USER_OBJS := $(V8_BUILDDIR)/libv8_monolith.a`

This issue was fixed by changing how the V8 monolith library is linked. The AROS build 
system uses `$(AROS_LD) -Ur` to create relocatable module objects, and this linker 
command does not properly resolve `-l` flags for static libraries. By using `USER_OBJS` 
instead, the static library file is directly passed to the linker as an input object, 
ensuring all V8 code is included in the final module.

## Testing Build

### Syntax Check (Without AROS Headers)
```bash
cd workbench/libs/v8
# Check C++ syntax only (will fail on AROS headers, but verifies C++ code)
g++ -std=c++17 -fsyntax-only -I../../../external/electron/v8 v8_bridge.cc
```

### Full Build (With AROS Toolchain)
```bash
# From AROS root directory
./configure --target=linux-x86_64  # or your target
make
mmake workbench-libs-v8
```

### Verify Library
```bash
# Check if library was created
ls -la bin/linux-x86_64/AROS/Libs/v8.library

# Check symbols
nm bin/linux-x86_64/AROS/Libs/v8.library | grep V8Initialize
```

## Integration Testing

### Test Programs
1. **test_v8.c** - Basic library functionality
2. **test_v8_enhanced.c** - Expression evaluator tests
3. **test_v8_advanced.c** - Advanced operator tests
4. **example_v8_demo.c** - Real-world usage examples

### Running Tests
```bash
# Build test programs
mmake workbench-libs-v8-test

# Run tests
./test_v8
./test_v8_enhanced
```

## Troubleshooting

### Library Won't Load
- Check if v8.library is in LIBS: directory
- Verify all dependencies are available
- Check version numbers match

### Crashes on Initialization
- Verify V8 platform library is linked
- Check memory allocation succeeds
- Enable debug output in v8_bridge.cc

### JavaScript Execution Fails
- Falls back to simple evaluator (expected until V8 source integrated)
- Check v8eval.c fallback logic
- Verify V8Bridge_ExecuteScript() error handling

## Future Build Improvements

### Short Term
1. Create mmakefile for external/electron/v8
2. Add proper build dependency in workbench/libs/v8/mmakefile.src
3. Automate platform library building

### Long Term
1. Integrate actual V8 source code
2. Add JIT compilation support
3. Enable code caching and snapshots
4. Optimize for AROS architecture

## Notes

- The v8_bridge.cc provides a clean C interface to C++ V8 platform
- All C++ exceptions are caught and converted to error codes
- The bridge is designed to be extended as more V8 features are ported
- Simple evaluator provides fallback until full V8 is integrated

## References

- AROS Build Guide: http://aros.sourceforge.net/documentation/developers/
- V8 Embedder's Guide: https://v8.dev/docs/embed
- V8 Build Instructions: https://v8.dev/docs/build

---

*Last Updated: 2025-10-31*  
*Status: Build system configured, awaiting AROS toolchain testing*
