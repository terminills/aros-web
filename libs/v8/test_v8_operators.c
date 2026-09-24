/*
    Copyright (C) 2025, The AROS Development Team. All rights reserved.

    Desc: Test program for V8 library enhanced operator support
*/

#include <proto/v8.h>
#include <proto/exec.h>
#include <stdio.h>
#include <string.h>

/* V8_OPERATORS_TEST_PROGRAM
 * AROS_IMPL: Tests new operator functionality added to simple evaluator
 * DESIGN: Comprehensive testing of new operators
 */

typedef struct {
    const char *expr;
    const char *expected;
    const char *description;
} TestCase;

static int run_test(struct Library *V8Base, V8IsolateHandle isolate, 
                    V8ContextHandle context, const TestCase *test, int test_num)
{
    char result[256];
    LONG rc;
    
    printf("TEST %d: %s\n", test_num, test->description);
    printf("  Expression: %s\n", test->expr);
    
    rc = V8Eval(isolate, context, test->expr, result, sizeof(result));
    if (rc == V8_SUCCESS) {
        printf("  Result: %s (expected: %s)\n", result, test->expected);
        if (strcmp(result, test->expected) == 0) {
            printf("  ✓ PASS\n\n");
            return 1;
        } else {
            printf("  ✗ FAIL: Expected '%s'\n\n", test->expected);
            return 0;
        }
    } else {
        printf("  ✗ FAIL: Error code %ld\n\n", rc);
        return 0;
    }
}

