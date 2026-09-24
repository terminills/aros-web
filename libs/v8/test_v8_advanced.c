/*
    Copyright (C) 2025, The AROS Development Team. All rights reserved.

    Desc: Advanced test program for V8 library - testing new operators
*/

#include <proto/v8.h>
#include <proto/exec.h>
#include <stdio.h>
#include <string.h>

/* V8_ADVANCED_TEST_PROGRAM
 * AROS_IMPL: Tests comparison, logical operators, and boolean support
 * DESIGN: Comprehensive API testing for new features
 */

int main(void)
{
    struct Library *V8Base;
    V8IsolateHandle isolate = NULL;
    V8ContextHandle context = NULL;
    APTR global = NULL;
    char result[256];
    LONG rc;
    int passed = 0, failed = 0;
    
    printf("V8 Library Advanced Test Program\n");
    printf("==================================\n\n");
    
    /* Open library */
    V8Base = OpenLibrary("v8.library", 1);
    if (!V8Base) {
        printf("FAILED: Could not open v8.library\n");
        return 1;
    }
    printf("Library opened (version %d.%d)\n\n", 
           V8Base->lib_Version, V8Base->lib_Revision);
    
    /* Initialize V8 */
    if (!V8Initialize()) {
        printf("FAILED: Could not initialize V8\n");
        CloseLibrary(V8Base);
        return 1;
    }
    
    /* Create isolate and context */
    isolate = V8CreateIsolate();
    if (!isolate) {
        printf("FAILED: Could not create isolate\n");
        V8Cleanup();
        CloseLibrary(V8Base);
        return 1;
    }
    
    context = V8CreateContext(isolate);
    if (!context) {
        printf("FAILED: Could not create context\n");
        V8DestroyIsolate(isolate);
        V8Cleanup();
        CloseLibrary(V8Base);
        return 1;
    }
    
    global = V8GetGlobalObject(isolate, context);
    if (!global) {
        printf("FAILED: Could not get global object\n");
        V8DestroyContext(isolate, context);
        V8DestroyIsolate(isolate);
        V8Cleanup();
        CloseLibrary(V8Base);
        return 1;
    }
    
    printf("Setup complete. Running tests...\n\n");
    
    /* Test 1: Boolean literals */
    printf("Test 1: Boolean literal 'true'...\n");
    rc = V8Eval(isolate, context, "true", result, sizeof(result));
    if (rc == V8_SUCCESS && strcmp(result, "true") == 0) {
        printf("  ✓ PASS: %s\n\n", result);
        passed++;
    } else {
        printf("  ✗ FAIL: Expected 'true', got '%s' (rc=%ld)\n\n", result, rc);
        failed++;
    }
    
    /* Test 2: Boolean literal false */
    printf("Test 2: Boolean literal 'false'...\n");
    rc = V8Eval(isolate, context, "false", result, sizeof(result));
    if (rc == V8_SUCCESS && strcmp(result, "false") == 0) {
        printf("  ✓ PASS: %s\n\n", result);
        passed++;
    } else {
        printf("  ✗ FAIL: Expected 'false', got '%s' (rc=%ld)\n\n", result, rc);
        failed++;
    }
    
    /* Test 3: Null */
    printf("Test 3: Null literal 'null'...\n");
    rc = V8Eval(isolate, context, "null", result, sizeof(result));
    if (rc == V8_SUCCESS && strcmp(result, "null") == 0) {
        printf("  ✓ PASS: %s\n\n", result);
        passed++;
    } else {
        printf("  ✗ FAIL: Expected 'null', got '%s' (rc=%ld)\n\n", result, rc);
        failed++;
    }
    
    /* Test 4: Undefined */
    printf("Test 4: Undefined literal 'undefined'...\n");
    rc = V8Eval(isolate, context, "undefined", result, sizeof(result));
    if (rc == V8_SUCCESS && strcmp(result, "undefined") == 0) {
        printf("  ✓ PASS: %s\n\n", result);
        passed++;
    } else {
        printf("  ✗ FAIL: Expected 'undefined', got '%s' (rc=%ld)\n\n", result, rc);
        failed++;
    }
    
    /* Test 5: Comparison == (true) */
    printf("Test 5: Comparison '5 == 5'...\n");
    rc = V8Eval(isolate, context, "5 == 5", result, sizeof(result));
    if (rc == V8_SUCCESS && strcmp(result, "true") == 0) {
        printf("  ✓ PASS: %s\n\n", result);
        passed++;
    } else {
        printf("  ✗ FAIL: Expected 'true', got '%s' (rc=%ld)\n\n", result, rc);
        failed++;
    }
    
    /* Test 6: Comparison == (false) */
    printf("Test 6: Comparison '5 == 3'...\n");
    rc = V8Eval(isolate, context, "5 == 3", result, sizeof(result));
    if (rc == V8_SUCCESS && strcmp(result, "false") == 0) {
        printf("  ✓ PASS: %s\n\n", result);
        passed++;
    } else {
        printf("  ✗ FAIL: Expected 'false', got '%s' (rc=%ld)\n\n", result, rc);
        failed++;
    }
    
    /* Test 7: Comparison != */
    printf("Test 7: Comparison '5 != 3'...\n");
    rc = V8Eval(isolate, context, "5 != 3", result, sizeof(result));
    if (rc == V8_SUCCESS && strcmp(result, "true") == 0) {
        printf("  ✓ PASS: %s\n\n", result);
        passed++;
    } else {
        printf("  ✗ FAIL: Expected 'true', got '%s' (rc=%ld)\n\n", result, rc);
        failed++;
    }
    
    /* Test 8: Comparison < */
    printf("Test 8: Comparison '3 < 5'...\n");
    rc = V8Eval(isolate, context, "3 < 5", result, sizeof(result));
    if (rc == V8_SUCCESS && strcmp(result, "true") == 0) {
        printf("  ✓ PASS: %s\n\n", result);
        passed++;
    } else {
        printf("  ✗ FAIL: Expected 'true', got '%s' (rc=%ld)\n\n", result, rc);
        failed++;
    }
    
    /* Test 9: Comparison > */
    printf("Test 9: Comparison '5 > 3'...\n");
    rc = V8Eval(isolate, context, "5 > 3", result, sizeof(result));
    if (rc == V8_SUCCESS && strcmp(result, "true") == 0) {
        printf("  ✓ PASS: %s\n\n", result);
        passed++;
    } else {
        printf("  ✗ FAIL: Expected 'true', got '%s' (rc=%ld)\n\n", result, rc);
        failed++;
    }
    
    /* Test 10: Comparison <= */
    printf("Test 10: Comparison '5 <= 5'...\n");
    rc = V8Eval(isolate, context, "5 <= 5", result, sizeof(result));
    if (rc == V8_SUCCESS && strcmp(result, "true") == 0) {
        printf("  ✓ PASS: %s\n\n", result);
        passed++;
    } else {
        printf("  ✗ FAIL: Expected 'true', got '%s' (rc=%ld)\n\n", result, rc);
        failed++;
    }
    
    /* Test 11: Comparison >= */
    printf("Test 11: Comparison '5 >= 3'...\n");
    rc = V8Eval(isolate, context, "5 >= 3", result, sizeof(result));
    if (rc == V8_SUCCESS && strcmp(result, "true") == 0) {
        printf("  ✓ PASS: %s\n\n", result);
        passed++;
    } else {
        printf("  ✗ FAIL: Expected 'true', got '%s' (rc=%ld)\n\n", result, rc);
        failed++;
    }
    
    /* Test 12: String comparison */
    printf("Test 12: String comparison '\"hello\" == \"hello\"'...\n");
    rc = V8Eval(isolate, context, "\"hello\" == \"hello\"", result, sizeof(result));
    if (rc == V8_SUCCESS && strcmp(result, "true") == 0) {
        printf("  ✓ PASS: %s\n\n", result);
        passed++;
    } else {
        printf("  ✗ FAIL: Expected 'true', got '%s' (rc=%ld)\n\n", result, rc);
        failed++;
    }
    
    /* Test 13: Logical AND (true) */
    printf("Test 13: Logical AND 'true && true'...\n");
    rc = V8Eval(isolate, context, "true && true", result, sizeof(result));
    if (rc == V8_SUCCESS && strcmp(result, "true") == 0) {
        printf("  ✓ PASS: %s\n\n", result);
        passed++;
    } else {
        printf("  ✗ FAIL: Expected 'true', got '%s' (rc=%ld)\n\n", result, rc);
        failed++;
    }
    
    /* Test 14: Logical AND (false) */
    printf("Test 14: Logical AND 'true && false'...\n");
    rc = V8Eval(isolate, context, "true && false", result, sizeof(result));
    if (rc == V8_SUCCESS && strcmp(result, "false") == 0) {
        printf("  ✓ PASS: %s\n\n", result);
        passed++;
    } else {
        printf("  ✗ FAIL: Expected 'false', got '%s' (rc=%ld)\n\n", result, rc);
        failed++;
    }
    
    /* Test 15: Logical OR (true) */
    printf("Test 15: Logical OR 'true || false'...\n");
    rc = V8Eval(isolate, context, "true || false", result, sizeof(result));
    if (rc == V8_SUCCESS && strcmp(result, "true") == 0) {
        printf("  ✓ PASS: %s\n\n", result);
        passed++;
    } else {
        printf("  ✗ FAIL: Expected 'true', got '%s' (rc=%ld)\n\n", result, rc);
        failed++;
    }
    
    /* Test 16: Logical OR (false) */
    printf("Test 16: Logical OR 'false || false'...\n");
    rc = V8Eval(isolate, context, "false || false", result, sizeof(result));
    if (rc == V8_SUCCESS && strcmp(result, "false") == 0) {
        printf("  ✓ PASS: %s\n\n", result);
        passed++;
    } else {
        printf("  ✗ FAIL: Expected 'false', got '%s' (rc=%ld)\n\n", result, rc);
        failed++;
    }
    
    /* Test 17: Logical NOT (true) */
    printf("Test 17: Logical NOT '!false'...\n");
    rc = V8Eval(isolate, context, "!false", result, sizeof(result));
    if (rc == V8_SUCCESS && strcmp(result, "true") == 0) {
        printf("  ✓ PASS: %s\n\n", result);
        passed++;
    } else {
        printf("  ✗ FAIL: Expected 'true', got '%s' (rc=%ld)\n\n", result, rc);
        failed++;
    }
    
    /* Test 18: Logical NOT (false) */
    printf("Test 18: Logical NOT '!true'...\n");
    rc = V8Eval(isolate, context, "!true", result, sizeof(result));
    if (rc == V8_SUCCESS && strcmp(result, "false") == 0) {
        printf("  ✓ PASS: %s\n\n", result);
        passed++;
    } else {
        printf("  ✗ FAIL: Expected 'false', got '%s' (rc=%ld)\n\n", result, rc);
        failed++;
    }
    
    /* Test 19: Property with boolean */
    printf("Test 19: Property with boolean value...\n");
    V8SetProperty(isolate, context, global, "isReady", "true");
    rc = V8GetProperty(isolate, context, global, "isReady", result, sizeof(result));
    if (rc == V8_SUCCESS && strcmp(result, "true") == 0) {
        printf("  ✓ PASS: %s\n\n", result);
        passed++;
    } else {
        printf("  ✗ FAIL: Expected 'true', got '%s' (rc=%ld)\n\n", result, rc);
        failed++;
    }
    
    /* Test 20: Comparison with property */
    printf("Test 20: Comparison with property '10 > x' where x=5...\n");
    V8SetProperty(isolate, context, global, "x", "5");
    rc = V8Eval(isolate, context, "10 > x", result, sizeof(result));
    if (rc == V8_SUCCESS && strcmp(result, "true") == 0) {
        printf("  ✓ PASS: %s\n\n", result);
        passed++;
    } else {
        printf("  ✗ FAIL: Expected 'true', got '%s' (rc=%ld)\n\n", result, rc);
        failed++;
    }
    
    /* Cleanup */
    V8DestroyContext(isolate, context);
    V8DestroyIsolate(isolate);
    V8Cleanup();
    CloseLibrary(V8Base);
    
    /* Summary */
    printf("\n==================================\n");
    printf("Test Results:\n");
    printf("  Passed: %d\n", passed);
    printf("  Failed: %d\n", failed);
    printf("  Total:  %d\n", passed + failed);
    printf("==================================\n\n");
    
    if (failed == 0) {
        printf("✓ All tests PASSED\n");
        printf("\nEnhanced features working:\n");
        printf("  - Boolean literals (true, false)\n");
        printf("  - Null and undefined\n");
        printf("  - Comparison operators (==, !=, <, >, <=, >=)\n");
        printf("  - Logical operators (&&, ||, !)\n");
        printf("  - Property integration with new types\n");
        return 0;
    } else {
        printf("✗ Some tests FAILED\n");
        return 1;
    }
}
