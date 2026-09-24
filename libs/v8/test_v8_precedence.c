/*
    Copyright (C) 2025, The AROS Development Team. All rights reserved.

    Desc: V8 Library Test Program - Operator Precedence and Parentheses
*/

#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include <string.h>

/* Include V8 library structures (internal for testing) */
#include "v8_intern.h"

/* Test counter */
static int test_count = 0;
static int tests_passed = 0;
static int tests_failed = 0;

/* Test helper function */
static void test_expression(struct V8Context *context, CONST_STRPTR expr, 
                           CONST_STRPTR expected, CONST_STRPTR description)
{
    char result[256];
    LONG rc;
    
    test_count++;
    printf("\nTest %d: %s\n", test_count, description);
    printf("  Expression: %s\n", expr);
    printf("  Expected:   %s\n", expected);
    
    rc = V8_EvaluateSimpleExpression(expr, context, result, sizeof(result));
    
    if (rc == V8_SUCCESS)
    {
        printf("  Result:     %s\n", result);
        
        if (strcmp(result, expected) == 0)
        {
            printf("  Status:     PASS ✓\n");
            tests_passed++;
        }
        else
        {
            printf("  Status:     FAIL ✗ (got '%s', expected '%s')\n", result, expected);
            tests_failed++;
        }
    }
    else
    {
        printf("  Result:     ERROR (code %ld)\n", rc);
        printf("  Status:     FAIL ✗\n");
        tests_failed++;
    }
}

int main(void)
{
    struct V8Context context;
    
    printf("V8 Library Enhanced Evaluator Test\n");
    printf("===================================\n");
    printf("\nTesting operator precedence and parentheses support...\n");
    
    /* Initialize context for property storage */
    memset(&context, 0, sizeof(context));
    NEWLIST((struct List *)&context.vc_Properties);
    
    /* Test 1: Basic operator precedence (multiplication before addition) */
    test_expression(&context, "2 + 3 * 4", "14", 
                   "Multiplication has higher precedence than addition");
    
    /* Test 2: Operator precedence (division before subtraction) */
    test_expression(&context, "10 - 6 / 2", "7",
                   "Division has higher precedence than subtraction");
    
    /* Test 3: Parentheses override precedence */
    test_expression(&context, "(2 + 3) * 4", "20",
                   "Parentheses override precedence");
    
    /* Test 4: Nested parentheses */
    test_expression(&context, "((2 + 3) * (4 + 1))", "25",
                   "Nested parentheses");
    
    /* Test 5: Complex expression with precedence */
    test_expression(&context, "2 + 3 * 4 - 5", "9",
                   "Multiple operators with precedence");
    
    /* Test 6: Modulo operator */
    test_expression(&context, "10 % 3", "1",
                   "Modulo operator");
    
    /* Test 7: Modulo with other operations */
    test_expression(&context, "10 % 3 + 2", "3",
                   "Modulo with addition");
    
    /* Test 8: Parentheses with modulo */
    test_expression(&context, "10 % (3 + 2)", "0",
                   "Modulo with parentheses");
    
    /* Test 9: Division and multiplication precedence */
    test_expression(&context, "20 / 4 * 2", "10",
                   "Division and multiplication (left to right)");
    
    /* Test 10: Addition and subtraction precedence */
    test_expression(&context, "10 - 3 + 2", "9",
                   "Addition and subtraction (left to right)");
    
    /* Test 11: Comparison with arithmetic */
    test_expression(&context, "2 + 2 == 4", "true",
                   "Comparison with arithmetic");
    
    /* Test 12: Comparison in parentheses */
    test_expression(&context, "(5 > 3) && (2 < 4)", "true",
                   "Comparison in parentheses with logical AND");
    
    /* Test 13: Logical operators with precedence */
    test_expression(&context, "true || false && false", "true",
                   "OR has lower precedence than AND");
    
    /* Test 14: Parentheses change logical precedence */
    test_expression(&context, "(true || false) && false", "false",
                   "Parentheses with logical operators");
    
    /* Test 15: String concatenation with precedence */
    test_expression(&context, "\"Hello\" + \" \" + \"World\"", "Hello World",
                   "String concatenation is left-to-right");
    
    /* Test 16: Properties with complex expressions */
    V8_SetPropertyValue(&context, "x", "5");
    V8_SetPropertyValue(&context, "y", "3");
    test_expression(&context, "x * 2 + y", "13",
                   "Properties in complex expressions");
    
    /* Test 17: Properties with parentheses */
    test_expression(&context, "(x + y) * 2", "16",
                   "Properties with parentheses");
    
    /* Test 18: Property in nested expression */
    test_expression(&context, "((x + 1) * (y + 2))", "30",
                   "Properties in nested expressions");
    
    /* Test 19: Unary NOT with parentheses */
    test_expression(&context, "!(5 > 10)", "true",
                   "Unary NOT with parentheses");
    
    /* Test 20: Complex nested expression */
    test_expression(&context, "2 * (3 + 4) - (5 - 2) * 2", "8",
                   "Complex nested expression");
    
    /* Clean up properties */
    V8_FreeProperties(&context);
    
    /* Print summary */
    printf("\n");
    printf("===================================\n");
    printf("Test Results:\n");
    printf("  Total:  %d\n", test_count);
    printf("  Passed: %d\n", tests_passed);
    printf("  Failed: %d\n", tests_failed);
    printf("===================================\n");
    
    if (tests_failed == 0)
    {
        printf("\nAll tests PASSED! ✓\n");
        return 0;
    }
    else
    {
        printf("\n%d test(s) FAILED ✗\n", tests_failed);
        return 1;
    }
}
