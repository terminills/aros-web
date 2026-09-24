# V8 Library Integration Guide for AROS Applications

## Overview

This guide explains how to integrate the V8 JavaScript engine library into AROS applications. The V8 library provides JavaScript execution capabilities through a simple C API.

## Prerequisites

- AROS development environment
- v8.library installed on the system
- C compiler (GCC recommended)

## Including the Library

### Header Files

Include the V8 library header in your application:

```c
#include <proto/v8.h>
#include <libraries/v8.h>
```

### Opening the Library

Open the library at the start of your application:

```c
struct Library *V8Base;

V8Base = OpenLibrary("v8.library", 1);
if (!V8Base) {
    /* Error: Library not available */
    return ERROR;
}
```

### Closing the Library

Always close the library when done:

```c
if (V8Base) {
    CloseLibrary(V8Base);
    V8Base = NULL;
}
```

## Basic Usage Pattern

### 1. Initialize V8

```c
if (!V8Initialize()) {
    printf("Failed to initialize V8\n");
    return ERROR;
}
```

### 2. Create Isolate and Context

```c
V8IsolateHandle isolate = V8CreateIsolate();
if (!isolate) {
    printf("Failed to create isolate\n");
    V8Cleanup();
    return ERROR;
}

V8ContextHandle context = V8CreateContext(isolate);
if (!context) {
    printf("Failed to create context\n");
    V8DestroyIsolate(isolate);
    V8Cleanup();
    return ERROR;
}
```

### 3. Execute JavaScript

```c
char result[256];
LONG rc;

rc = V8Eval(isolate, context, "2 + 2", result, sizeof(result));
if (rc == V8_SUCCESS) {
    printf("Result: %s\n", result);
} else {
    printf("Error: %ld\n", rc);
}
```

### 4. Cleanup

```c
V8DestroyContext(isolate, context);
V8DestroyIsolate(isolate);
V8Cleanup();
```

## Common Use Cases

### Simple Script Evaluation

Evaluate JavaScript and get the result:

```c
char result[256];
const char *script = "Math.sqrt(144)";

LONG rc = V8Eval(isolate, context, script, result, sizeof(result));
if (rc == V8_SUCCESS) {
    printf("Square root: %s\n", result);
}
```

### Compiled Scripts (Caching)

For scripts that will be executed multiple times:

```c
/* Compile once */
V8ScriptHandle script = V8CompileScript(isolate, context,
    "function calculate(x) { return x * 2; }",
    "calc.js");

if (script) {
    /* Run multiple times */
    for (int i = 0; i < 10; i++) {
        char result[256];
        V8RunScript(isolate, context, script, result, sizeof(result));
        /* Process result */
    }
    
    /* Free when done */
    V8FreeScript(script);
}
```

### Setting Global Variables

Set variables accessible from JavaScript:

```c
APTR global = V8GetGlobalObject(isolate, context);

V8SetProperty(isolate, context, global, "appName", "MyAROSApp");
V8SetProperty(isolate, context, global, "version", "1.0");

/* Now JavaScript can access these:
 * console.log(appName);  // "MyAROSApp"
 * console.log(version);  // "1.0"
 */
```

### Reading Global Variables

Read variables set by JavaScript:

```c
/* JavaScript: var result = 42; */
V8Eval(isolate, context, "var result = 42;", NULL, 0);

/* Read it from C */
char value[256];
APTR global = V8GetGlobalObject(isolate, context);
if (V8GetProperty(isolate, context, global, "result", 
                  value, sizeof(value)) == V8_SUCCESS) {
    printf("Result from JS: %s\n", value);
}
```

### Calling JavaScript Functions

Call JavaScript functions by name:

```c
/* Define function in JavaScript */
V8Eval(isolate, context, 
       "function greet(name) { return 'Hello, ' + name; }", 
       NULL, 0);

/* Call it from C */
char greeting[256];
APTR global = V8GetGlobalObject(isolate, context);
if (V8CallFunction(isolate, context, global, "greet", 
                   greeting, sizeof(greeting)) == V8_SUCCESS) {
    printf("%s\n", greeting);
}
```

## Error Handling

### Return Codes

All V8 functions return error codes:

```c
LONG rc = V8Eval(isolate, context, script, result, sizeof(result));

switch (rc) {
    case V8_SUCCESS:
        /* Success */
        break;
    case V8_ERROR_SYNTAX:
        printf("Syntax error in JavaScript\n");
        break;
    case V8_ERROR_COMPILE:
        printf("Compilation error\n");
        break;
    case V8_ERROR_RUNTIME:
        printf("Runtime error\n");
        break;
    case V8_ERROR_NOMEM:
        printf("Out of memory or buffer too small\n");
        break;
    case V8_ERROR_INVALID:
        printf("Invalid parameter\n");
        break;
    default:
        printf("Unknown error: %ld\n", rc);
}
```

### Best Practices

1. **Always check return values**
   ```c
   if (V8Initialize() == NULL) {
       /* Handle error */
   }
   ```

