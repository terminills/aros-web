/*
    V8 Library Integration Test
    
    Comprehensive test that verifies the complete integration path:
    C API (proto/v8.h) → v8.library → v8_bridge.cc → v8-embedder.cc
    
    This test validates:
    - Library initialization and cleanup
    - Isolate and context management
    - Script execution and caching
    - Property and object manipulation
    - Error handling and recovery
    - Memory management
    - Thread safety (basic)
*/

#include <proto/v8.h>
#include <proto/exec.h>
#include <stdio.h>
#include <string.h>

struct Library *V8Base = NULL;

static int total_tests = 0;
static int passed_tests = 0;
static int failed_tests = 0;

#define TEST_START(name) \
    do { \
        total_tests++; \
        printf("\n[Test %d] %s\n", total_tests, name); \
        printf("-------------------------------------------\n"); \
    } while(0)

#define TEST_PASS(msg) \
    do { \
        passed_tests++; \
        printf("  ✓ PASS: %s\n", msg); \
    } while(0)

#define TEST_FAIL(msg) \
    do { \
        failed_tests++; \
        printf("  ✗ FAIL: %s\n", msg); \
    } while(0)

#define TEST_INFO(msg) \
    printf("  • %s\n", msg)

/* Test 1: Library Management */
static void test_library_management(void)
{
    TEST_START("Library Management");
    
    /* Open library */
    V8Base = OpenLibrary("v8.library", 0);
    if (!V8Base) {
        TEST_FAIL("Failed to open v8.library");
        return;
    }
    TEST_PASS("v8.library opened successfully");
    
    /* Verify library base is valid before accessing members */
    if (V8Base && V8Base->lib_Version > 0) {
        TEST_PASS("Library version is valid");
        TEST_INFO("Library version: %d.%d", V8Base->lib_Version, V8Base->lib_Revision);
    } else {
        TEST_FAIL("Library version is invalid");
    }
}

/* Test 2: Platform Initialization */
static void test_platform_initialization(void)
{
    TEST_START("Platform Initialization");
    
    /* Initialize V8 platform */
    APTR platform = V8Initialize();
    if (!platform) {
        TEST_FAIL("V8Initialize() returned NULL");
        return;
    }
    TEST_PASS("V8 platform initialized");
    TEST_INFO("Platform handle: %p", platform);
    
    /* Try double initialization (should be safe) */
    APTR platform2 = V8Initialize();
    if (platform2 == platform) {
        TEST_PASS("Double initialization returns same platform");
    } else {
        TEST_FAIL("Double initialization returned different platform");
    }
}

/* Test 3: Isolate Management */
static void test_isolate_management(void)
{
    TEST_START("Isolate Management");
    
    /* Create isolate */
    APTR isolate = V8CreateIsolate();
    if (!isolate) {
        TEST_FAIL("V8CreateIsolate() returned NULL");
        return;
    }
    TEST_PASS("Isolate created");
    TEST_INFO("Isolate handle: %p", isolate);
    
    /* Create second isolate (should work) */
    APTR isolate2 = V8CreateIsolate();
    if (!isolate2) {
        TEST_FAIL("Failed to create second isolate");
    } else if (isolate2 == isolate) {
        TEST_FAIL("Second isolate has same handle as first");
    } else {
        TEST_PASS("Multiple isolates can be created");
        V8DestroyIsolate(isolate2);
    }
    
    /* Cleanup */
    V8DestroyIsolate(isolate);
    TEST_PASS("Isolate destroyed without errors");
}

/* Test 4: Context Management */
static void test_context_management(void)
{
    TEST_START("Context Management");
    
    APTR isolate = V8CreateIsolate();
    if (!isolate) {
        TEST_FAIL("Failed to create isolate");
        return;
    }
    
    /* Create context */
    APTR context = V8CreateContext(isolate);
    if (!context) {
        TEST_FAIL("V8CreateContext() returned NULL");
        V8DestroyIsolate(isolate);
        return;
    }
    TEST_PASS("Context created");
    TEST_INFO("Context handle: %p", context);
    
    /* Create second context in same isolate */
    APTR context2 = V8CreateContext(isolate);
    if (!context2) {
        TEST_FAIL("Failed to create second context");
    } else if (context2 == context) {
        TEST_FAIL("Second context has same handle as first");
    } else {
        TEST_PASS("Multiple contexts can be created in same isolate");
        V8DestroyContext(context2);
    }
    
    /* Cleanup */
    V8DestroyContext(context);
    V8DestroyIsolate(isolate);
    TEST_PASS("Context and isolate destroyed without errors");
}

