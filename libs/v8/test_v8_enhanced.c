/*
    Copyright (C) 2025, The AROS Development Team. All rights reserved.

    Desc: Enhanced test program for V8 library with simple evaluator
*/

#include <proto/v8.h>
#include <proto/exec.h>
#include <stdio.h>
#include <string.h>

/* V8_ENHANCED_TEST_PROGRAM
 * AROS_IMPL: Tests simple evaluator functionality
 * DESIGN: Comprehensive testing of property storage and expressions
 */

int main(void)
{
    struct Library *V8Base;
    V8IsolateHandle isolate = NULL;
    V8ContextHandle context = NULL;
    APTR global = NULL;
    char result[256];
    LONG rc;
    BOOL success = TRUE;
    
    printf("V8 Library Enhanced Test Program\n");
    printf("==================================\n\n");
    
    /* Open library */
    printf("Opening v8.library...\n");
    V8Base = OpenLibrary("v8.library", 1);
    if (!V8Base) {
        printf("FAILED: Could not open v8.library\n");
        return 1;
    }
    printf("SUCCESS: v8.library opened (version %d.%d)\n\n",
           V8Base->lib_Version, V8Base->lib_Revision);
    
    /* Initialize V8 */
    printf("Initializing V8 platform...\n");
    if (!V8Initialize()) {
        printf("FAILED: Could not initialize V8\n");
        success = FALSE;
        goto cleanup;
    }
    printf("SUCCESS: V8 platform initialized\n\n");
    
    /* Create isolate */
    printf("Creating isolate...\n");
    isolate = V8CreateIsolate();
    if (!isolate) {
        printf("FAILED: Could not create isolate\n");
        success = FALSE;
        goto cleanup;
    }
    printf("SUCCESS: Isolate created\n\n");
    
    /* Create context */
    printf("Creating context...\n");
    context = V8CreateContext(isolate);
    if (!context) {
        printf("FAILED: Could not create context\n");
        success = FALSE;
        goto cleanup;
    }
    printf("SUCCESS: Context created\n\n");
    
    /* Get global object */
    printf("Getting global object...\n");
    global = V8GetGlobalObject(isolate, context);
    if (!global) {
        printf("FAILED: Could not get global object\n");
        success = FALSE;
        goto cleanup;
    }
    printf("SUCCESS: Global object retrieved\n\n");
    
    /* Test 1: Simple number evaluation */
    printf("TEST 1: Evaluating '42'...\n");
    rc = V8Eval(isolate, context, "42", result, sizeof(result));
    if (rc == V8_SUCCESS) {
        printf("  Result: %s\n", result);
        if (strcmp(result, "42") == 0)
            printf("  ✓ PASS\n\n");
        else {
            printf("  ✗ FAIL: Expected '42'\n\n");
            success = FALSE;
        }
    } else {
        printf("  ✗ FAIL: Error code %ld\n\n", rc);
        success = FALSE;
    }
    
    /* Test 2: Simple arithmetic */
    printf("TEST 2: Evaluating '2 + 2'...\n");
    rc = V8Eval(isolate, context, "2 + 2", result, sizeof(result));
    if (rc == V8_SUCCESS) {
        printf("  Result: %s\n", result);
        if (strcmp(result, "4") == 0)
            printf("  ✓ PASS\n\n");
        else {
            printf("  ✗ FAIL: Expected '4'\n\n");
            success = FALSE;
        }
    } else {
        printf("  ✗ FAIL: Error code %ld\n\n", rc);
        success = FALSE;
    }
    
    /* Test 3: Multiplication */
    printf("TEST 3: Evaluating '5 * 3'...\n");
    rc = V8Eval(isolate, context, "5 * 3", result, sizeof(result));
    if (rc == V8_SUCCESS) {
        printf("  Result: %s\n", result);
        if (strcmp(result, "15") == 0)
            printf("  ✓ PASS\n\n");
        else {
            printf("  ✗ FAIL: Expected '15'\n\n");
            success = FALSE;
        }
    } else {
        printf("  ✗ FAIL: Error code %ld\n\n", rc);
        success = FALSE;
    }
    
    /* Test 4: String concatenation */
    printf("TEST 4: Evaluating '\"Hello\" + \"World\"'...\n");
    rc = V8Eval(isolate, context, "\"Hello\" + \"World\"", result, sizeof(result));
    if (rc == V8_SUCCESS) {
        printf("  Result: %s\n", result);
        if (strcmp(result, "HelloWorld") == 0)
            printf("  ✓ PASS\n\n");
        else {
            printf("  ✗ FAIL: Expected 'HelloWorld'\n\n");
            success = FALSE;
        }
    } else {
        printf("  ✗ FAIL: Error code %ld\n\n", rc);
        success = FALSE;
    }
    
    /* Test 5: Set property */
    printf("TEST 5: Setting property 'answer' = '42'...\n");
    rc = V8SetProperty(isolate, context, global, "answer", "42");
    if (rc == V8_SUCCESS) {
        printf("  ✓ PASS: Property set\n\n");
    } else {
        printf("  ✗ FAIL: Error code %ld\n\n", rc);
        success = FALSE;
    }
    
    /* Test 6: Get property */
    printf("TEST 6: Getting property 'answer'...\n");
    rc = V8GetProperty(isolate, context, global, "answer", result, sizeof(result));
    if (rc == V8_SUCCESS) {
        printf("  Result: %s\n", result);
        if (strcmp(result, "42") == 0)
            printf("  ✓ PASS\n\n");
        else {
            printf("  ✗ FAIL: Expected '42'\n\n");
            success = FALSE;
        }
    } else {
        printf("  ✗ FAIL: Error code %ld\n\n", rc);
        success = FALSE;
    }
    
    /* Test 7: Property reference in expression */
    printf("TEST 7: Setting property 'x' = '10' and evaluating 'x + 5'...\n");
    V8SetProperty(isolate, context, global, "x", "10");
    rc = V8Eval(isolate, context, "x + 5", result, sizeof(result));
    if (rc == V8_SUCCESS) {
        printf("  Result: %s\n", result);
        if (strcmp(result, "15") == 0)
            printf("  ✓ PASS\n\n");
        else {
            printf("  ✗ FAIL: Expected '15'\n\n");
            success = FALSE;
        }
    } else {
        printf("  ✗ FAIL: Error code %ld\n\n", rc);
        success = FALSE;
    }
    
    /* Test 8: Multiple properties in expression */
    printf("TEST 8: Setting 'a'='3', 'b'='7' and evaluating 'a * b'...\n");
    V8SetProperty(isolate, context, global, "a", "3");
    V8SetProperty(isolate, context, global, "b", "7");
    rc = V8Eval(isolate, context, "a * b", result, sizeof(result));
    if (rc == V8_SUCCESS) {
        printf("  Result: %s\n", result);
        if (strcmp(result, "21") == 0)
            printf("  ✓ PASS\n\n");
        else {
            printf("  ✗ FAIL: Expected '21'\n\n");
            success = FALSE;
        }
    } else {
        printf("  ✗ FAIL: Error code %ld\n\n", rc);
        success = FALSE;
    }
    
    /* Test 9: Undefined property */
    printf("TEST 9: Getting undefined property 'notexist'...\n");
    rc = V8GetProperty(isolate, context, global, "notexist", result, sizeof(result));
    if (rc == V8_SUCCESS) {
        printf("  Result: %s\n", result);
        if (strcmp(result, "undefined") == 0)
            printf("  ✓ PASS\n\n");
        else {
            printf("  ✗ FAIL: Expected 'undefined'\n\n");
            success = FALSE;
        }
    } else {
        printf("  ✗ FAIL: Error code %ld\n\n", rc);
        success = FALSE;
    }
    
    /* Test 10: Division */
    printf("TEST 10: Evaluating '100 / 4'...\n");
    rc = V8Eval(isolate, context, "100 / 4", result, sizeof(result));
    if (rc == V8_SUCCESS) {
        printf("  Result: %s\n", result);
        if (strcmp(result, "25") == 0)
            printf("  ✓ PASS\n\n");
        else {
            printf("  ✗ FAIL: Expected '25'\n\n");
            success = FALSE;
        }
    } else {
        printf("  ✗ FAIL: Error code %ld\n\n", rc);
        success = FALSE;
    }
    
    /* Test 11: Function call placeholder */
    printf("TEST 11: Setting 'testFunc'='123' and calling it...\n");
    V8SetProperty(isolate, context, global, "testFunc", "123");
    rc = V8CallFunction(isolate, context, global, "testFunc", result, sizeof(result));
    if (rc == V8_SUCCESS) {
        printf("  Result: %s\n", result);
        if (strcmp(result, "123") == 0)
            printf("  ✓ PASS\n\n");
        else {
            printf("  ✗ FAIL: Expected '123'\n\n");
            success = FALSE;
        }
    } else {
        printf("  ✗ FAIL: Error code %ld\n\n", rc);
        success = FALSE;
    }
    
cleanup:
    /* Cleanup in reverse order */
    printf("Cleanup:\n");
    
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
    printf("==================================\n");
    if (success) {
        printf("✓ All tests PASSED\n");
        printf("\nNote: Simple expression evaluator working.\n");
        printf("Full V8 engine integration still pending.\n");
        return 0;
    } else {
        printf("✗ Some tests FAILED\n");
        return 1;
    }
}
