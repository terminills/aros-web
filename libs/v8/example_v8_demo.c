/*
    Copyright (C) 2025, The AROS Development Team. All rights reserved.

    Desc: Practical example program demonstrating V8 library capabilities
*/

#include <proto/v8.h>
#include <proto/exec.h>
#include <stdio.h>
#include <string.h>

/* V8_EXAMPLE_PROGRAM
 * AROS_IMPL: Shows real-world usage patterns
 * DESIGN: Simple calculator and comparison demo
 */

/* Simple calculator function */
static void calculator_demo(V8IsolateHandle isolate, V8ContextHandle context)
{
    char result[256];
    LONG rc;
    
    printf("\n=== Simple Calculator Demo ===\n");
    
    /* Basic arithmetic */
    printf("\nArithmetic Operations:\n");
    
    const char *expressions[] = {
        "5 + 3",
        "10 - 4",
        "6 * 7",
        "100 / 4",
        "15 + 3",
        NULL
    };
    
    for (int i = 0; expressions[i]; i++)
    {
        rc = V8Eval(isolate, context, expressions[i], result, sizeof(result));
        if (rc == V8_SUCCESS)
            printf("  %s = %s\n", expressions[i], result);
        else
            printf("  %s = ERROR (code %ld)\n", expressions[i], rc);
    }
}

/* Temperature comparison demo */
static void temperature_demo(V8IsolateHandle isolate, V8ContextHandle context, APTR global)
{
    char result[256];
    LONG rc;
    
    printf("\n=== Temperature Comparison Demo ===\n");
    
    /* Set temperature values */
    V8SetProperty(isolate, context, global, "currentTemp", "25");
    V8SetProperty(isolate, context, global, "targetTemp", "22");
    V8SetProperty(isolate, context, global, "maxTemp", "30");
    
    printf("\nTemperature Settings:\n");
    printf("  Current: 25°C\n");
    printf("  Target:  22°C\n");
    printf("  Maximum: 30°C\n");
    
    /* Check conditions */
    printf("\nTemperature Checks:\n");
    
    rc = V8Eval(isolate, context, "currentTemp > targetTemp", result, sizeof(result));
    if (rc == V8_SUCCESS)
        printf("  Current > Target: %s\n", result);
    
    rc = V8Eval(isolate, context, "currentTemp < maxTemp", result, sizeof(result));
    if (rc == V8_SUCCESS)
        printf("  Current < Maximum: %s\n", result);
    
    rc = V8Eval(isolate, context, "currentTemp == targetTemp", result, sizeof(result));
    if (rc == V8_SUCCESS)
        printf("  At target temperature: %s\n", result);
}

/* Boolean logic demo */
static void boolean_logic_demo(V8IsolateHandle isolate, V8ContextHandle context, APTR global)
{
    char result[256];
    LONG rc;
    
    printf("\n=== Boolean Logic Demo ===\n");
    
    /* Set some boolean flags */
    V8SetProperty(isolate, context, global, "isPoweredOn", "true");
    V8SetProperty(isolate, context, global, "isConnected", "false");
    V8SetProperty(isolate, context, global, "hasError", "false");
    
    printf("\nSystem Status:\n");
    printf("  Power:      ON\n");
    printf("  Connected:  NO\n");
    printf("  Error:      NO\n");
    
    /* Check system state */
    printf("\nSystem Checks:\n");
    
    rc = V8Eval(isolate, context, "isPoweredOn && isConnected", result, sizeof(result));
    if (rc == V8_SUCCESS)
        printf("  System ready: %s\n", result);
    
    rc = V8Eval(isolate, context, "isPoweredOn && !hasError", result, sizeof(result));
    if (rc == V8_SUCCESS)
        printf("  System operational: %s\n", result);
    
    rc = V8Eval(isolate, context, "!isConnected || hasError", result, sizeof(result));
    if (rc == V8_SUCCESS)
        printf("  Need attention: %s\n", result);
}

/* Score comparison demo */
static void score_demo(V8IsolateHandle isolate, V8ContextHandle context, APTR global)
{
    char result[256];
    LONG rc;
    
    printf("\n=== Score Comparison Demo ===\n");
    
    /* Set scores */
    V8SetProperty(isolate, context, global, "score", "85");
    V8SetProperty(isolate, context, global, "passingScore", "60");
    V8SetProperty(isolate, context, global, "perfectScore", "100");
    
    printf("\nScores:\n");
    printf("  Your score:    85\n");
    printf("  Passing score: 60\n");
    printf("  Perfect score: 100\n");
    
    /* Evaluate performance */
    printf("\nEvaluation:\n");
    
    rc = V8Eval(isolate, context, "score >= passingScore", result, sizeof(result));
    if (rc == V8_SUCCESS)
        printf("  Passed: %s\n", result);
    
    rc = V8Eval(isolate, context, "score == perfectScore", result, sizeof(result));
    if (rc == V8_SUCCESS)
        printf("  Perfect score: %s\n", result);
    
    rc = V8Eval(isolate, context, "score > passingScore", result, sizeof(result));
    if (rc == V8_SUCCESS)
        printf("  Above passing: %s\n", result);
}

