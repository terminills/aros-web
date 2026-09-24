# Quick Test Example for V8.library

This is a minimal example showing how to quickly verify V8.library is working.

## Minimal Test Program

Save this as `quick_v8_test.c`:

```c
#include <proto/v8.h>
#include <proto/exec.h>
#include <stdio.h>
#include <string.h>

struct Library *V8Base = NULL;

int main(void)
{
    printf("Quick V8.library Test\n");
    printf("=====================\n\n");
    
    /* Open the library */
    V8Base = OpenLibrary("v8.library", 0);
    if (!V8Base) {
        printf("ERROR: Could not open v8.library\n");
        return 1;
    }
    printf("✓ v8.library opened\n");
    
    /* Initialize V8 */
    APTR platform = V8Initialize();
    if (!platform) {
        printf("ERROR: V8Initialize() failed\n");
        CloseLibrary(V8Base);
        return 1;
    }
    printf("✓ V8 initialized\n");
    
    /* Create isolate and context */
    APTR isolate = V8CreateIsolate();
    if (!isolate) {
        printf("ERROR: Failed to create isolate\n");
        V8Cleanup();
        CloseLibrary(V8Base);
        return 1;
    }
    printf("✓ Isolate created\n");
    
    APTR context = V8CreateContext(isolate);
    if (!context) {
        printf("ERROR: Failed to create context\n");
        V8DestroyIsolate(isolate);
        V8Cleanup();
        CloseLibrary(V8Base);
        return 1;
    }
    printf("✓ Context created\n");
    
    /* Test basic evaluation */
    char result[256];
    LONG status = V8Eval(context, "2 + 2", result, sizeof(result));
    
    if (status == 0) {
        printf("✓ JavaScript evaluation works\n");
        printf("  Expression: 2 + 2\n");
        printf("  Result: %s\n", result);
        
        if (strcmp(result, "4") == 0) {
            printf("\n✓✓✓ SUCCESS ✓✓✓\n");
            printf("V8.library is working correctly!\n");
        } else {
            printf("\n✗ Result mismatch (expected 4, got %s)\n", result);
        }
    } else {
        printf("✗ Evaluation failed: %s\n", result);
    }
    
    /* Cleanup */
    V8DestroyContext(context);
    V8DestroyIsolate(isolate);
    V8Cleanup();
    CloseLibrary(V8Base);
    
    printf("\n✓ Cleanup complete\n");
    return 0;
}
```

## Building

### Using mmake

Add to mmakefile.src:

```makefile
#MM workbench-libs-v8-quick-test : workbench-libs-v8
%build_prog mmake=workbench-libs-v8-quick-test \
    progname=quick_v8_test \
    files=quick_v8_test \
    uselibs="v8"
```

Then build:

```bash
mmake workbench-libs-v8-quick-test
```

### Manual Build

```bash
gcc -o quick_v8_test quick_v8_test.c -I/path/to/AROS/includes -lv8
```

## Running

```bash
./quick_v8_test
```

## Expected Output

```
Quick V8.library Test
=====================

✓ v8.library opened
✓ V8 initialized
✓ Isolate created
✓ Context created
✓ JavaScript evaluation works
  Expression: 2 + 2
  Result: 4

✓✓✓ SUCCESS ✓✓✓
V8.library is working correctly!

✓ Cleanup complete
```

## What This Tests

This minimal test validates:

1. ✓ v8.library can be opened
2. ✓ V8 platform initializes
3. ✓ Isolates can be created
4. ✓ Contexts can be created
5. ✓ JavaScript code can be evaluated
6. ✓ Cleanup works properly

If this test passes, v8.library is functioning correctly!

## Next Steps

After this quick test passes, run the comprehensive test suite:

```bash
mmake workbench-libs-v8-test-library
./test_v8_library
```

See [TEST_GUIDE.md](TEST_GUIDE.md) for detailed testing instructions.

## Troubleshooting

### "Could not open v8.library"

Make sure v8.library is installed:
```bash
ls -l LIBS:v8.library
```

If missing, build it:
```bash
mmake workbench-libs-v8
```

### "V8Initialize() failed"

Check:
- Available memory (need ~100MB)
- V8 platform backend is built
- glibc stubs are available

### Evaluation Returns Wrong Result

This could mean:
- Stub evaluator is being used (limited functionality)
- Need to build with real V8 engine (libv8_monolith.a)

Check if V8 is using real engine:
```bash
strings LIBS:v8.library | grep -i v8_monolith
```

If no result, rebuild with V8:
```bash
mmake external-v8
mmake workbench-libs-v8
```

## Testing Specific Features

### Test String Concatenation

```c
V8Eval(context, "'Hello ' + 'AROS'", result, sizeof(result));
printf("Result: %s\n", result);  // Should print: Hello AROS
```

### Test Functions

```c
V8Eval(context, "function greet(name) { return 'Hello, ' + name; } greet('User')", 
       result, sizeof(result));
printf("Result: %s\n", result);  // Should print: Hello, User
```

### Test Arrays

```c
V8Eval(context, "[1, 2, 3].length", result, sizeof(result));
printf("Result: %s\n", result);  // Should print: 3
```

### Test Error Handling

```c
status = V8Eval(context, "invalid syntax +++", result, sizeof(result));
if (status != 0) {
    printf("Error caught: %s\n", result);
}
```

## Performance Check

Add timing to see how fast V8 evaluates:

```c
/* AROS-specific timer includes */
#include <proto/timer.h>
#include <devices/timer.h>

struct timeval start, end;
int i;

GetSysTime(&start);

for (i = 0; i < 1000; i++) {
    V8Eval(context, "2 + 2", result, sizeof(result));
}

GetSysTime(&end);
SubTime(&end, &start);
printf("1000 evaluations: %ld.%06ld seconds\n", 
       end.tv_secs, end.tv_micro);
```

## Conclusion

This quick test is perfect for:

- ✓ Verifying v8.library installation
- ✓ Checking basic functionality
- ✓ Quick smoke test during development
- ✓ CI/CD validation

For comprehensive testing, use `test_v8_library` from the full test suite.