int main(void)
{
    struct Library *V8Base;
    V8IsolateHandle isolate = NULL;
    V8ContextHandle context = NULL;
    APTR global = NULL;
    int passed = 0, failed = 0;
    int i;
    
    /* Test cases for new operators */
    TestCase tests[] = {
        /* Unary minus */
        {"-5", "-5", "Unary minus on number"},
        {"-10 + 3", "-7", "Unary minus in expression"},
        {"5 + -3", "2", "Unary minus as operand"},
        {"--5", "5", "Double unary minus"},
        {"-(-7)", "7", "Negation of negative"},
        
        /* Unary plus */
        {"+5", "5", "Unary plus on number"},
        {"+(-5)", "-5", "Unary plus on negative"},
        
        /* Bitwise NOT */
        {"~0", "-1", "Bitwise NOT of 0"},
        {"~-1", "0", "Bitwise NOT of -1"},
        {"~5", "-6", "Bitwise NOT of 5"},
        
        /* Strict equality */
        {"5 === 5", "true", "Strict equality (same numbers)"},
        {"5 === 6", "false", "Strict equality (different numbers)"},
        {"5 !== 6", "true", "Strict inequality (different numbers)"},
        {"5 !== 5", "false", "Strict inequality (same numbers)"},
        
        /* Loose vs strict equality with types */
        /* Note: Our simple evaluator doesn't do JavaScript type coercion, 
         * so "5" == 5 returns false (string vs number, no coercion) */
        {"\"5\" == 5", "false", "Loose equality string vs number (no coercion in simple eval)"},
        {"\"5\" === 5", "false", "Strict equality string vs number"},
        {"\"hello\" === \"hello\"", "true", "Strict equality strings"},
        {"\"hello\" !== \"world\"", "true", "Strict inequality strings"},
        
        /* Bitwise AND */
        {"5 & 3", "1", "Bitwise AND (5 & 3)"},
        {"12 & 10", "8", "Bitwise AND (12 & 10)"},
        {"255 & 15", "15", "Bitwise AND (255 & 15)"},
        
        /* Bitwise OR */
        {"5 | 3", "7", "Bitwise OR (5 | 3)"},
        {"8 | 4", "12", "Bitwise OR (8 | 4)"},
        {"1 | 2 | 4", "7", "Chained bitwise OR"},
        
        /* Bitwise XOR */
        {"5 ^ 3", "6", "Bitwise XOR (5 ^ 3)"},
        {"15 ^ 5", "10", "Bitwise XOR (15 ^ 5)"},
        {"7 ^ 7", "0", "Bitwise XOR same values"},
        
        /* Left shift */
        {"1 << 4", "16", "Left shift (1 << 4)"},
        {"5 << 2", "20", "Left shift (5 << 2)"},
        {"2 << 8", "512", "Left shift (2 << 8)"},
        
        /* Right shift */
        {"16 >> 2", "4", "Right shift (16 >> 2)"},
        {"100 >> 3", "12", "Right shift (100 >> 3)"},
        {"256 >> 4", "16", "Right shift (256 >> 4)"},
        
        /* Ternary operator */
        {"true ? 1 : 2", "1", "Ternary with true condition"},
        {"false ? 1 : 2", "2", "Ternary with false condition"},
        {"5 > 3 ? 10 : 20", "10", "Ternary with comparison condition"},
        {"2 > 5 ? 10 : 20", "20", "Ternary with false comparison"},
        {"1 ? \"yes\" : \"no\"", "yes", "Ternary with string results"},
        {"0 ? \"yes\" : \"no\"", "no", "Ternary with falsy condition"},
        
        /* Nested ternary */
        {"true ? (false ? 1 : 2) : 3", "2", "Nested ternary in true branch"},
        {"false ? 1 : (true ? 2 : 3)", "2", "Nested ternary in false branch"},
        
        /* Combined operators */
        {"(5 & 3) == 1", "true", "Bitwise AND in comparison"},
        {"(5 | 2) > 5", "true", "Bitwise OR in comparison"},
        {"5 << 1 == 10", "true", "Shift in comparison"},
        {"-5 * 2", "-10", "Unary minus with multiplication"},
        {"-(3 + 2)", "-5", "Unary minus with parentheses"},
        
        /* Operator precedence tests */
        {"5 & 3 | 4", "5", "Bitwise AND before OR: (5 & 3) | 4 = 1 | 4 = 5"},
        {"2 << 2 + 1", "8", "Shift after addition: 2 << (2+1) = 2 << 3 = 8"},
    };
    
    int num_tests = sizeof(tests) / sizeof(tests[0]);
    
    printf("V8 Library Operators Test Program\n");
    printf("==================================\n\n");
    printf("Testing: Unary minus/plus, bitwise NOT/AND/OR/XOR, shifts, strict equality, ternary\n\n");
    
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
        CloseLibrary(V8Base);
        return 1;
    }
    printf("SUCCESS: V8 platform initialized\n\n");
    
    /* Create isolate */
    printf("Creating isolate...\n");
    isolate = V8CreateIsolate();
    if (!isolate) {
        printf("FAILED: Could not create isolate\n");
        V8Cleanup();
        CloseLibrary(V8Base);
        return 1;
    }
    printf("SUCCESS: Isolate created\n\n");
    
    /* Create context */
    printf("Creating context...\n");
    context = V8CreateContext(isolate);
    if (!context) {
        printf("FAILED: Could not create context\n");
        V8DestroyIsolate(isolate);
        V8Cleanup();
        CloseLibrary(V8Base);
        return 1;
    }
    printf("SUCCESS: Context created\n\n");
    
    /* Get global object */
    global = V8GetGlobalObject(isolate, context);
    if (!global) {
        printf("WARNING: Could not get global object\n\n");
    }
    
    printf("Running %d test cases...\n\n", num_tests);
    printf("----------------------------------\n\n");
    
    /* Run all tests */
    for (i = 0; i < num_tests; i++) {
        if (run_test(V8Base, isolate, context, &tests[i], i + 1)) {
            passed++;
        } else {
            failed++;
        }
    }
    
    /* Cleanup */
    printf("----------------------------------\n\n");
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
    printf("Results: %d/%d tests passed\n", passed, num_tests);
    
    if (failed == 0) {
        printf("✓ All tests PASSED\n");
        return 0;
    } else {
        printf("✗ %d tests FAILED\n", failed);
        return 1;
    }
}
