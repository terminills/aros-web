/*
    Test for V8 Array Methods
    
    This test verifies that the simple JavaScript evaluator supports
    array methods like join(), reverse(), and slice().
*/

#include <proto/v8.h>
#include <proto/exec.h>
#include <stdio.h>
#include <string.h>

struct Library *V8Base = NULL;

static int test_count = 0;
static int pass_count = 0;
static int fail_count = 0;

static void test_expression(APTR context, const char *expr, const char *expected, const char *test_name)
{
    char result[256];
    LONG status;
    
    test_count++;
    printf("Test %d: %s\n", test_count, test_name);
    printf("  Expression: %s\n", expr);
    
    status = V8Eval(context, expr, result, sizeof(result));
    
    if (status == 0)
    {
        printf("  Result: %s\n", result);
        printf("  Expected: %s\n", expected);
        
        if (strcmp(result, expected) == 0)
        {
            printf("  ✓ PASS\n\n");
            pass_count++;
        }
        else
        {
            printf("  ✗ FAIL: Expected '%s', got '%s'\n\n", expected, result);
            fail_count++;
        }
    }
    else
    {
        printf("  ✗ FAIL: Evaluation failed: %s\n\n", result);
        fail_count++;
    }
}

static void test_array_join(APTR context)
{
    printf("=== Testing Array.join() ===\n\n");
    
    test_expression(context, "[1,2,3].join()", "1,2,3", "join with default separator");
    test_expression(context, "[1,2,3].join('-')", "1-2-3", "join with dash separator");
    test_expression(context, "[1,2,3].join(' ')", "1 2 3", "join with space separator");
    test_expression(context, "['a','b','c'].join()", "a,b,c", "join strings with default");
    test_expression(context, "['a','b','c'].join('|')", "a|b|c", "join strings with pipe");
    test_expression(context, "[].join()", "", "join empty array");
    test_expression(context, "[1].join()", "1", "join single element");
}

static void test_array_reverse(APTR context)
{
    printf("\n=== Testing Array.reverse() ===\n\n");
    
    test_expression(context, "[1,2,3].reverse()", "[3,2,1]", "reverse numbers");
    test_expression(context, "['a','b','c'].reverse()", "['c','b','a']", "reverse strings");
    test_expression(context, "[1].reverse()", "[1]", "reverse single element");
    test_expression(context, "[].reverse()", "[]", "reverse empty array");
    test_expression(context, "[1,2,3,4,5].reverse()", "[5,4,3,2,1]", "reverse longer array");
}

static void test_array_slice(APTR context)
{
    printf("\n=== Testing Array.slice() ===\n\n");
    
    test_expression(context, "[1,2,3,4,5].slice(1,3)", "[2,3]", "slice with start and end");
    test_expression(context, "[1,2,3,4,5].slice(2)", "[3,4,5]", "slice with start only");
    test_expression(context, "[1,2,3,4,5].slice(0,2)", "[1,2]", "slice from beginning");
    test_expression(context, "[1,2,3,4,5].slice(-2)", "[4,5]", "slice with negative start");
    test_expression(context, "[1,2,3,4,5].slice(1,-1)", "[2,3,4]", "slice with negative end");
    test_expression(context, "['a','b','c'].slice(1,2)", "['b']", "slice strings");
    test_expression(context, "[].slice(0,1)", "[]", "slice empty array");
}

static void test_array_chaining(APTR context)
{
    printf("\n=== Testing Array Method Chaining ===\n\n");
    
    test_expression(context, "[1,2,3].reverse().join('-')", "3-2-1", "reverse then join");
    test_expression(context, "[1,2,3,4,5].slice(1,4).reverse()", "[4,3,2]", "slice then reverse");
    test_expression(context, "[1,2,3].slice(0,2).join(':')", "1:2", "slice then join");
}

static void test_array_with_variables(APTR context)
{
    printf("\n=== Testing Array Methods with Variables ===\n\n");
    
    /* Set a variable to an array */
    char result[256];
    V8Eval(context, "arr = [5,4,3,2,1]", result, sizeof(result));
    
    test_expression(context, "arr.length", "5", "array variable length");
    test_expression(context, "arr[0]", "5", "array variable indexing");
    test_expression(context, "arr.join('-')", "5-4-3-2-1", "array variable join");
    test_expression(context, "arr.reverse()", "[1,2,3,4,5]", "array variable reverse");
    test_expression(context, "arr.slice(1,3)", "[2,3]", "array variable slice");
}

int main(void)
{
    APTR isolate = NULL;
    APTR context = NULL;
    
    printf("\n");
    printf("========================================\n");
    printf("  V8 Array Methods Test\n");
    printf("========================================\n");
    printf("\n");
    
    V8Base = OpenLibrary("v8.library", 0);
    if (!V8Base)
    {
        printf("ERROR: Failed to open v8.library\n");
        return 1;
    }
    
    isolate = V8CreateIsolate();
    if (!isolate)
    {
        printf("ERROR: Failed to create isolate\n");
        CloseLibrary(V8Base);
        return 1;
    }
    
    context = V8CreateContext(isolate);
    if (!context)
    {
        printf("ERROR: Failed to create context\n");
        V8DestroyIsolate(isolate);
        CloseLibrary(V8Base);
        return 1;
    }
    
    test_array_join(context);
    test_array_reverse(context);
    test_array_slice(context);
    test_array_chaining(context);
    test_array_with_variables(context);
    
    /* Cleanup */
    V8DestroyContext(context);
    V8DestroyIsolate(isolate);
    CloseLibrary(V8Base);
    
    printf("\n");
    printf("========================================\n");
    printf("  Test Results\n");
    printf("========================================\n");
    printf("Total Tests:  %d\n", test_count);
    printf("Passed:       %d\n", pass_count);
    printf("Failed:       %d\n", fail_count);
    printf("Success Rate: %.1f%%\n", test_count > 0 ? (100.0 * pass_count / test_count) : 0.0);
    printf("========================================\n");
    
    return (fail_count == 0) ? 0 : 1;
}
