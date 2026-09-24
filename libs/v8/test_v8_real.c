/*
    Test for V8 Real Engine Integration
    
    This test verifies that v8.library is properly integrated with
    the real V8 JavaScript engine (libv8_monolith.a).
*/

#include <proto/v8.h>
#include <proto/exec.h>
#include <stdio.h>
#include <string.h>

struct Library *V8Base = NULL;

static void test_basic_arithmetic(void)
{
    printf("\n=== Test 1: Basic Arithmetic ===\n");
    
    APTR isolate = V8CreateIsolate();
    if (!isolate) {
        printf("ERROR: Failed to create isolate\n");
        return;
    }
    
    APTR context = V8CreateContext(isolate);
    if (!context) {
        printf("ERROR: Failed to create context\n");
        V8DestroyIsolate(isolate);
        return;
    }
    
    char result[256];
    LONG status = V8Eval(isolate, context, "2 + 2", result, sizeof(result));
    
    if (status == 0) {
        printf("  Expression: 2 + 2\n");
        printf("  Result: %s\n", result);
        printf("  Expected: 4\n");
        if (strcmp(result, "4") == 0) {
            printf("  ✓ PASS\n");
        } else {
            printf("  ✗ FAIL: Expected '4', got '%s'\n", result);
        }
    } else {
        printf("  ✗ FAIL: Evaluation failed: %s\n", result);
    }
    
    V8DestroyContext(isolate, context);
    V8DestroyIsolate(isolate);
}

static void test_string_concatenation(void)
{
    printf("\n=== Test 2: String Concatenation ===\n");
    
    APTR isolate = V8CreateIsolate();
    if (!isolate) {
        printf("ERROR: Failed to create isolate\n");
        return;
    }
    
    APTR context = V8CreateContext(isolate);
    if (!context) {
        printf("ERROR: Failed to create context\n");
        V8DestroyIsolate(isolate);
        return;
    }
    
    char result[256];
    LONG status = V8Eval(isolate, context, "'Hello ' + 'AROS'", result, sizeof(result));
    
    if (status == 0) {
        printf("  Expression: 'Hello ' + 'AROS'\n");
        printf("  Result: %s\n", result);
        printf("  Expected: Hello AROS\n");
        if (strcmp(result, "Hello AROS") == 0) {
            printf("  ✓ PASS\n");
        } else {
            printf("  ✗ FAIL: Expected 'Hello AROS', got '%s'\n", result);
        }
    } else {
        printf("  ✗ FAIL: Evaluation failed: %s\n", result);
    }
    
    V8DestroyContext(isolate, context);
    V8DestroyIsolate(isolate);
}

static void test_function_definition(void)
{
    printf("\n=== Test 3: Function Definition ===\n");
    
    APTR isolate = V8CreateIsolate();
    if (!isolate) {
        printf("ERROR: Failed to create isolate\n");
        return;
    }
    
    APTR context = V8CreateContext(isolate);
    if (!context) {
        printf("ERROR: Failed to create context\n");
        V8DestroyIsolate(isolate);
        return;
    }
    
    char result[256];
    LONG status = V8Eval(isolate, context,
                        "function double(x) { return x * 2; } double(21)",
                        result, sizeof(result));
    
    if (status == 0) {
        printf("  Expression: function double(x) { return x * 2; } double(21)\n");
        printf("  Result: %s\n", result);
        printf("  Expected: 42\n");
        if (strcmp(result, "42") == 0) {
            printf("  ✓ PASS\n");
        } else {
            printf("  ✗ FAIL: Expected '42', got '%s'\n", result);
        }
    } else {
        printf("  ✗ FAIL: Evaluation failed: %s\n", result);
    }
    
    V8DestroyContext(isolate, context);
    V8DestroyIsolate(isolate);
}

static void test_array_operations(void)
{
    printf("\n=== Test 4: Array Operations ===\n");
    
    APTR isolate = V8CreateIsolate();
    if (!isolate) {
        printf("ERROR: Failed to create isolate\n");
        return;
    }
    
    APTR context = V8CreateContext(isolate);
    if (!context) {
        printf("ERROR: Failed to create context\n");
        V8DestroyIsolate(isolate);
        return;
    }
    
    char result[256];
    LONG status = V8Eval(isolate, context, "[1, 2, 3].length", result, sizeof(result));
    
    if (status == 0) {
        printf("  Expression: [1, 2, 3].length\n");
        printf("  Result: %s\n", result);
        printf("  Expected: 3\n");
        if (strcmp(result, "3") == 0) {
            printf("  ✓ PASS\n");
        } else {
            printf("  ✗ FAIL: Expected '3', got '%s'\n", result);
        }
    } else {
        printf("  ✗ FAIL: Evaluation failed: %s\n", result);
    }
    
    V8DestroyContext(isolate, context);
    V8DestroyIsolate(isolate);
}

static void test_error_handling(void)
{
    printf("\n=== Test 5: Error Handling ===\n");
    
    APTR isolate = V8CreateIsolate();
    if (!isolate) {
        printf("ERROR: Failed to create isolate\n");
        return;
    }
    
    APTR context = V8CreateContext(isolate);
    if (!context) {
        printf("ERROR: Failed to create context\n");
        V8DestroyIsolate(isolate);
        return;
    }
    
    char result[256];
    LONG status = V8Eval(isolate, context, "undefinedFunction()", result, sizeof(result));
    
    if (status != 0) {
        printf("  Expression: undefinedFunction()\n");
        printf("  Error message: %s\n", result);
        printf("  ✓ PASS: Error correctly caught\n");
    } else {
        printf("  ✗ FAIL: Should have failed but succeeded\n");
    }
    
    V8DestroyContext(isolate, context);
    V8DestroyIsolate(isolate);
}

int main(void)
{
    printf("\n");
    printf("================================================\n");
    printf("       V8 Real Engine Integration Test\n");
    printf("================================================\n");
    
    // Open v8.library
    V8Base = OpenLibrary("v8.library", 0);
    if (!V8Base) {
        printf("ERROR: Could not open v8.library\n");
        printf("Make sure v8.library is built and installed.\n");
        return 1;
    }
    printf("\n✓ v8.library opened successfully\n");
    
    // Initialize V8
    APTR platform = V8Initialize();
    if (!platform) {
        printf("ERROR: V8Initialize() failed\n");
        CloseLibrary(V8Base);
        return 1;
    }
    printf("✓ V8 platform initialized\n");
    
    // Run tests
    test_basic_arithmetic();
    test_string_concatenation();
    test_function_definition();
    test_array_operations();
    test_error_handling();
    
    // Cleanup
    V8Cleanup();
    printf("\n✓ V8 cleanup complete\n");
    
    CloseLibrary(V8Base);
    printf("\n================================================\n");
    printf("           Integration Test Complete\n");
    printf("================================================\n\n");
    
    return 0;
}