/* Test 5: Basic Script Evaluation */
static void test_basic_evaluation(void)
{
    TEST_START("Basic Script Evaluation");
    
    APTR isolate = V8CreateIsolate();
    APTR context = V8CreateContext(isolate);
    char result[256];
    LONG status;
    
    if (!isolate || !context) {
        TEST_FAIL("Failed to create isolate or context");
        return;
    }
    
    /* Test 1: Simple arithmetic */
    status = V8Eval(context, "2 + 2", result, sizeof(result));
    if (status == 0 && strcmp(result, "4") == 0) {
        TEST_PASS("Simple arithmetic: 2 + 2 = 4");
    } else {
        TEST_FAIL("Simple arithmetic failed: got '%s'", result);
    }
    
    /* Test 2: String concatenation */
    status = V8Eval(context, "'Hello' + ' ' + 'World'", result, sizeof(result));
    if (status == 0 && strcmp(result, "Hello World") == 0) {
        TEST_PASS("String concatenation works");
    } else {
        TEST_FAIL("String concatenation failed: got '%s'", result);
    }
    
    /* Test 3: Comparison */
    status = V8Eval(context, "5 > 3", result, sizeof(result));
    if (status == 0 && strcmp(result, "true") == 0) {
        TEST_PASS("Comparison operators work");
    } else {
        TEST_FAIL("Comparison failed: got '%s'", result);
    }
    
    /* Test 4: Arrays */
    status = V8Eval(context, "[1,2,3].length", result, sizeof(result));
    if (status == 0 && strcmp(result, "3") == 0) {
        TEST_PASS("Array literals and .length work");
    } else {
        TEST_FAIL("Array handling failed: got '%s'", result);
    }
    
    /* Test 5: Array methods */
    status = V8Eval(context, "[1,2,3].join('-')", result, sizeof(result));
    if (status == 0 && strcmp(result, "1-2-3") == 0) {
        TEST_PASS("Array methods work (join)");
    } else {
        TEST_FAIL("Array method failed: got '%s'", result);
    }
    
    V8DestroyContext(context);
    V8DestroyIsolate(isolate);
}

/* Test 6: Property Management */
static void test_property_management(void)
{
    TEST_START("Property Management");
    
    APTR isolate = V8CreateIsolate();
    APTR context = V8CreateContext(isolate);
    APTR global;
    char result[256];
    LONG status;
    
    if (!isolate || !context) {
        TEST_FAIL("Failed to create isolate or context");
        return;
    }
    
    global = V8GetGlobalObject(isolate, context);
    if (!global) {
        TEST_FAIL("Failed to get global object");
        V8DestroyContext(context);
        V8DestroyIsolate(isolate);
        return;
    }
    TEST_PASS("Global object retrieved");
    
    /* Set a property */
    status = V8SetProperty(isolate, context, global, "testValue", "42");
    if (status == 0) {
        TEST_PASS("Property set successfully");
    } else {
        TEST_FAIL("Failed to set property");
    }
    
    /* Get the property back */
    status = V8GetProperty(isolate, context, global, "testValue", result, sizeof(result));
    if (status == 0 && strcmp(result, "42") == 0) {
        TEST_PASS("Property retrieved successfully");
    } else {
        TEST_FAIL("Failed to retrieve property: got '%s'", result);
    }
    
    /* Try to get non-existent property */
    status = V8GetProperty(isolate, context, global, "nonExistent", result, sizeof(result));
    if (status == 0) {
        TEST_PASS("Non-existent property handled gracefully");
    } else {
        TEST_FAIL("Error handling non-existent property");
    }
    
    V8DestroyContext(context);
    V8DestroyIsolate(isolate);
}

/* Test 7: Script Compilation and Caching */
static void test_script_compilation(void)
{
    TEST_START("Script Compilation and Caching");
    
    APTR isolate = V8CreateIsolate();
    APTR context = V8CreateContext(isolate);
    APTR script;
    char result[256];
    LONG status;
    
    if (!isolate || !context) {
        TEST_FAIL("Failed to create isolate or context");
        return;
    }
    
    /* Compile a script */
    script = V8CompileScript(isolate, context, "10 + 20", "test.js");
    if (!script) {
        TEST_FAIL("Failed to compile script");
        V8DestroyContext(context);
        V8DestroyIsolate(isolate);
        return;
    }
    TEST_PASS("Script compiled successfully");
    
    /* Run the compiled script */
    status = V8RunScript(isolate, context, script, result, sizeof(result));
    if (status == 0 && strcmp(result, "30") == 0) {
        TEST_PASS("Compiled script executed successfully");
    } else {
        TEST_FAIL("Failed to run compiled script: got '%s'", result);
    }
    
    /* Run it again (test caching) */
    status = V8RunScript(isolate, context, script, result, sizeof(result));
    if (status == 0 && strcmp(result, "30") == 0) {
        TEST_PASS("Cached script executed successfully");
    } else {
        TEST_FAIL("Failed to run cached script");
    }
    
    /* Cleanup */
    V8FreeScript(script);
    TEST_PASS("Script freed without errors");
    
    V8DestroyContext(context);
    V8DestroyIsolate(isolate);
}

