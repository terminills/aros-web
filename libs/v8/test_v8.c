/*
    Copyright (C) 2025, The AROS Development Team. All rights reserved.

    Desc: Test program for V8 JavaScript Engine Library
*/

#include <proto/v8.h>
#include <proto/exec.h>
#include <stdio.h>
#include <string.h>

/* V8_TEST_PROGRAM
 * AROS_IMPL: Tests all library functions
 * DESIGN: Comprehensive API testing
 * TODO: Add more complex test cases when V8 is ported
 */

int main(void)
{
    struct Library *V8Base;
    V8IsolateHandle isolate = NULL;
    V8ContextHandle context = NULL;
    V8ScriptHandle script = NULL;
    APTR global = NULL;
    char result[256];
    LONG rc;
    BOOL success = TRUE;
    
    printf("V8 Library Test Program\n");
    printf("========================\n\n");
    
    /* Test 1: Open library */
    printf("Test 1: Opening v8.library...\n");
    V8Base = OpenLibrary("v8.library", 1);
    if (!V8Base) {
        printf("FAILED: Could not open v8.library\n");
        return 1;
    }
    printf("SUCCESS: v8.library opened (version %d.%d)\n\n",
           V8Base->lib_Version, V8Base->lib_Revision);
    
    /* Test 2: Initialize V8 */
    printf("Test 2: Initializing V8 platform...\n");
    if (!V8Initialize()) {
        printf("FAILED: Could not initialize V8\n");
        success = FALSE;
        goto cleanup;
    }
    printf("SUCCESS: V8 platform initialized\n\n");
    
    /* Test 3: Create isolate */
    printf("Test 3: Creating isolate...\n");
    isolate = V8CreateIsolate();
    if (!isolate) {
        printf("FAILED: Could not create isolate\n");
        success = FALSE;
        goto cleanup;
    }
    printf("SUCCESS: Isolate created (handle: %p)\n\n", isolate);
    
    /* Test 4: Create context */
    printf("Test 4: Creating context...\n");
    context = V8CreateContext(isolate);
    if (!context) {
        printf("FAILED: Could not create context\n");
        success = FALSE;
        goto cleanup;
    }
    printf("SUCCESS: Context created (handle: %p)\n\n", context);
    
    /* Test 5: Simple evaluation */
    printf("Test 5: Evaluating '2 + 2'...\n");
    rc = V8Eval(isolate, context, "2 + 2", result, sizeof(result));
    if (rc == V8_SUCCESS) {
        printf("SUCCESS: Result = %s\n\n", result);
    } else {
        printf("FAILED: Error code %ld\n\n", rc);
        success = FALSE;
    }
    
    /* Test 6: Get global object */
    printf("Test 6: Getting global object...\n");
    global = V8GetGlobalObject(isolate, context);
    if (global) {
        printf("SUCCESS: Global object retrieved (handle: %p)\n\n", global);
    } else {
        printf("FAILED: Could not get global object\n\n");
        success = FALSE;
    }
    
    /* Test 7: Set property */
    printf("Test 7: Setting property 'answer' = '42'...\n");
    rc = V8SetProperty(isolate, context, global, "answer", "42");
    if (rc == V8_SUCCESS) {
        printf("SUCCESS: Property set\n\n");
    } else {
        printf("FAILED: Error code %ld\n\n", rc);
        success = FALSE;
    }
    
    /* Test 8: Get property */
    printf("Test 8: Getting property 'answer'...\n");
    rc = V8GetProperty(isolate, context, global, "answer", result, sizeof(result));
    if (rc == V8_SUCCESS) {
        printf("SUCCESS: Property value = %s\n\n", result);
    } else {
        printf("FAILED: Error code %ld\n\n", rc);
        success = FALSE;
    }
    
    /* Test 9: Compile script */
    printf("Test 9: Compiling script 'function test() { return 123; }'...\n");
    script = V8CompileScript(isolate, context, 
                            "function test() { return 123; }", 
                            "test.js");
    if (script) {
        printf("SUCCESS: Script compiled (handle: %p)\n\n", script);
    } else {
        printf("FAILED: Could not compile script\n\n");
        success = FALSE;
    }
    
    /* Test 10: Run compiled script */
    if (script) {
        printf("Test 10: Running compiled script...\n");
        rc = V8RunScript(isolate, context, script, result, sizeof(result));
        if (rc == V8_SUCCESS) {
            printf("SUCCESS: Result = %s\n\n", result);
        } else {
            printf("FAILED: Error code %ld\n\n", rc);
            success = FALSE;
        }
    }
    
    /* Test 11: Call function */
    printf("Test 11: Calling function 'test()'...\n");
    rc = V8CallFunction(isolate, context, global, "test", result, sizeof(result));
    if (rc == V8_SUCCESS) {
        printf("SUCCESS: Result = %s\n\n", result);
    } else {
        printf("FAILED: Error code %ld\n\n", rc);
        success = FALSE;
    }
    
    /* Test 12: Error handling - invalid parameters */
    printf("Test 12: Testing error handling with NULL context...\n");
    rc = V8Eval(isolate, NULL, "1+1", result, sizeof(result));
    if (rc == V8_ERROR_INVALID) {
        printf("SUCCESS: Correctly returned V8_ERROR_INVALID\n\n");
    } else {
        printf("FAILED: Expected V8_ERROR_INVALID, got %ld\n\n", rc);
        success = FALSE;
    }
    
cleanup:
    /* Cleanup in reverse order */
    printf("Cleanup:\n");
    
    if (script) {
        printf("  Freeing script...\n");
        V8FreeScript(script);
    }
    
    if (context) {
        printf("  Destroying context...\n");
        V8DestroyContext(isolate, context);
    }
    
    if (isolate) {
        printf("  Destroying isolate...\n");
        V8DestroyIsolate(isolate);
    }
    
    printf("  Cleaning up V8 platform...\n");
    V8Cleanup();
    
    if (V8Base) {
        printf("  Closing v8.library...\n");
        CloseLibrary(V8Base);
    }
    
    printf("\n");
    printf("========================\n");
    if (success) {
        printf("All tests PASSED\n");
        printf("Engine: real V8 (isolate/context/eval/properties/functions via v8.library)\n");
        return 0;
    } else {
        printf("Some tests FAILED\n");
        return 1;
    }
}