/* String operations demo */
static void string_demo(V8IsolateHandle isolate, V8ContextHandle context, APTR global)
{
    char result[256];
    LONG rc;
    
    printf("\n=== String Operations Demo ===\n");
    
    /* Set string values */
    V8SetProperty(isolate, context, global, "firstName", "John");
    V8SetProperty(isolate, context, global, "lastName", "Doe");
    
    printf("\nString Concatenation:\n");
    
    rc = V8Eval(isolate, context, "\"Hello\" + \"World\"", result, sizeof(result));
    if (rc == V8_SUCCESS)
        printf("  \"Hello\" + \"World\" = %s\n", result);
    
    rc = V8Eval(isolate, context, "firstName + lastName", result, sizeof(result));
    if (rc == V8_SUCCESS)
        printf("  firstName + lastName = %s\n", result);
    
    printf("\nString Comparison:\n");
    
    rc = V8Eval(isolate, context, "firstName == \"John\"", result, sizeof(result));
    if (rc == V8_SUCCESS)
        printf("  firstName == \"John\": %s\n", result);
    
    rc = V8Eval(isolate, context, "lastName != \"Smith\"", result, sizeof(result));
    if (rc == V8_SUCCESS)
        printf("  lastName != \"Smith\": %s\n", result);
}

int main(void)
{
    struct Library *V8Base;
    V8IsolateHandle isolate = NULL;
    V8ContextHandle context = NULL;
    APTR global = NULL;
    
    printf("V8 Library - Practical Examples\n");
    printf("================================\n");
    
    /* Initialize library */
    V8Base = OpenLibrary("v8.library", 1);
    if (!V8Base) {
        printf("ERROR: Could not open v8.library\n");
        return 1;
    }
    
    if (!V8Initialize()) {
        printf("ERROR: Could not initialize V8\n");
        CloseLibrary(V8Base);
        return 1;
    }
    
    /* Create execution context */
    isolate = V8CreateIsolate();
    if (!isolate) {
        printf("ERROR: Could not create isolate\n");
        V8Cleanup();
        CloseLibrary(V8Base);
        return 1;
    }
    
    context = V8CreateContext(isolate);
    if (!context) {
        printf("ERROR: Could not create context\n");
        V8DestroyIsolate(isolate);
        V8Cleanup();
        CloseLibrary(V8Base);
        return 1;
    }
    
    global = V8GetGlobalObject(isolate, context);
    if (!global) {
        printf("ERROR: Could not get global object\n");
        V8DestroyContext(isolate, context);
        V8DestroyIsolate(isolate);
        V8Cleanup();
        CloseLibrary(V8Base);
        return 1;
    }
    
    printf("\nV8 library initialized successfully!\n");
    printf("Library version: %d.%d\n", V8Base->lib_Version, V8Base->lib_Revision);
    
    /* Run demonstrations */
    calculator_demo(isolate, context);
    temperature_demo(isolate, context, global);
    boolean_logic_demo(isolate, context, global);
    score_demo(isolate, context, global);
    string_demo(isolate, context, global);
    
    /* Summary */
    printf("\n================================\n");
    printf("All demonstrations completed!\n");
    printf("\nSupported Features:\n");
    printf("  ✓ Arithmetic operations (+, -, *, /)\n");
    printf("  ✓ String concatenation\n");
    printf("  ✓ Comparison operators (==, !=, <, >, <=, >=)\n");
    printf("  ✓ Logical operators (&&, ||, !)\n");
    printf("  ✓ Boolean values (true, false)\n");
    printf("  ✓ Property storage and retrieval\n");
    printf("  ✓ Property references in expressions\n");
    printf("\nNote: This is a simple evaluator demonstration.\n");
    printf("Full V8 engine integration will enable complete JavaScript support.\n");
    
    /* Cleanup */
    V8DestroyContext(isolate, context);
    V8DestroyIsolate(isolate);
    V8Cleanup();
    CloseLibrary(V8Base);
    
    return 0;
}