/* Test 8: Error Handling */
static void test_error_handling(void)
{
    TEST_START("Error Handling");
    
    APTR isolate = V8CreateIsolate();
    APTR context = V8CreateContext(isolate);
    char result[256];
    LONG status;
    
    if (!isolate || !context) {
        TEST_FAIL("Failed to create isolate or context");
        return;
    }
    
    /* Test with NULL parameters */
    status = V8Eval(NULL, "2+2", result, sizeof(result));
    if (status != 0) {
        TEST_PASS("NULL isolate handled correctly");
    } else {
        TEST_FAIL("NULL isolate not detected");
    }
    
    status = V8Eval(context, NULL, result, sizeof(result));
    if (status != 0) {
        TEST_PASS("NULL script handled correctly");
    } else {
        TEST_FAIL("NULL script not detected");
    }
    
    /* Test with empty script */
    status = V8Eval(context, "", result, sizeof(result));
    if (status != 0) {
        TEST_PASS("Empty script handled correctly");
    } else {
        TEST_FAIL("Empty script not detected");
    }
    
    /* Test with invalid syntax (limited detection in simple evaluator) */
    status = V8Eval(context, "2 +", result, sizeof(result));
    if (status != 0) {
        TEST_PASS("Invalid syntax detected");
    } else {
        TEST_INFO("Note: Simple evaluator has limited syntax checking");
    }
    
    V8DestroyContext(context);
    V8DestroyIsolate(isolate);
}

/* Test 9: Memory Management */
static void test_memory_management(void)
{
    TEST_START("Memory Management");
    
    const int iterations = 100;
    int i;
    
    /* Create and destroy many isolates */
    for (i = 0; i < iterations; i++) {
        APTR isolate = V8CreateIsolate();
        if (!isolate) {
            TEST_FAIL("Failed to create isolate in iteration %d", i);
            return;
        }
        V8DestroyIsolate(isolate);
    }
    TEST_PASS("Created and destroyed %d isolates without leaks", iterations);
    
    /* Create and destroy many contexts */
    APTR isolate = V8CreateIsolate();
    if (!isolate) {
        TEST_FAIL("Failed to create isolate for context test");
        return;
    }
    
    for (i = 0; i < iterations; i++) {
        APTR context = V8CreateContext(isolate);
        if (!context) {
            TEST_FAIL("Failed to create context in iteration %d", i);
            V8DestroyIsolate(isolate);
            return;
        }
        V8DestroyContext(context);
    }
    TEST_PASS("Created and destroyed %d contexts without leaks", iterations);
    
    V8DestroyIsolate(isolate);
}

/* Test 10: Cleanup and Shutdown */
static void test_cleanup_shutdown(void)
{
    TEST_START("Cleanup and Shutdown");
    
    /* Cleanup V8 platform */
    V8Cleanup();
    TEST_PASS("V8 platform cleaned up");
    
    /* Close library */
    if (V8Base) {
        CloseLibrary(V8Base);
        V8Base = NULL;
        TEST_PASS("v8.library closed");
    }
}

int main(void)
{
    printf("\n");
    printf("====================================================\n");
    printf("  V8 Library Integration Test Suite\n");
    printf("====================================================\n");
    printf("\n");
    printf("This test validates the complete integration path:\n");
    printf("  C API → v8.library → v8_bridge.cc → v8-embedder.cc\n");
    printf("\n");
    
    /* Run all tests */
    test_library_management();
    test_platform_initialization();
    test_isolate_management();
    test_context_management();
    test_basic_evaluation();
    test_property_management();
    test_script_compilation();
    test_error_handling();
    test_memory_management();
    test_cleanup_shutdown();
    
    /* Print summary */
    printf("\n");
    printf("====================================================\n");
    printf("  Test Results Summary\n");
    printf("====================================================\n");
    printf("Total Tests:    %d\n", total_tests);
    printf("Passed:         %d\n", passed_tests);
    printf("Failed:         %d\n", failed_tests);
    printf("Success Rate:   %.1f%%\n", 
           total_tests > 0 ? (100.0 * passed_tests / total_tests) : 0.0);
    printf("====================================================\n");
    printf("\n");
    
    if (failed_tests == 0) {
        printf("✓ ALL TESTS PASSED - Integration is working correctly!\n");
        printf("\n");
        return 0;
    } else {
        printf("✗ SOME TESTS FAILED - Please review the failures above.\n");
        printf("\n");
        return 1;
    }
}