2. **Validate handles**
   ```c
   V8IsolateHandle isolate = V8CreateIsolate();
   if (!isolate) {
       /* Handle error */
   }
   ```

3. **Use appropriate buffer sizes**
   ```c
   char result[1024];  /* Large enough for expected results */
   ```

4. **Clean up in reverse order**
   ```c
   V8DestroyContext(isolate, context);
   V8DestroyIsolate(isolate);
   V8Cleanup();
   ```

## Advanced Topics

### Multiple Contexts

Run isolated JavaScript code in separate contexts:

```c
V8IsolateHandle isolate = V8CreateIsolate();

V8ContextHandle context1 = V8CreateContext(isolate);
V8ContextHandle context2 = V8CreateContext(isolate);

/* Set variable in context1 */
APTR global1 = V8GetGlobalObject(isolate, context1);
V8SetProperty(isolate, context1, global1, "x", "10");

/* context2 cannot see it */
char value[256];
APTR global2 = V8GetGlobalObject(isolate, context2);
V8GetProperty(isolate, context2, global2, "x", value, sizeof(value));
/* value will be "undefined" */

V8DestroyContext(isolate, context1);
V8DestroyContext(isolate, context2);
V8DestroyIsolate(isolate);
```

### Multiple Isolates

Run JavaScript on different threads (when V8 is ported):

```c
/* Thread 1 */
V8IsolateHandle isolate1 = V8CreateIsolate();
V8ContextHandle context1 = V8CreateContext(isolate1);
/* Use isolate1... */

/* Thread 2 */
V8IsolateHandle isolate2 = V8CreateIsolate();
V8ContextHandle context2 = V8CreateContext(isolate2);
/* Use isolate2... */

/* Isolates are completely independent */
```

## Building Applications

### Makefile Example

```makefile
CC = gcc
CFLAGS = -I/usr/include/aros
LIBS = -lv8

myapp: myapp.c
	$(CC) $(CFLAGS) -o myapp myapp.c $(LIBS)
```

### GCC Command Line

```bash
gcc -o myapp myapp.c -I/usr/include/aros -lv8
```

## Complete Example

```c
#include <proto/v8.h>
#include <proto/exec.h>
#include <stdio.h>

int main(void)
{
    struct Library *V8Base;
    V8IsolateHandle isolate = NULL;
    V8ContextHandle context = NULL;
    char result[256];
    LONG rc;
    int status = 0;
    
    /* Open library */
    V8Base = OpenLibrary("v8.library", 1);
    if (!V8Base) {
        printf("Error: Cannot open v8.library\n");
        return 1;
    }
    
    /* Initialize V8 */
    if (!V8Initialize()) {
        printf("Error: Cannot initialize V8\n");
        status = 1;
        goto cleanup;
    }
    
    /* Create isolate */
    isolate = V8CreateIsolate();
    if (!isolate) {
        printf("Error: Cannot create isolate\n");
        status = 1;
        goto cleanup;
    }
    
    /* Create context */
    context = V8CreateContext(isolate);
    if (!context) {
        printf("Error: Cannot create context\n");
        status = 1;
        goto cleanup;
    }
    
    /* Execute JavaScript */
    rc = V8Eval(isolate, context, 
                "var x = 10; var y = 20; x + y", 
                result, sizeof(result));
    
    if (rc == V8_SUCCESS) {
        printf("Result: %s\n", result);
    } else {
        printf("Error executing JavaScript: %ld\n", rc);
        status = 1;
    }
    
cleanup:
    /* Clean up */
    if (context) V8DestroyContext(isolate, context);
    if (isolate) V8DestroyIsolate(isolate);
    V8Cleanup();
    if (V8Base) CloseLibrary(V8Base);
    
    return status;
}
```

## Troubleshooting

### Library Not Found

**Problem**: `OpenLibrary("v8.library", 1)` returns NULL

**Solutions**:
- Ensure v8.library is installed in LIBS:
- Check library version matches
- Verify system requirements are met

### Initialization Fails

**Problem**: `V8Initialize()` returns NULL

**Solutions**:
- Check system memory availability
- Ensure no other instance is running
- Check V8 platform prerequisites

### Runtime Errors

**Problem**: JavaScript code fails to execute

**Solutions**:
- Check JavaScript syntax
- Verify context is valid
- Check result buffer size
- Review error codes

## API Reference

See `README.md` for complete API documentation.

## Notes on Current Implementation

**Important**: The current implementation is a framework with stub functions. All APIs are in place and work correctly from a C perspective, but the actual V8 JavaScript engine is not yet ported to AROS.

When V8 is fully ported:
- All functions will execute actual JavaScript
- JIT compilation will be available
- Full V8 feature set will be accessible

For now, the library:
- Loads and initializes correctly
- Validates all parameters
- Returns appropriate error codes
- Manages memory correctly
- Provides correct API structure

This allows you to develop applications now that will work when V8 is ported.

## License

This guide is part of the AROS V8 library project and follows AROS license terms.

## Support

For questions and issues:
- AROS project website
- AROS development mailing lists
- GitHub repository issues
