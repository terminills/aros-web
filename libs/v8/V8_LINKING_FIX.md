# V8 Monolithic Library Linking Fix

## Issue Summary

The V8 monolithic library (`libv8_monolith.a`, 72MB) was not being properly linked into `v8.library`, resulting in a final library size of only 840KB instead of the expected size (>70MB). This indicated that the V8 engine code was not being included.

## Root Cause

The problem was in `workbench/libs/v8/mmakefile.src` where two conflicting MetaMake dependency directives existed:

```makefile
# Line 98 (inside ifeq block) - CORRECT
#MM- workbench-libs-v8-linklib-v8monolith : workbench-libs-v8-linklib-v8monolith-real

# Inside else block (before fix) - THE PROBLEM
#MM- workbench-libs-v8-linklib-v8monolith : workbench-libs-v8-linklib-v8monolith-stub
```

**Note:** Line numbers refer to the code before the fix was applied.

### Why This Caused the Problem

**MetaMake processes ALL `#MM` directives before Make evaluates conditional statements (`ifeq`/`else`/`endif`).**

This meant that both dependency lines were visible to MetaMake, causing:
1. `workbench-libs-v8-linklib-v8monolith-real` to execute (building the real library)
2. `workbench-libs-v8-linklib-v8monolith-stub` to ALSO execute (overriding with stub configuration)

The build log confirmed this:
```
[MMAKE] Making workbench-libs-v8-linklib-v8monolith-real in workbench/libs/v8
V8: Using real V8 engine from .../libv8_monolith.a
Installing V8 monolithic library to .../linklibs...
V8 monolithic library installed (libv8_monolith.a -> libv8monolith.a for -lv8monolith linker flag)

[MMAKE] Making workbench-libs-v8-linklib-v8monolith-stub in workbench/libs/v8
V8: Using real V8 engine from .../libv8_monolith.a
V8 monolithic library not available - using stubs only
```

Both targets executed, with the stub target overriding `V8_USELIBS` after the real target had prepared the library.

## Solution

Remove the MetaMake dependency directive (`#MM-`) from the stub target:

```makefile
V8_USELIBS := v8monolith stdc stdcio posixc pthread m
else
V8_USELIBS := stdc stdcio posixc
endif

# Stub target is defined but NOT registered as a MetaMake dependency.
# This prevents MetaMake from executing both -real and -stub targets.
# When V8 is not available, the build will use the stubs-only V8_USELIBS.
#MM
workbench-libs-v8-linklib-v8monolith-stub:
	@$(ECHO) "V8 monolithic library not available - using stubs only"
```

The stub target still exists (so the build system doesn't fail if it's accidentally invoked), but it's no longer registered as a MetaMake dependency.

## How It Works Now

### When V8 is Available (V8_USE_REAL_ENGINE=yes)
1. Line 98's `#MM-` directive makes `workbench-libs-v8-linklib-v8monolith-real` a dependency
2. The real target copies `libv8_monolith.a` to the linklibs directory
3. `V8_USELIBS` is set to include `v8monolith`
4. `%build_module` links against the V8 monolithic library
5. The stub target is NOT invoked

### When V8 is Not Available (V8_USE_REAL_ENGINE=no)
1. No MetaMake dependency is registered for the stub target
2. `V8_USELIBS` is set to exclude `v8monolith`
3. `%build_module` builds with stubs only (no V8 engine)
4. The stub target is NOT invoked (it's just a safety fallback)

## Comparison with Other AROS Libraries

This fix aligns with patterns used in other AROS libraries:

### Mesa3DGL
Uses `uselibs="glapi mesa mesa-sse41 compiler galliumauxiliary gallium mesautil pthread"` directly in `%build_module_library`, with all libraries built as separate linklib targets.

### FreeType2
Builds linklibs separately with `%build_linklib`, then references them via the module's `uselibs=` parameter. No conditional MetaMake directives that could conflict.

### LLVM
Has a stub/full approach like V8, but carefully avoids registering conflicting MetaMake dependencies by using separate target names (`workbench-libs-llvm-library` vs `workbench-libs-llvm-library-full`).

## Expected Build Behavior After Fix

### With V8 Available
```
[MMAKE] Making workbench-libs-v8-linklib-v8monolith-real in workbench/libs/v8
Installing V8 monolithic library to .../linklibs...
V8 monolithic library installed

[MMAKE] Making workbench-libs-v8 in workbench/libs/v8
Building Module  AROS/Libs/v8.library ...
```

The final `v8.library` should be significantly larger (>70MB) as it now includes the V8 engine code.

### Without V8 Available
```
[MMAKE] Making workbench-libs-v8 in workbench/libs/v8
V8: libv8_monolith.a not found, using Phase 2.2 stubs
Building Module  AROS/Libs/v8.library ...
```

The library will be built with API stubs only (~840KB).

## Testing

To verify the fix works:

1. **With V8 built:**
   ```bash
   mmake workbench-libs-v8
   ls -lh bin/linux-x86_64/AROS/Libs/v8.library
   # Should show >70MB
   ```

2. **Without V8:**
   ```bash
   rm -rf bin/linux-x86_64/gen/external/electron/v8/build
   mmake workbench-libs-v8
   ls -lh bin/linux-x86_64/AROS/Libs/v8.library
   # Should show ~840KB with stubs
   ```

3. **Check link dependencies:**
   ```bash
   nm -D bin/linux-x86_64/AROS/Libs/v8.library | grep v8::
   # Should show V8 engine symbols when built with real library
   ```

## Technical Details

### MetaMake Processing Order

1. MetaMake scans all files for `#MM` directives
2. Builds dependency graph
3. Invokes Make with appropriate targets

This happens **before** Make evaluates any conditional statements, which is why both `#MM-` directives were processed regardless of the `ifeq` condition.

### The `#MM-` Directive

`#MM- target : dependency` means "add dependency to target without creating a default rule". Both lines 98 and 110 were adding dependencies to the same target name, so both dependencies were registered.

## Commits

- Fix implementation: f961be3e60
- Documentation: c6b9ae421b

**File:** `workbench/libs/v8/mmakefile.src`  
**Lines changed:** 108-118
