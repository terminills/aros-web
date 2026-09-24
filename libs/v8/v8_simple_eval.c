/*
    Copyright (C) 2025, The AROS Development Team. All rights reserved.

    Desc: Simple JavaScript expression evaluator (temporary until V8 is ported)
*/

#include <proto/exec.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "v8_intern.h"

/* V8_SIMPLE_EVALUATOR
 * AROS_IMPL: Temporary solution until V8 engine is ported
 * DESIGN: Handles arithmetic, strings, comparisons, logical, bitwise, ternary operators, and arrays
 * FEATURES: +, -, *, /, %, ==, !=, ===, !==, <, >, <=, >=, &&, ||, !, &, |, ^, ~, <<, >>, >>>, ?:, ++, --, comma, compound assignments, array literals, array indexing, array methods
 * ENHANCEMENTS: Operator precedence, parentheses, unary minus, strict equality, bitwise, ternary, unsigned right shift, increment/decrement, comma operator, compound assignments, array literals, array indexing, array.length, array.join(), array.reverse(), array.slice()
 * LIMITATIONS: No functions or control flow yet, limited array method support (join, reverse, slice only), max 32 array elements of 256 chars each
 * TODO: Add function definitions, control flow, more array methods (map, filter, reduce, forEach), dynamic array allocation
 */

/*****************************************************************************
 * Simple tokenizer for basic expressions
 *****************************************************************************/

typedef enum {
    TOKEN_NUMBER,
    TOKEN_STRING,
    TOKEN_IDENTIFIER,
    TOKEN_OPERATOR,
    TOKEN_BOOLEAN,
    TOKEN_NULL,
    TOKEN_UNDEFINED,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_LBRACKET,   /* [ for arrays */
    TOKEN_RBRACKET,   /* ] for arrays */
    TOKEN_DOT,        /* . for property access */
    TOKEN_QUESTION,   /* Ternary ? */
    TOKEN_COLON,      /* Ternary : */
    TOKEN_COMMA,      /* Comma operator */
    TOKEN_INCREMENT,  /* ++ */
    TOKEN_DECREMENT,  /* -- */
    TOKEN_END,
    TOKEN_ERROR
} TokenType;

typedef struct {
    TokenType type;
    char value[256];
} Token;

/* Skip whitespace */
static const char *skip_whitespace(const char *str)
{
    while (*str && isspace(*str))
        str++;
    return str;
}

/* Parse a number */
static const char *parse_number(const char *str, Token *token)
{
    int i = 0;
    token->type = TOKEN_NUMBER;
    
    while (*str && (isdigit(*str) || *str == '.'))
    {
        if (i < 255)
            token->value[i++] = *str;
        str++;
    }
    token->value[i] = '\0';
    return str;
}

/* Parse a string literal */
static const char *parse_string(const char *str, Token *token)
{
    int i = 0;
    char quote = *str++;  /* Skip opening quote */
    token->type = TOKEN_STRING;
    
    while (*str && *str != quote)
    {
        if (i < 255)
            token->value[i++] = *str;
        str++;
    }
    token->value[i] = '\0';
    
    if (*str == quote)
        str++;  /* Skip closing quote */
    
    return str;
}

/* Parse an identifier or keyword */
static const char *parse_identifier(const char *str, Token *token)
{
    int i = 0;
    token->type = TOKEN_IDENTIFIER;
    
    while (*str && (isalnum(*str) || *str == '_'))
    {
        if (i < 255)
            token->value[i++] = *str;
        str++;
    }
    token->value[i] = '\0';
    
    /* Check for keywords */
    if (strcmp(token->value, "true") == 0 || strcmp(token->value, "false") == 0)
        token->type = TOKEN_BOOLEAN;
    else if (strcmp(token->value, "null") == 0)
        token->type = TOKEN_NULL;
    else if (strcmp(token->value, "undefined") == 0)
        token->type = TOKEN_UNDEFINED;
    
    return str;
}

/* Get next token */
static const char *get_token(const char *str, Token *token)
{
    str = skip_whitespace(str);
    
    if (!*str)
    {
        token->type = TOKEN_END;
        token->value[0] = '\0';
        return str;
    }
    
    /* Numbers */
    if (isdigit(*str))
        return parse_number(str, token);
    
    /* Strings */
    if (*str == '"' || *str == '\'')
        return parse_string(str, token);
    
    /* Identifiers and keywords */
    if (isalpha(*str) || *str == '_')
        return parse_identifier(str, token);
    
    /* Parentheses */
    if (*str == '(')
    {
        token->type = TOKEN_LPAREN;
        token->value[0] = '(';
        token->value[1] = '\0';
        return str + 1;
    }
    if (*str == ')')
    {
        token->type = TOKEN_RPAREN;
        token->value[0] = ')';
        token->value[1] = '\0';
        return str + 1;
    }
    
    /* Square brackets for arrays */
    if (*str == '[')
    {
        token->type = TOKEN_LBRACKET;
        token->value[0] = '[';
        token->value[1] = '\0';
        return str + 1;
    }
    if (*str == ']')
    {
        token->type = TOKEN_RBRACKET;
        token->value[0] = ']';
        token->value[1] = '\0';
        return str + 1;
    }
    
    /* Dot for property access */
    if (*str == '.')
    {
        token->type = TOKEN_DOT;
        token->value[0] = '.';
        token->value[1] = '\0';
        return str + 1;
    }
    
    /* Ternary operator tokens */
    if (*str == '?')
    {
        token->type = TOKEN_QUESTION;
        token->value[0] = '?';
        token->value[1] = '\0';
        return str + 1;
    }
    if (*str == ':')
    {
        token->type = TOKEN_COLON;
        token->value[0] = ':';
        token->value[1] = '\0';
        return str + 1;
    }
    
    /* Comma operator */
    if (*str == ',')
    {
        token->type = TOKEN_COMMA;
        token->value[0] = ',';
        token->value[1] = '\0';
        return str + 1;
    }
    
    /* Multi-character operators - check longest matches first */
    
    /* Unsigned right shift (3 characters) */
    if (str[0] == '>' && str[1] == '>' && str[2] == '>')
    {
        token->type = TOKEN_OPERATOR;
        token->value[0] = '>';
        token->value[1] = '>';
        token->value[2] = '>';
        token->value[3] = '\0';
        return str + 3;
    }
    
    /* Compound assignment operators with shifts (3+ characters) */
    if (str[0] == '>' && str[1] == '>' && str[2] == '>' && str[3] == '=')
    {
        token->type = TOKEN_OPERATOR;
        strncpy(token->value, ">>>=", 5);
        return str + 4;
    }
    if (str[0] == '<' && str[1] == '<' && str[2] == '=')
    {
        token->type = TOKEN_OPERATOR;
        strncpy(token->value, "<<=", 4);
        return str + 3;
    }
    if (str[0] == '>' && str[1] == '>' && str[2] == '=')
    {
        token->type = TOKEN_OPERATOR;
        strncpy(token->value, ">>=", 4);
        return str + 3;
    }
    
    /* Strict equality operators (3 characters) */
    if (str[0] == '=' && str[1] == '=' && str[2] == '=')
    {
        token->type = TOKEN_OPERATOR;
        token->value[0] = '=';
        token->value[1] = '=';
        token->value[2] = '=';
        token->value[3] = '\0';
        return str + 3;
    }
    if (str[0] == '!' && str[1] == '=' && str[2] == '=')
    {
        token->type = TOKEN_OPERATOR;
        token->value[0] = '!';
        token->value[1] = '=';
        token->value[2] = '=';
        token->value[3] = '\0';
        return str + 3;
    }
    
    /* Increment/Decrement operators (2 characters) - must come before single char +/- */
    if (str[0] == '+' && str[1] == '+')
    {
        token->type = TOKEN_INCREMENT;
        token->value[0] = '+';
        token->value[1] = '+';
        token->value[2] = '\0';
        return str + 2;
    }
    if (str[0] == '-' && str[1] == '-')
    {
        token->type = TOKEN_DECREMENT;
        token->value[0] = '-';
        token->value[1] = '-';
        token->value[2] = '\0';
        return str + 2;
    }
    
    /* Compound assignment operators (2 characters) */
    if (str[0] == '+' && str[1] == '=')
    {
        token->type = TOKEN_OPERATOR;
        token->value[0] = '+';
        token->value[1] = '=';
        token->value[2] = '\0';
        return str + 2;
    }
    if (str[0] == '-' && str[1] == '=')
    {
        token->type = TOKEN_OPERATOR;
        token->value[0] = '-';
        token->value[1] = '=';
        token->value[2] = '\0';
        return str + 2;
    }
    if (str[0] == '*' && str[1] == '=')
    {
        token->type = TOKEN_OPERATOR;
        token->value[0] = '*';
        token->value[1] = '=';
        token->value[2] = '\0';
        return str + 2;
    }
    if (str[0] == '/' && str[1] == '=')
    {
        token->type = TOKEN_OPERATOR;
        token->value[0] = '/';
        token->value[1] = '=';
        token->value[2] = '\0';
        return str + 2;
    }
    if (str[0] == '%' && str[1] == '=')
    {
        token->type = TOKEN_OPERATOR;
        token->value[0] = '%';
        token->value[1] = '=';
        token->value[2] = '\0';
        return str + 2;
    }
    if (str[0] == '&' && str[1] == '=')
    {
        token->type = TOKEN_OPERATOR;
        token->value[0] = '&';
        token->value[1] = '=';
        token->value[2] = '\0';
        return str + 2;
    }
    if (str[0] == '|' && str[1] == '=')
    {
        token->type = TOKEN_OPERATOR;
        token->value[0] = '|';
        token->value[1] = '=';
        token->value[2] = '\0';
        return str + 2;
    }
    if (str[0] == '^' && str[1] == '=')
    {
        token->type = TOKEN_OPERATOR;
        token->value[0] = '^';
        token->value[1] = '=';
        token->value[2] = '\0';
        return str + 2;
    }
    
    /* Shift operators (2 characters) */
    if (str[0] == '<' && str[1] == '<')
    {
        token->type = TOKEN_OPERATOR;
        token->value[0] = '<';
        token->value[1] = '<';
        token->value[2] = '\0';
        return str + 2;
    }
    if (str[0] == '>' && str[1] == '>')
    {
        token->type = TOKEN_OPERATOR;
        token->value[0] = '>';
        token->value[1] = '>';
        token->value[2] = '\0';
        return str + 2;
    }
    
    /* Other 2-character operators */
    if (str[0] == '=' && str[1] == '=')
    {
        token->type = TOKEN_OPERATOR;
        token->value[0] = '=';
        token->value[1] = '=';
        token->value[2] = '\0';
        return str + 2;
    }
    if (str[0] == '!' && str[1] == '=')
    {
        token->type = TOKEN_OPERATOR;
        token->value[0] = '!';
        token->value[1] = '=';
        token->value[2] = '\0';
        return str + 2;
    }
    if (str[0] == '<' && str[1] == '=')
    {
        token->type = TOKEN_OPERATOR;
        token->value[0] = '<';
        token->value[1] = '=';
        token->value[2] = '\0';
        return str + 2;
    }
    if (str[0] == '>' && str[1] == '=')
    {
        token->type = TOKEN_OPERATOR;
        token->value[0] = '>';
        token->value[1] = '=';
        token->value[2] = '\0';
        return str + 2;
    }
    if (str[0] == '&' && str[1] == '&')
    {
        token->type = TOKEN_OPERATOR;
        token->value[0] = '&';
        token->value[1] = '&';
        token->value[2] = '\0';
        return str + 2;
    }
    if (str[0] == '|' && str[1] == '|')
    {
        token->type = TOKEN_OPERATOR;
        token->value[0] = '|';
        token->value[1] = '|';
        token->value[2] = '\0';
        return str + 2;
    }
    
    /* Single-character operators (including bitwise and assignment) */
    if (strchr("+-*/%<>!&|^~=", *str))
    {
        token->type = TOKEN_OPERATOR;
        token->value[0] = *str;
        token->value[1] = '\0';
        return str + 1;
    }
    
    /* Unknown */
    token->type = TOKEN_ERROR;
    return str;
}

/*****************************************************************************
 * Simple expression evaluator
 *****************************************************************************/

/* Helper function to check if value is a number */
static BOOL is_number_value(const char *val)
{
    if (!val || !*val) return FALSE;
    /* Handle negative numbers */
    if (*val == '-') val++;
    if (!*val) return FALSE;
    return isdigit(*val) || (*val == '.' && isdigit(val[1]));
}

/* Helper function for safe string-to-long conversion
 * JavaScript uses 32-bit signed integers for bitwise operations
 * This function converts a string to int32_t, clamping to INT32 range
 */
static long safe_to_int32(const char *val)
{
    double d;
    long result;
    
    if (!val || !*val) return 0;
    
    d = atof(val);
    
    /* Handle infinity and NaN */
    if (d != d) return 0;  /* NaN check */
    if (d > 2147483647.0) return 2147483647L;
    if (d < -2147483648.0) return -2147483648L;
    
    result = (long)d;
    return result;
}

/* Helper function to check if value is an array literal */
static BOOL is_array_value(const char *val)
{
    if (!val || !*val) return FALSE;
    return val[0] == '[';
}

/* Helper function to get array length from array string [1,2,3] */
static int get_array_length(const char *arr)
{
    int len = 0;
    int depth = 0;
    const char *p = arr;
    
    if (!arr || arr[0] != '[') return 0;
    
    /* Skip opening bracket */
    p++;
    
    /* Handle empty array */
    while (*p && isspace(*p)) p++;
    if (*p == ']') return 0;
    
    /* Count elements */
    len = 1;  /* At least one element if not empty */
    
    while (*p && *p != '\0')
    {
        if (*p == '[') depth++;
        else if (*p == ']')
        {
            if (depth > 0) depth--;
            else break;  /* End of array */
        }
        else if (*p == ',' && depth == 0)
        {
            len++;
        }
        p++;
    }
    
    return len;
}

/* Helper function to get array element at index from array string [1,2,3] */
static BOOL get_array_element(const char *arr, int index, char *result, ULONG resultSize)
{
    int current_index = 0;
    int depth = 0;
    const char *p = arr;
    const char *elem_start = NULL;
    
    if (!arr || arr[0] != '[' || index < 0)
    {
        strncpy(result, "undefined", resultSize - 1);
        result[resultSize - 1] = '\0';
        return TRUE;
    }
    
    /* Skip opening bracket */
    p++;
    
    /* Skip whitespace */
    while (*p && isspace(*p)) p++;
    
    /* Handle empty array */
    if (*p == ']')
    {
        strncpy(result, "undefined", resultSize - 1);
        result[resultSize - 1] = '\0';
        return TRUE;
    }
    
    /* Find element at index */
    elem_start = p;
    
    while (*p && *p != '\0')
    {
        if (*p == '[') depth++;
        else if (*p == ']')
        {
            if (depth > 0)
            {
                depth--;
            }
            else
            {
                /* End of array */
                if (current_index == index)
                {
                    /* Copy element */
                    int elem_len = p - elem_start;
                    /* Trim trailing whitespace */
                    while (elem_len > 0 && isspace(elem_start[elem_len - 1]))
                        elem_len--;
                    if (elem_len >= resultSize) elem_len = resultSize - 1;
                    strncpy(result, elem_start, elem_len);
                    result[elem_len] = '\0';
                    return TRUE;
                }
                else
                {
                    /* Index out of bounds */
                    strncpy(result, "undefined", resultSize - 1);
                    result[resultSize - 1] = '\0';
                    return TRUE;
                }
            }
        }
        else if (*p == ',' && depth == 0)
        {
            if (current_index == index)
            {
                /* Copy element */
                int elem_len = p - elem_start;
                /* Trim trailing whitespace */
                while (elem_len > 0 && isspace(elem_start[elem_len - 1]))
                    elem_len--;
                if (elem_len >= resultSize) elem_len = resultSize - 1;
                strncpy(result, elem_start, elem_len);
                result[elem_len] = '\0';
                return TRUE;
            }
            current_index++;
            p++;
            /* Skip whitespace after comma */
            while (*p && isspace(*p)) p++;
            elem_start = p;
            continue;
        }
        p++;
    }
    
    /* Index out of bounds */
    strncpy(result, "undefined", resultSize - 1);
    result[resultSize - 1] = '\0';
    return TRUE;
}

/* Array helper constants
 * MAX_ARRAY_ELEMENTS: Limit of 32 elements balances functionality with stack usage
 * MAX_ELEMENT_SIZE: 256 chars per element handles typical string/number values
 * MAX_SEPARATOR_SIZE: 256 chars for join separator handles most use cases
 * Note: Stack usage is ~8KB (32*256) per function call - suitable for typical stacks
 */
#define MAX_ARRAY_ELEMENTS 32
#define MAX_ELEMENT_SIZE 256
#define MAX_SEPARATOR_SIZE 256

/* Helper function to join array elements with a separator */
static BOOL array_join(const char *arr, const char *separator, char *result, ULONG resultSize)
{
    int depth = 0;
    const char *p = arr;
    const char *elem_start = NULL;
    char *out = result;
    ULONG remaining = resultSize;
    BOOL first = TRUE;
    char sep[MAX_SEPARATOR_SIZE];
    
    if (!arr || arr[0] != '[')
    {
        strncpy(result, "", resultSize - 1);
        result[resultSize - 1] = '\0';
        return TRUE;
    }
    
    /* Default separator is comma */
    if (!separator || !separator[0])
        strcpy(sep, ",");
    else
    {
        /* Validate separator length to prevent buffer overflow */
        strncpy(sep, separator, sizeof(sep) - 1);
        sep[sizeof(sep) - 1] = '\0';
    }
    
    /* Skip opening bracket */
    p++;
    
    /* Skip whitespace */
    while (*p && isspace(*p)) p++;
    
    /* Handle empty array */
    if (*p == ']')
    {
        result[0] = '\0';
        return TRUE;
    }
    
    /* Process each element */
    elem_start = p;
    
    while (*p && *p != '\0')
    {
        if (*p == '[') depth++;
        else if (*p == ']')
        {
            if (depth > 0)
            {
                depth--;
                p++;
                continue;
            }
            else
            {
                /* Last element */
                int elem_len = p - elem_start;
                while (elem_len > 0 && isspace(elem_start[elem_len - 1]))
                    elem_len--;
                
                if (!first && remaining > strlen(sep))
                {
                    strcpy(out, sep);
                    out += strlen(sep);
                    remaining -= strlen(sep);
                }
                
                if (elem_len > 0 && remaining > elem_len)
                {
                    strncpy(out, elem_start, elem_len);
                    out[elem_len] = '\0';
                    out += elem_len;
                    remaining -= elem_len;
                }
                break;
            }
        }
        else if (*p == ',' && depth == 0)
        {
            /* Element boundary */
            int elem_len = p - elem_start;
            while (elem_len > 0 && isspace(elem_start[elem_len - 1]))
                elem_len--;
            
            if (!first && remaining > strlen(sep))
            {
                strcpy(out, sep);
                out += strlen(sep);
                remaining -= strlen(sep);
            }
            
            if (elem_len > 0 && remaining > elem_len)
            {
                strncpy(out, elem_start, elem_len);
                out[elem_len] = '\0';
                out += elem_len;
                remaining -= elem_len;
            }
            
            first = FALSE;
            p++;
            while (*p && isspace(*p)) p++;
            elem_start = p;
            continue;
        }
        p++;
    }
    
    *out = '\0';
    return TRUE;
}

/* Helper function to reverse array elements */
static BOOL array_reverse(const char *arr, char *result, ULONG resultSize)
{
    char elements[MAX_ARRAY_ELEMENTS][MAX_ELEMENT_SIZE];
    int count = 0;
    int depth = 0;
    const char *p = arr;
    const char *elem_start = NULL;
    
    if (!arr || arr[0] != '[')
    {
        strncpy(result, arr, resultSize - 1);
        result[resultSize - 1] = '\0';
        return TRUE;
    }
    
    /* Skip opening bracket */
    p++;
    
    /* Skip whitespace */
    while (*p && isspace(*p)) p++;
    
    /* Handle empty array */
    if (*p == ']')
    {
        strncpy(result, "[]", resultSize - 1);
        result[resultSize - 1] = '\0';
        return TRUE;
    }
    
    /* Parse elements into array */
    elem_start = p;
    
    while (*p && *p != '\0' && count < MAX_ARRAY_ELEMENTS)
    {
        if (*p == '[') depth++;
        else if (*p == ']')
        {
            if (depth > 0)
            {
                depth--;
            }
            else
            {
                /* Last element */
                int elem_len = p - elem_start;
                while (elem_len > 0 && isspace(elem_start[elem_len - 1]))
                    elem_len--;
                if (elem_len >= MAX_ELEMENT_SIZE) elem_len = MAX_ELEMENT_SIZE - 1;
                strncpy(elements[count], elem_start, elem_len);
                elements[count][elem_len] = '\0';
                count++;
                break;
            }
        }
        else if (*p == ',' && depth == 0)
        {
            /* Element boundary */
            int elem_len = p - elem_start;
            while (elem_len > 0 && isspace(elem_start[elem_len - 1]))
                elem_len--;
            if (elem_len >= MAX_ELEMENT_SIZE) elem_len = MAX_ELEMENT_SIZE - 1;
            strncpy(elements[count], elem_start, elem_len);
            elements[count][elem_len] = '\0';
            count++;
            
            p++;
            while (*p && isspace(*p)) p++;
            elem_start = p;
            continue;
        }
        p++;
    }
    
    /* Build reversed array with bounds checking */
    int i;
    int pos = 0;
    
    /* Check if result buffer is large enough for opening bracket */
    if (resultSize < 2)
        return FALSE;
    
    result[pos++] = '[';
    
    for (i = count - 1; i >= 0; i--)
    {
        /* Add comma separator if not first element */
        if (i < count - 1)
        {
            if (pos >= resultSize - 1)
                return FALSE;
            result[pos++] = ',';
        }
        
        /* Add element with bounds checking */
        int elem_len = strlen(elements[i]);
        if (pos + elem_len >= resultSize - 1)
            return FALSE;
        
        strcpy(result + pos, elements[i]);
        pos += elem_len;
    }
    
    /* Add closing bracket */
    if (pos >= resultSize - 1)
        return FALSE;
    
    result[pos++] = ']';
    result[pos] = '\0';
    
    return TRUE;
}

/* Helper function to slice array elements */
static BOOL array_slice(const char *arr, int start, int end, char *result, ULONG resultSize)
{
    char elements[MAX_ARRAY_ELEMENTS][MAX_ELEMENT_SIZE];
    int count = 0;
    int depth = 0;
    const char *p = arr;
    const char *elem_start = NULL;
    int len;
    
    if (!arr || arr[0] != '[')
    {
        strncpy(result, "[]", resultSize - 1);
        result[resultSize - 1] = '\0';
        return TRUE;
    }
    
    /* Parse elements into array */
    p++;
    while (*p && isspace(*p)) p++;
    
    if (*p == ']')
    {
        strncpy(result, "[]", resultSize - 1);
        result[resultSize - 1] = '\0';
        return TRUE;
    }
    
    elem_start = p;
    
    while (*p && *p != '\0' && count < MAX_ARRAY_ELEMENTS)
    {
        if (*p == '[') depth++;
        else if (*p == ']')
        {
            if (depth > 0)
            {
                depth--;
            }
            else
            {
                int elem_len = p - elem_start;
                while (elem_len > 0 && isspace(elem_start[elem_len - 1]))
                    elem_len--;
                if (elem_len >= MAX_ELEMENT_SIZE) elem_len = MAX_ELEMENT_SIZE - 1;
                strncpy(elements[count], elem_start, elem_len);
                elements[count][elem_len] = '\0';
                count++;
                break;
            }
        }
        else if (*p == ',' && depth == 0)
        {
            int elem_len = p - elem_start;
            while (elem_len > 0 && isspace(elem_start[elem_len - 1]))
                elem_len--;
            if (elem_len >= MAX_ELEMENT_SIZE) elem_len = MAX_ELEMENT_SIZE - 1;
            strncpy(elements[count], elem_start, elem_len);
            elements[count][elem_len] = '\0';
            count++;
            
            p++;
            while (*p && isspace(*p)) p++;
            elem_start = p;
            continue;
        }
        p++;
    }
    
    len = count;
    
    /* Handle negative indices */
    if (start < 0) start = len + start;
    if (end < 0) end = len + end;
    
    /* Clamp to valid range */
    if (start < 0) start = 0;
    if (start > len) start = len;
    if (end < 0) end = 0;
    if (end > len) end = len;
    if (end < start) end = start;
    
    /* Build sliced array with bounds checking */
    int i;
    int pos = 0;
    
    /* Check if result buffer is large enough for opening bracket */
    if (resultSize < 2)
        return FALSE;
    
    result[pos++] = '[';
    
    for (i = start; i < end; i++)
    {
        /* Add comma separator if not first element */
        if (i > start)
        {
            if (pos >= resultSize - 1)
                return FALSE;
            result[pos++] = ',';
        }
        
        /* Add element with bounds checking */
        int elem_len = strlen(elements[i]);
        if (pos + elem_len >= resultSize - 1)
            return FALSE;
        
        strcpy(result + pos, elements[i]);
        pos += elem_len;
    }
    
    /* Add closing bracket */
    if (pos >= resultSize - 1)
        return FALSE;
    
    result[pos++] = ']';
    result[pos] = '\0';
    
    return TRUE;
}

/* Helper function to compare two string values */
static BOOL compare_values(const char *val1, const char *val2, const char *op, char *result, ULONG resultSize)
{
    double num1, num2;
    BOOL is_num1 = is_number_value(val1);
    BOOL is_num2 = is_number_value(val2);
    BOOL comp_result = FALSE;
    BOOL is_strict = (strcmp(op, "===") == 0 || strcmp(op, "!==") == 0);
    
    /* Strict equality - types must match */
    if (is_strict)
    {
        /* If types differ, strict equality always returns false, strict inequality returns true */
        if (is_num1 != is_num2)
        {
            comp_result = (strcmp(op, "!==") == 0);
        }
        else if (is_num1 && is_num2)
        {
            /* Both are numbers */
            num1 = atof(val1);
            num2 = atof(val2);
            if (strcmp(op, "===") == 0)
                comp_result = (num1 == num2);
            else /* !== */
                comp_result = (num1 != num2);
        }
        else
        {
            /* Both are strings */
            int cmp = strcmp(val1, val2);
            if (strcmp(op, "===") == 0)
                comp_result = (cmp == 0);
            else /* !== */
                comp_result = (cmp != 0);
        }
        strncpy(result, comp_result ? "true" : "false", resultSize - 1);
        result[resultSize - 1] = '\0';
        return TRUE;
    }
    
    /* Loose equality - numeric comparison if both are numbers */
    if (is_num1 && is_num2)
    {
        num1 = atof(val1);
        num2 = atof(val2);
        
        if (strcmp(op, "==") == 0)
            comp_result = (num1 == num2);
        else if (strcmp(op, "!=") == 0)
            comp_result = (num1 != num2);
        else if (strcmp(op, "<") == 0)
            comp_result = (num1 < num2);
        else if (strcmp(op, ">") == 0)
            comp_result = (num1 > num2);
        else if (strcmp(op, "<=") == 0)
            comp_result = (num1 <= num2);
        else if (strcmp(op, ">=") == 0)
            comp_result = (num1 >= num2);
        else
            return FALSE;
    }
    /* String comparison */
    else
    {
        int cmp = strcmp(val1, val2);
        
        if (strcmp(op, "==") == 0)
            comp_result = (cmp == 0);
        else if (strcmp(op, "!=") == 0)
            comp_result = (cmp != 0);
        else if (strcmp(op, "<") == 0)
            comp_result = (cmp < 0);
        else if (strcmp(op, ">") == 0)
            comp_result = (cmp > 0);
        else if (strcmp(op, "<=") == 0)
            comp_result = (cmp <= 0);
        else if (strcmp(op, ">=") == 0)
            comp_result = (cmp >= 0);
        else
            return FALSE;
    }
    
    strncpy(result, comp_result ? "true" : "false", resultSize - 1);
    result[resultSize - 1] = '\0';
    return TRUE;
}

/* Helper function to check if value is truthy */
static BOOL is_truthy(const char *val)
{
    if (!val || strlen(val) == 0)
        return FALSE;
    if (strcmp(val, "false") == 0)
        return FALSE;
    if (strcmp(val, "null") == 0)
        return FALSE;
    if (strcmp(val, "undefined") == 0)
        return FALSE;
    if (strcmp(val, "NaN") == 0)
        return FALSE;
    if (strcmp(val, "0") == 0)
        return FALSE;
    return TRUE;
}

/* Forward declaration for recursive evaluation */
static LONG evaluate_expression_internal(const char **p, struct V8Context *context,
                                        STRPTR result, ULONG resultSize, int min_precedence);

/* Forward declaration for ternary evaluation */
static LONG evaluate_ternary(const char **p, struct V8Context *context,
                            STRPTR result, ULONG resultSize);

/* Get operator precedence (higher number = higher precedence)
 * JavaScript operator precedence (from lowest to highest):
 *  0. , (comma operator)
 *  0.5. = += -= etc (assignment operators - right to left)
 *  1. || (logical OR)
 *  2. && (logical AND)
 *  3. | (bitwise OR)
 *  4. ^ (bitwise XOR)
 *  5. & (bitwise AND)
 *  6. ==, !=, ===, !== (equality)
 *  7. <, >, <=, >= (relational)
 *  8. <<, >>, >>> (bitwise shift)
 *  9. +, - (addition/subtraction)
 * 10. *, /, % (multiplication/division/modulo)
 * Note: Unary operators (!, ~, unary -, ++, --) are handled separately in evaluate_primary
 */
static int get_precedence(const char *op)
{
    /* Assignment operators have lowest precedence (right-to-left) */
    if (strcmp(op, "=") == 0 ||
        strcmp(op, "+=") == 0 || strcmp(op, "-=") == 0 ||
        strcmp(op, "*=") == 0 || strcmp(op, "/=") == 0 ||
        strcmp(op, "%=") == 0 || strcmp(op, "&=") == 0 ||
        strcmp(op, "|=") == 0 || strcmp(op, "^=") == 0 ||
        strcmp(op, "<<=") == 0 || strcmp(op, ">>=") == 0 ||
        strcmp(op, ">>>=") == 0) return 0;
    if (strcmp(op, "||") == 0) return 1;
    if (strcmp(op, "&&") == 0) return 2;
    if (strlen(op) == 1 && op[0] == '|') return 3;  /* Bitwise OR */
    if (strlen(op) == 1 && op[0] == '^') return 4;  /* Bitwise XOR */
    if (strlen(op) == 1 && op[0] == '&') return 5;  /* Bitwise AND */
    if (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0 ||
        strcmp(op, "===") == 0 || strcmp(op, "!==") == 0) return 6;
    if (strcmp(op, "<") == 0 || strcmp(op, ">") == 0 ||
        strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0) return 7;
    if (strcmp(op, "<<") == 0 || strcmp(op, ">>") == 0 || strcmp(op, ">>>") == 0) return 8;  /* Shift operators */
    if (strlen(op) == 1 && (op[0] == '+' || op[0] == '-')) return 9;
    if (strlen(op) == 1 && (op[0] == '*' || op[0] == '/' || op[0] == '%')) return 10;
    return -1;  /* Unknown operator */
}

/* Evaluate a primary expression (number, string, identifier, or parenthesized expression) */
static LONG evaluate_primary(const char **p, struct V8Context *context,
                             STRPTR result, ULONG resultSize)
{
    Token token;
    const char *start = *p;
    
    *p = get_token(*p, &token);
    
    /* Handle prefix increment (++x) */
    if (token.type == TOKEN_INCREMENT)
    {
        Token var_token;
        *p = get_token(*p, &var_token);
        
        if (var_token.type == TOKEN_IDENTIFIER && context)
        {
            CONST_STRPTR prop_value = V8_GetPropertyValue(context, var_token.value);
            double num = 0;
            if (prop_value && is_number_value(prop_value))
                num = atof(prop_value);
            num++;
            
            char new_val[64];
            if (num == (long)num)
                snprintf(new_val, sizeof(new_val), "%ld", (long)num);
            else
                snprintf(new_val, sizeof(new_val), "%.10g", num);
            
            V8_SetPropertyValue(context, var_token.value, new_val);
            strncpy(result, new_val, resultSize - 1);
            result[resultSize - 1] = '\0';
            return V8_SUCCESS;
        }
        return V8_ERROR_SYNTAX;
    }
    
    /* Handle prefix decrement (--x) */
    if (token.type == TOKEN_DECREMENT)
    {
        Token var_token;
        *p = get_token(*p, &var_token);
        
        if (var_token.type == TOKEN_IDENTIFIER && context)
        {
            CONST_STRPTR prop_value = V8_GetPropertyValue(context, var_token.value);
            double num = 0;
            if (prop_value && is_number_value(prop_value))
                num = atof(prop_value);
            num--;
            
            char new_val[64];
            if (num == (long)num)
                snprintf(new_val, sizeof(new_val), "%ld", (long)num);
            else
                snprintf(new_val, sizeof(new_val), "%.10g", num);
            
            V8_SetPropertyValue(context, var_token.value, new_val);
            strncpy(result, new_val, resultSize - 1);
            result[resultSize - 1] = '\0';
            return V8_SUCCESS;
        }
        return V8_ERROR_SYNTAX;
    }
    
    /* Handle unary NOT */
    if (token.type == TOKEN_OPERATOR && strcmp(token.value, "!") == 0)
    {
        char operand_result[256];
        LONG rc = evaluate_primary(p, context, operand_result, sizeof(operand_result));
        if (rc != V8_SUCCESS) return rc;
        
        BOOL val = is_truthy(operand_result);
        strncpy(result, val ? "false" : "true", resultSize - 1);
        result[resultSize - 1] = '\0';
        return V8_SUCCESS;
    }
    
    /* Handle unary minus (negative numbers) */
    if (token.type == TOKEN_OPERATOR && strcmp(token.value, "-") == 0)
    {
        char operand_result[256];
        LONG rc = evaluate_primary(p, context, operand_result, sizeof(operand_result));
        if (rc != V8_SUCCESS) return rc;
        
        /* Negate the numeric value */
        if (is_number_value(operand_result))
        {
            double num = atof(operand_result);
            num = -num;
            if (num == (long)num)
                snprintf(result, resultSize, "%ld", (long)num);
            else
                snprintf(result, resultSize, "%.10g", num);
        }
        else
        {
            /* NaN for non-numeric operand */
            strncpy(result, "NaN", resultSize - 1);
            result[resultSize - 1] = '\0';
        }
        return V8_SUCCESS;
    }
    
    /* Handle unary plus (convert to number) */
    if (token.type == TOKEN_OPERATOR && strcmp(token.value, "+") == 0)
    {
        char operand_result[256];
        LONG rc = evaluate_primary(p, context, operand_result, sizeof(operand_result));
        if (rc != V8_SUCCESS) return rc;
        
        /* Convert to number */
        if (is_number_value(operand_result))
        {
            double num = atof(operand_result);
            if (num == (long)num)
                snprintf(result, resultSize, "%ld", (long)num);
            else
                snprintf(result, resultSize, "%.10g", num);
        }
        else
        {
            /* NaN for non-numeric operand */
            strncpy(result, "NaN", resultSize - 1);
            result[resultSize - 1] = '\0';
        }
        return V8_SUCCESS;
    }
    
    /* Handle bitwise NOT (~) */
    if (token.type == TOKEN_OPERATOR && strcmp(token.value, "~") == 0)
    {
        char operand_result[256];
        LONG rc = evaluate_primary(p, context, operand_result, sizeof(operand_result));
        if (rc != V8_SUCCESS) return rc;
        
        /* Bitwise NOT on 32-bit integer */
        if (is_number_value(operand_result))
        {
            long num = safe_to_int32(operand_result);
            num = ~num;
            snprintf(result, resultSize, "%ld", num);
        }
        else
        {
            /* ~NaN = -1 in JavaScript */
            strncpy(result, "-1", resultSize - 1);
            result[resultSize - 1] = '\0';
        }
        return V8_SUCCESS;
    }
    
    /* Handle parentheses */
    if (token.type == TOKEN_LPAREN)
    {
        LONG rc = evaluate_ternary(p, context, result, resultSize);
        if (rc != V8_SUCCESS) return rc;
        
        /* Expect closing parenthesis */
        *p = get_token(*p, &token);
        if (token.type != TOKEN_RPAREN)
            return V8_ERROR_SYNTAX;
        
        return V8_SUCCESS;
    }
    
    /* Handle array literals [1, 2, 3] */
    if (token.type == TOKEN_LBRACKET)
    {
        char array_str[1024];
        int array_pos = 0;
        int element_count = 0;
        LONG rc;
        
        array_str[array_pos++] = '[';
        
        /* Check for empty array */
        const char *save_p = *p;
        *p = get_token(*p, &token);
        
        if (token.type == TOKEN_RBRACKET)
        {
            /* Empty array [] */
            strcpy(array_str, "[]");
        }
        else
        {
            /* Restore position to parse first element */
            *p = save_p;
            
            /* Parse array elements */
            while (1)
            {
                char element_result[256];
                
                /* Evaluate element expression */
                rc = evaluate_ternary(p, context, element_result, sizeof(element_result));
                if (rc != V8_SUCCESS) return rc;
                
                /* Add element to array string */
                if (element_count > 0)
                {
                    if (array_pos < sizeof(array_str) - 1)
                        array_str[array_pos++] = ',';
                }
                
                int elem_len = strlen(element_result);
                if (array_pos + elem_len < sizeof(array_str) - 2)
                {
                    strcpy(array_str + array_pos, element_result);
                    array_pos += elem_len;
                }
                element_count++;
                
                /* Check for comma or closing bracket */
                *p = get_token(*p, &token);
                
                if (token.type == TOKEN_RBRACKET)
                {
                    /* End of array */
                    break;
                }
                else if (token.type == TOKEN_COMMA)
                {
                    /* More elements */
                    continue;
                }
                else
                {
                    /* Unexpected token */
                    return V8_ERROR_SYNTAX;
                }
            }
            
            array_str[array_pos++] = ']';
            array_str[array_pos] = '\0';
        }
        
        /* Check for immediate array indexing [1,2,3][0] or property access [1,2,3].length */
        save_p = *p;
        Token next_token;
        *p = get_token(*p, &next_token);
        
        /* Handle array indexing: [1,2,3][0] */
        while (next_token.type == TOKEN_LBRACKET)
        {
            /* Evaluate index expression */
            char index_result[256];
            rc = evaluate_ternary(p, context, index_result, sizeof(index_result));
            if (rc != V8_SUCCESS) return rc;
            
            /* Expect closing bracket */
            *p = get_token(*p, &next_token);
            if (next_token.type != TOKEN_RBRACKET)
                return V8_ERROR_SYNTAX;
            
            /* Get element at index */
            int index = (int)atof(index_result);
            char element[256];
            get_array_element(array_str, index, element, sizeof(element));
            strncpy(array_str, element, sizeof(array_str) - 1);
            array_str[sizeof(array_str) - 1] = '\0';
            
            /* Check for more indexing or property access */
            save_p = *p;
            *p = get_token(*p, &next_token);
        }
        
        /* Handle property access: [1,2,3].length */
        while (next_token.type == TOKEN_DOT)
        {
            Token prop_token;
            *p = get_token(*p, &prop_token);
            
            if (prop_token.type != TOKEN_IDENTIFIER)
                return V8_ERROR_SYNTAX;
            
            /* Handle .length property for arrays */
            if (strcmp(prop_token.value, "length") == 0)
            {
                if (is_array_value(array_str))
                {
                    int len = get_array_length(array_str);
                    snprintf(array_str, sizeof(array_str), "%d", len);
                }
                else
                {
                    /* String length */
                    int len = strlen(array_str);
                    snprintf(array_str, sizeof(array_str), "%d", len);
                }
            }
            /* Handle array methods */
            else if (is_array_value(array_str))
            {
                /* Check if this is a method call */
                const char *save_method_p = *p;
                Token method_paren;
                *p = get_token(*p, &method_paren);
                
                if (method_paren.type == TOKEN_LPAREN)
                {
                    /* This is a method call */
                    if (strcmp(prop_token.value, "join") == 0)
                    {
                        /* Parse separator argument or use default */
                        char separator[256] = ",";
                        Token arg_token;
                        *p = get_token(*p, &arg_token);
                        
                        if (arg_token.type == TOKEN_STRING)
                        {
                            strncpy(separator, arg_token.value, sizeof(separator) - 1);
                            separator[sizeof(separator) - 1] = '\0';
                            *p = get_token(*p, &arg_token);  /* Get closing paren */
                        }
                        
                        if (arg_token.type != TOKEN_RPAREN)
                            return V8_ERROR_SYNTAX;
                        
                        array_join(array_str, separator, array_str, sizeof(array_str));
                    }
                    else if (strcmp(prop_token.value, "reverse") == 0)
                    {
                        /* Expect closing paren */
                        Token close_paren;
                        *p = get_token(*p, &close_paren);
                        if (close_paren.type != TOKEN_RPAREN)
                            return V8_ERROR_SYNTAX;
                        
                        array_reverse(array_str, array_str, sizeof(array_str));
                    }
                    else if (strcmp(prop_token.value, "slice") == 0)
                    {
                        /* Parse start and optional end arguments */
                        char start_str[256];
                        char end_str[256];
                        int start_idx = 0;
                        int end_idx = get_array_length(array_str);
                        
                        /* Get start argument */
                        LONG rc = evaluate_ternary(p, context, start_str, sizeof(start_str));
                        if (rc != V8_SUCCESS) return rc;
                        start_idx = atoi(start_str);
                        
                        /* Check for comma (optional end argument) */
                        Token comma_token;
                        const char *before_comma = *p;
                        *p = get_token(*p, &comma_token);
                        
                        if (comma_token.type == TOKEN_COMMA)
                        {
                            /* Get end argument */
                            rc = evaluate_ternary(p, context, end_str, sizeof(end_str));
                            if (rc != V8_SUCCESS) return rc;
                            end_idx = atoi(end_str);
                        }
                        else
                        {
                            /* No end argument, restore position */
                            *p = before_comma;
                        }
                        
                        /* Expect closing paren */
                        Token close_paren;
                        *p = get_token(*p, &close_paren);
                        if (close_paren.type != TOKEN_RPAREN)
                            return V8_ERROR_SYNTAX;
                        
                        array_slice(array_str, start_idx, end_idx, array_str, sizeof(array_str));
                    }
                    else
                    {
                        /* Method not supported */
                        strncpy(result, "undefined", resultSize - 1);
                        result[resultSize - 1] = '\0';
                        return V8_SUCCESS;
                    }
                }
                else
                {
                    /* Not a method call, restore position */
                    *p = save_method_p;
                    
                    /* Other properties not supported yet */
                    strncpy(result, "undefined", resultSize - 1);
                    result[resultSize - 1] = '\0';
                    return V8_SUCCESS;
                }
            }
            else
            {
                /* Other properties not supported yet */
                strncpy(result, "undefined", resultSize - 1);
                result[resultSize - 1] = '\0';
                return V8_SUCCESS;
            }
            
            /* Check for more property access or indexing */
            save_p = *p;
            *p = get_token(*p, &next_token);
        }
        
        /* Restore position if not consumed */
        *p = save_p;
        
        strncpy(result, array_str, resultSize - 1);
        result[resultSize - 1] = '\0';
        return V8_SUCCESS;
    }
    
    /* Handle literals and identifiers */
    if (token.type == TOKEN_NUMBER || token.type == TOKEN_STRING ||
        token.type == TOKEN_BOOLEAN || token.type == TOKEN_NULL ||
        token.type == TOKEN_UNDEFINED)
    {
        strncpy(result, token.value, resultSize - 1);
        result[resultSize - 1] = '\0';
        
        /* Check for postfix increment/decrement (not applicable to literals) */
        return V8_SUCCESS;
    }
    
    /* Handle property reference */
    if (token.type == TOKEN_IDENTIFIER && context)
    {
        char var_name[256];
        char current_value[1024];
        strncpy(var_name, token.value, sizeof(var_name) - 1);
        var_name[sizeof(var_name) - 1] = '\0';
        
        CONST_STRPTR prop_value = V8_GetPropertyValue(context, var_name);
        
        /* Copy current value for potential array/property access */
        if (prop_value)
        {
            strncpy(current_value, prop_value, sizeof(current_value) - 1);
            current_value[sizeof(current_value) - 1] = '\0';
        }
        else
        {
            strcpy(current_value, "undefined");
        }
        
        /* Check for array indexing or property access */
        const char *save_p = *p;
        Token next_token;
        *p = get_token(*p, &next_token);
        
        /* Handle array indexing: arr[0] */
        while (next_token.type == TOKEN_LBRACKET)
        {
            if (!is_array_value(current_value))
            {
                /* Not an array, cannot index */
                strncpy(result, "undefined", resultSize - 1);
                result[resultSize - 1] = '\0';
                return V8_SUCCESS;
            }
            
            /* Evaluate index expression */
            char index_result[256];
            LONG rc = evaluate_ternary(p, context, index_result, sizeof(index_result));
            if (rc != V8_SUCCESS) return rc;
            
            /* Expect closing bracket */
            *p = get_token(*p, &next_token);
            if (next_token.type != TOKEN_RBRACKET)
                return V8_ERROR_SYNTAX;
            
            /* Get element at index */
            int index = (int)atof(index_result);
            char element[256];
            get_array_element(current_value, index, element, sizeof(element));
            strncpy(current_value, element, sizeof(current_value) - 1);
            current_value[sizeof(current_value) - 1] = '\0';
            
            /* Check for more indexing or property access */
            save_p = *p;
            *p = get_token(*p, &next_token);
        }
        
        /* Handle property access: arr.length */
        while (next_token.type == TOKEN_DOT)
        {
            Token prop_token;
            *p = get_token(*p, &prop_token);
            
            if (prop_token.type != TOKEN_IDENTIFIER)
                return V8_ERROR_SYNTAX;
            
            /* Handle .length property for arrays and strings */
            if (strcmp(prop_token.value, "length") == 0)
            {
                if (is_array_value(current_value))
                {
                    int len = get_array_length(current_value);
                    snprintf(current_value, sizeof(current_value), "%d", len);
                }
                else if (current_value[0] != '\0' && strcmp(current_value, "undefined") != 0 
                         && strcmp(current_value, "null") != 0)
                {
                    /* String length */
                    int len = strlen(current_value);
                    snprintf(current_value, sizeof(current_value), "%d", len);
                }
                else
                {
                    /* undefined/null don't have length */
                    strncpy(result, "undefined", resultSize - 1);
                    result[resultSize - 1] = '\0';
                    return V8_SUCCESS;
                }
            }
            /* Handle array methods */
            else if (is_array_value(current_value))
            {
                /* Check if this is a method call */
                const char *save_method_p = *p;
                Token method_paren;
                *p = get_token(*p, &method_paren);
                
                if (method_paren.type == TOKEN_LPAREN)
                {
                    /* This is a method call */
                    if (strcmp(prop_token.value, "join") == 0)
                    {
                        /* Parse separator argument or use default */
                        char separator[256] = ",";
                        Token arg_token;
                        *p = get_token(*p, &arg_token);
                        
                        if (arg_token.type == TOKEN_STRING)
                        {
                            strncpy(separator, arg_token.value, sizeof(separator) - 1);
                            separator[sizeof(separator) - 1] = '\0';
                            *p = get_token(*p, &arg_token);  /* Get closing paren */
                        }
                        
                        if (arg_token.type != TOKEN_RPAREN)
                            return V8_ERROR_SYNTAX;
                        
                        array_join(current_value, separator, current_value, sizeof(current_value));
                    }
                    else if (strcmp(prop_token.value, "reverse") == 0)
                    {
                        /* Expect closing paren */
                        Token close_paren;
                        *p = get_token(*p, &close_paren);
                        if (close_paren.type != TOKEN_RPAREN)
                            return V8_ERROR_SYNTAX;
                        
                        array_reverse(current_value, current_value, sizeof(current_value));
                    }
                    else if (strcmp(prop_token.value, "slice") == 0)
                    {
                        /* Parse start and optional end arguments */
                        char start_str[256];
                        char end_str[256];
                        int start_idx = 0;
                        int end_idx = get_array_length(current_value);
                        
                        /* Get start argument */
                        LONG rc = evaluate_ternary(p, context, start_str, sizeof(start_str));
                        if (rc != V8_SUCCESS) return rc;
                        start_idx = atoi(start_str);
                        
                        /* Check for comma (optional end argument) */
                        Token comma_token;
                        const char *before_comma = *p;
                        *p = get_token(*p, &comma_token);
                        
                        if (comma_token.type == TOKEN_COMMA)
                        {
                            /* Get end argument */
                            rc = evaluate_ternary(p, context, end_str, sizeof(end_str));
                            if (rc != V8_SUCCESS) return rc;
                            end_idx = atoi(end_str);
                        }
                        else
                        {
                            /* No end argument, restore position */
                            *p = before_comma;
                        }
                        
                        /* Expect closing paren */
                        Token close_paren;
                        *p = get_token(*p, &close_paren);
                        if (close_paren.type != TOKEN_RPAREN)
                            return V8_ERROR_SYNTAX;
                        
                        array_slice(current_value, start_idx, end_idx, current_value, sizeof(current_value));
                    }
                    else
                    {
                        /* Method not supported */
                        strncpy(result, "undefined", resultSize - 1);
                        result[resultSize - 1] = '\0';
                        return V8_SUCCESS;
                    }
                }
                else
                {
                    /* Not a method call, restore position */
                    *p = save_method_p;
                    
                    /* Other properties not supported yet */
                    strncpy(result, "undefined", resultSize - 1);
                    result[resultSize - 1] = '\0';
                    return V8_SUCCESS;
                }
            }
            else
            {
                /* Other properties not supported yet */
                strncpy(result, "undefined", resultSize - 1);
                result[resultSize - 1] = '\0';
                return V8_SUCCESS;
            }
            
            /* Check for more property access or indexing */
            save_p = *p;
            *p = get_token(*p, &next_token);
        }
        
        /* Handle postfix increment/decrement */
        if (next_token.type == TOKEN_INCREMENT)
        {
            /* Postfix increment (x++) - return old value, then increment */
            double num = 0;
            if (is_number_value(current_value))
                num = atof(current_value);
            
            /* Return old value */
            if (num == (long)num)
                snprintf(result, resultSize, "%ld", (long)num);
            else
                snprintf(result, resultSize, "%.10g", num);
            
            /* Increment and store new value */
            num++;
            char new_val[64];
            if (num == (long)num)
                snprintf(new_val, sizeof(new_val), "%ld", (long)num);
            else
                snprintf(new_val, sizeof(new_val), "%.10g", num);
            V8_SetPropertyValue(context, var_name, new_val);
            
            return V8_SUCCESS;
        }
        else if (next_token.type == TOKEN_DECREMENT)
        {
            /* Postfix decrement (x--) - return old value, then decrement */
            double num = 0;
            if (is_number_value(current_value))
                num = atof(current_value);
            
            /* Return old value */
            if (num == (long)num)
                snprintf(result, resultSize, "%ld", (long)num);
            else
                snprintf(result, resultSize, "%.10g", num);
            
            /* Decrement and store new value */
            num--;
            char new_val[64];
            if (num == (long)num)
                snprintf(new_val, sizeof(new_val), "%ld", (long)num);
            else
                snprintf(new_val, sizeof(new_val), "%.10g", num);
            V8_SetPropertyValue(context, var_name, new_val);
            
            return V8_SUCCESS;
        }
        else
        {
            /* Not an increment/decrement, restore position */
            *p = save_p;
        }
        
        strncpy(result, current_value, resultSize - 1);
        result[resultSize - 1] = '\0';
        return V8_SUCCESS;
    }
    
    return V8_ERROR_SYNTAX;
}

/* Evaluate expression with operator precedence */
static LONG evaluate_expression_internal(const char **p, struct V8Context *context,
                                        STRPTR result, ULONG resultSize, int min_precedence)
{
    char left[256], right[256];
    char var_name[256];  /* Store variable name for assignment operators */
    Token op_token;
    LONG rc;
    
    /* Save position before parsing left operand to capture variable name */
    const char *left_start = *p;
    
    /* Get left operand */
    rc = evaluate_primary(p, context, left, sizeof(left));
    if (rc != V8_SUCCESS) return rc;
    
    /* Try to extract variable name from left operand position */
    var_name[0] = '\0';
    {
        const char *name_p = left_start;
        Token name_token;
        name_p = skip_whitespace(name_p);
        get_token(name_p, &name_token);
        if (name_token.type == TOKEN_IDENTIFIER)
        {
            strncpy(var_name, name_token.value, sizeof(var_name) - 1);
            var_name[sizeof(var_name) - 1] = '\0';
        }
    }
    
    /* Process operators with precedence */
    while (1)
    {
        const char *save_p = *p;
        *p = get_token(*p, &op_token);
        
        /* Check for end, closing parenthesis/bracket, ternary operator tokens, or comma */
        if (op_token.type == TOKEN_END || op_token.type == TOKEN_RPAREN ||
            op_token.type == TOKEN_RBRACKET || op_token.type == TOKEN_QUESTION ||
            op_token.type == TOKEN_COLON || op_token.type == TOKEN_COMMA)
        {
            *p = save_p;  /* Restore position */
            strncpy(result, left, resultSize - 1);
            result[resultSize - 1] = '\0';
            return V8_SUCCESS;
        }
        
        if (op_token.type != TOKEN_OPERATOR)
        {
            *p = save_p;
            strncpy(result, left, resultSize - 1);
            result[resultSize - 1] = '\0';
            return V8_SUCCESS;
        }
        
        int precedence = get_precedence(op_token.value);
        if (precedence < min_precedence)
        {
            *p = save_p;
            strncpy(result, left, resultSize - 1);
            result[resultSize - 1] = '\0';
            return V8_SUCCESS;
        }
        
        /* Get right operand with higher precedence */
        rc = evaluate_expression_internal(p, context, right, sizeof(right), precedence + 1);
        if (rc != V8_SUCCESS) return rc;
        
        /* Apply operator */
        if (strcmp(op_token.value, "&&") == 0)
        {
            BOOL val1 = is_truthy(left);
            BOOL val2 = is_truthy(right);
            strncpy(left, (val1 && val2) ? "true" : "false", sizeof(left) - 1);
        }
        else if (strcmp(op_token.value, "||") == 0)
        {
            BOOL val1 = is_truthy(left);
            BOOL val2 = is_truthy(right);
            strncpy(left, (val1 || val2) ? "true" : "false", sizeof(left) - 1);
        }
        else if (strcmp(op_token.value, "==") == 0 || strcmp(op_token.value, "!=") == 0 ||
                 strcmp(op_token.value, "===") == 0 || strcmp(op_token.value, "!==") == 0 ||
                 strcmp(op_token.value, "<") == 0 || strcmp(op_token.value, ">") == 0 ||
                 strcmp(op_token.value, "<=") == 0 || strcmp(op_token.value, ">=") == 0)
        {
            if (!compare_values(left, right, op_token.value, left, sizeof(left)))
                return V8_ERROR_SYNTAX;
        }
        /* Unsigned right shift (>>>) */
        else if (strcmp(op_token.value, ">>>") == 0)
        {
            unsigned long num1 = (unsigned long)safe_to_int32(left);
            long num2 = safe_to_int32(right);
            unsigned long shift_result;
            
            /* Clamp shift amount to 0-31 range (JavaScript behavior) */
            num2 = num2 & 0x1F;
            
            shift_result = num1 >> num2;
            
            snprintf(left, sizeof(left), "%lu", shift_result);
        }
        /* Signed bitwise shift operators */
        else if (strcmp(op_token.value, "<<") == 0 || strcmp(op_token.value, ">>") == 0)
        {
            long num1 = safe_to_int32(left);
            long num2 = safe_to_int32(right);
            long shift_result;
            
            /* Clamp shift amount to 0-31 range (JavaScript behavior) */
            num2 = num2 & 0x1F;
            
            if (strcmp(op_token.value, "<<") == 0)
                shift_result = num1 << num2;
            else
                shift_result = num1 >> num2;
            
            snprintf(left, sizeof(left), "%ld", shift_result);
        }
        /* Simple assignment (=) */
        else if (strcmp(op_token.value, "=") == 0)
        {
            if (var_name[0] && context)
            {
                V8_SetPropertyValue(context, var_name, right);
                strncpy(left, right, sizeof(left) - 1);
            }
            else
            {
                return V8_ERROR_SYNTAX;
            }
        }
        /* Compound assignment operators */
        else if (strcmp(op_token.value, "+=") == 0)
        {
            if (var_name[0] && context)
            {
                double num_result;
                if (!is_number_value(left) || !is_number_value(right))
                {
                    /* String concatenation */
                    char temp[256];
                    snprintf(temp, sizeof(temp), "%s%s", left, right);
                    V8_SetPropertyValue(context, var_name, temp);
                    strncpy(left, temp, sizeof(left) - 1);
                }
                else
                {
                    num_result = atof(left) + atof(right);
                    if (num_result == (long)num_result)
                        snprintf(left, sizeof(left), "%ld", (long)num_result);
                    else
                        snprintf(left, sizeof(left), "%.10g", num_result);
                    V8_SetPropertyValue(context, var_name, left);
                }
            }
            else
            {
                return V8_ERROR_SYNTAX;
            }
        }
        else if (strcmp(op_token.value, "-=") == 0)
        {
            if (var_name[0] && context)
            {
                double num_result = atof(left) - atof(right);
                if (num_result == (long)num_result)
                    snprintf(left, sizeof(left), "%ld", (long)num_result);
                else
                    snprintf(left, sizeof(left), "%.10g", num_result);
                V8_SetPropertyValue(context, var_name, left);
            }
            else
            {
                return V8_ERROR_SYNTAX;
            }
        }
        else if (strcmp(op_token.value, "*=") == 0)
        {
            if (var_name[0] && context)
            {
                double num_result = atof(left) * atof(right);
                if (num_result == (long)num_result)
                    snprintf(left, sizeof(left), "%ld", (long)num_result);
                else
                    snprintf(left, sizeof(left), "%.10g", num_result);
                V8_SetPropertyValue(context, var_name, left);
            }
            else
            {
                return V8_ERROR_SYNTAX;
            }
        }
        else if (strcmp(op_token.value, "/=") == 0)
        {
            if (var_name[0] && context)
            {
                double num1 = atof(left);
                double num2 = atof(right);
                if (num2 == 0.0)
                {
                    /* Handle sign for Infinity */
                    const char *inf_str = (num1 >= 0.0) ? "Infinity" : "-Infinity";
                    V8_SetPropertyValue(context, var_name, inf_str);
                    strncpy(left, inf_str, sizeof(left) - 1);
                }
                else
                {
                    double num_result = num1 / num2;
                    if (num_result == (long)num_result)
                        snprintf(left, sizeof(left), "%ld", (long)num_result);
                    else
                        snprintf(left, sizeof(left), "%.10g", num_result);
                    V8_SetPropertyValue(context, var_name, left);
                }
            }
            else
            {
                return V8_ERROR_SYNTAX;
            }
        }
        else if (strcmp(op_token.value, "%=") == 0)
        {
            if (var_name[0] && context)
            {
                long num2 = (long)atof(right);
                if (num2 == 0)
                {
                    V8_SetPropertyValue(context, var_name, "NaN");
                    strncpy(left, "NaN", sizeof(left) - 1);
                }
                else
                {
                    long num_result = (long)atof(left) % num2;
                    snprintf(left, sizeof(left), "%ld", num_result);
                    V8_SetPropertyValue(context, var_name, left);
                }
            }
            else
            {
                return V8_ERROR_SYNTAX;
            }
        }
        else if (strcmp(op_token.value, "&=") == 0)
        {
            if (var_name[0] && context)
            {
                long num_result = safe_to_int32(left) & safe_to_int32(right);
                snprintf(left, sizeof(left), "%ld", num_result);
                V8_SetPropertyValue(context, var_name, left);
            }
            else
            {
                return V8_ERROR_SYNTAX;
            }
        }
        else if (strcmp(op_token.value, "|=") == 0)
        {
            if (var_name[0] && context)
            {
                long num_result = safe_to_int32(left) | safe_to_int32(right);
                snprintf(left, sizeof(left), "%ld", num_result);
                V8_SetPropertyValue(context, var_name, left);
            }
            else
            {
                return V8_ERROR_SYNTAX;
            }
        }
        else if (strcmp(op_token.value, "^=") == 0)
        {
            if (var_name[0] && context)
            {
                long num_result = safe_to_int32(left) ^ safe_to_int32(right);
                snprintf(left, sizeof(left), "%ld", num_result);
                V8_SetPropertyValue(context, var_name, left);
            }
            else
            {
                return V8_ERROR_SYNTAX;
            }
        }
        else if (strcmp(op_token.value, "<<=") == 0)
        {
            if (var_name[0] && context)
            {
                long num1 = safe_to_int32(left);
                long num2 = safe_to_int32(right) & 0x1F;
                long num_result = num1 << num2;
                snprintf(left, sizeof(left), "%ld", num_result);
                V8_SetPropertyValue(context, var_name, left);
            }
            else
            {
                return V8_ERROR_SYNTAX;
            }
        }
        else if (strcmp(op_token.value, ">>=") == 0)
        {
            if (var_name[0] && context)
            {
                long num1 = safe_to_int32(left);
                long num2 = safe_to_int32(right) & 0x1F;
                long num_result = num1 >> num2;
                snprintf(left, sizeof(left), "%ld", num_result);
                V8_SetPropertyValue(context, var_name, left);
            }
            else
            {
                return V8_ERROR_SYNTAX;
            }
        }
        else if (strcmp(op_token.value, ">>>=") == 0)
        {
            if (var_name[0] && context)
            {
                unsigned long num1 = (unsigned long)safe_to_int32(left);
                long num2 = safe_to_int32(right) & 0x1F;
                unsigned long num_result = num1 >> num2;
                snprintf(left, sizeof(left), "%lu", num_result);
                V8_SetPropertyValue(context, var_name, left);
            }
            else
            {
                return V8_ERROR_SYNTAX;
            }
        }
        /* Bitwise AND (single &) */
        else if (strlen(op_token.value) == 1 && op_token.value[0] == '&')
        {
            long num1 = safe_to_int32(left);
            long num2 = safe_to_int32(right);
            snprintf(left, sizeof(left), "%ld", num1 & num2);
        }
        /* Bitwise OR (single |) */
        else if (strlen(op_token.value) == 1 && op_token.value[0] == '|')
        {
            long num1 = safe_to_int32(left);
            long num2 = safe_to_int32(right);
            snprintf(left, sizeof(left), "%ld", num1 | num2);
        }
        /* Bitwise XOR (^) */
        else if (strlen(op_token.value) == 1 && op_token.value[0] == '^')
        {
            long num1 = safe_to_int32(left);
            long num2 = safe_to_int32(right);
            snprintf(left, sizeof(left), "%ld", num1 ^ num2);
        }
        else if (strlen(op_token.value) == 1 && op_token.value[0] == '+')
        {
            /* Check if either operand is not a number (string concatenation) */
            if (!is_number_value(left) || !is_number_value(right))
            {
                /* String concatenation */
                char temp[256];
                snprintf(temp, sizeof(temp), "%s%s", left, right);
                strncpy(left, temp, sizeof(left) - 1);
                left[sizeof(left) - 1] = '\0';
            }
            else
            {
                /* Numeric addition */
                double num1 = atof(left);
                double num2 = atof(right);
                double num_result = num1 + num2;
                
                if (num_result == (long)num_result)
                    snprintf(left, sizeof(left), "%ld", (long)num_result);
                else
                    snprintf(left, sizeof(left), "%.10g", num_result);
            }
        }
        else if (strlen(op_token.value) == 1 && 
                 (op_token.value[0] == '-' || op_token.value[0] == '*' ||
                  op_token.value[0] == '/' || op_token.value[0] == '%'))
        {
            /* Arithmetic operations */
            double num1 = atof(left);
            double num2 = atof(right);
            double num_result;
            
            switch (op_token.value[0])
            {
                case '-': num_result = num1 - num2; break;
                case '*': num_result = num1 * num2; break;
                case '/':
                    if (num2 == 0.0)
                    {
                        /* Handle sign for Infinity */
                        const char *inf_str = (num1 >= 0.0) ? "Infinity" : "-Infinity";
                        strncpy(left, inf_str, sizeof(left) - 1);
                        left[sizeof(left) - 1] = '\0';
                        continue;
                    }
                    num_result = num1 / num2;
                    break;
                case '%':
                    if (num2 == 0.0)
                    {
                        strncpy(left, "NaN", sizeof(left) - 1);
                        left[sizeof(left) - 1] = '\0';
                        continue;
                    }
                    num_result = (long)num1 % (long)num2;
                    break;
                default:
                    return V8_ERROR_SYNTAX;
            }
            
            if (num_result == (long)num_result)
                snprintf(left, sizeof(left), "%ld", (long)num_result);
            else
                snprintf(left, sizeof(left), "%.10g", num_result);
        }
        else
        {
            return V8_ERROR_SYNTAX;
        }
    }
}

/* Evaluate ternary expression (condition ? true_value : false_value) */
static LONG evaluate_ternary(const char **p, struct V8Context *context,
                            STRPTR result, ULONG resultSize)
{
    char condition[256];
    Token token;
    LONG rc;
    
    /* Evaluate the condition (full expression) */
    rc = evaluate_expression_internal(p, context, condition, sizeof(condition), 0);
    if (rc != V8_SUCCESS) return rc;
    
    /* Check for ternary operator '?' */
    const char *save_p = *p;
    *p = get_token(*p, &token);
    
    if (token.type != TOKEN_QUESTION)
    {
        /* Not a ternary expression, just return the condition result */
        *p = save_p;
        strncpy(result, condition, resultSize - 1);
        result[resultSize - 1] = '\0';
        return V8_SUCCESS;
    }
    
    /* Evaluate true branch (recursively handle nested ternary) */
    char true_result[256];
    rc = evaluate_ternary(p, context, true_result, sizeof(true_result));
    if (rc != V8_SUCCESS) return rc;
    
    /* Expect ':' */
    *p = get_token(*p, &token);
    if (token.type != TOKEN_COLON)
        return V8_ERROR_SYNTAX;
    
    /* Evaluate false branch (recursively handle nested ternary) */
    char false_result[256];
    rc = evaluate_ternary(p, context, false_result, sizeof(false_result));
    if (rc != V8_SUCCESS) return rc;
    
    /* Return appropriate branch based on condition truthiness */
    if (is_truthy(condition))
    {
        strncpy(result, true_result, resultSize - 1);
    }
    else
    {
        strncpy(result, false_result, resultSize - 1);
    }
    result[resultSize - 1] = '\0';
    
    return V8_SUCCESS;
}

LONG V8_EvaluateSimpleExpression(CONST_STRPTR expr, struct V8Context *context,
                                  STRPTR result, ULONG resultSize)
{
    const char *p = expr;
    char temp_result[256];
    Token token;
    
    if (!expr || !result || resultSize == 0)
        return V8_ERROR_INVALID;
    
    /* Skip leading whitespace */
    p = skip_whitespace(p);
    
    /* Handle comma operator - evaluates expressions left to right, returns last value */
    while (1)
    {
        /* Use evaluate_ternary which handles ternary expressions and calls
         * evaluate_expression_internal for regular expressions */
        LONG rc = evaluate_ternary(&p, context, temp_result, sizeof(temp_result));
        if (rc != V8_SUCCESS) return rc;
        
        /* Check for comma operator */
        const char *save_p = p;
        p = get_token(p, &token);
        
        if (token.type == TOKEN_COMMA)
        {
            /* Continue to evaluate next expression */
            continue;
        }
        else
        {
            /* No more comma-separated expressions */
            p = save_p;
            strncpy(result, temp_result, resultSize - 1);
            result[resultSize - 1] = '\0';
            return V8_SUCCESS;
        }
    }
}

/*****************************************************************************
 * Property management functions
 *****************************************************************************/

BOOL V8_SetPropertyValue(struct V8Context *context, CONST_STRPTR name, CONST_STRPTR value)
{
    struct V8Property *prop;
    
    if (!context || !name || !value)
        return FALSE;
    
    /* Check if property already exists */
    prop = (struct V8Property *)context->vc_Properties.mlh_Head;
    while (prop->vp_Node.mln_Succ)
    {
        if (strcmp(prop->vp_Name, name) == 0)
        {
            /* Update existing property */
            FreeVec(prop->vp_Value);
            prop->vp_Value = AllocVec(strlen(value) + 1, MEMF_PUBLIC);
            if (!prop->vp_Value)
                return FALSE;
            strcpy(prop->vp_Value, value);
            return TRUE;
        }
        prop = (struct V8Property *)prop->vp_Node.mln_Succ;
    }
    
    /* Create new property */
    prop = AllocVec(sizeof(struct V8Property), MEMF_PUBLIC | MEMF_CLEAR);
    if (!prop)
        return FALSE;
    
    prop->vp_Name = AllocVec(strlen(name) + 1, MEMF_PUBLIC);
    if (!prop->vp_Name)
    {
        FreeVec(prop);
        return FALSE;
    }
    strcpy(prop->vp_Name, name);
    
    prop->vp_Value = AllocVec(strlen(value) + 1, MEMF_PUBLIC);
    if (!prop->vp_Value)
    {
        FreeVec(prop->vp_Name);
        FreeVec(prop);
        return FALSE;
    }
    strcpy(prop->vp_Value, value);
    
    AddTail((struct List *)&context->vc_Properties, (struct Node *)&prop->vp_Node);
    return TRUE;
}

CONST_STRPTR V8_GetPropertyValue(struct V8Context *context, CONST_STRPTR name)
{
    struct V8Property *prop;
    
    if (!context || !name)
        return NULL;
    
    /* Search for property */
    prop = (struct V8Property *)context->vc_Properties.mlh_Head;
    while (prop->vp_Node.mln_Succ)
    {
        if (strcmp(prop->vp_Name, name) == 0)
            return prop->vp_Value;
        prop = (struct V8Property *)prop->vp_Node.mln_Succ;
    }
    
    return NULL;
}

void V8_FreeProperties(struct V8Context *context)
{
    struct V8Property *prop, *next;
    
    if (!context)
        return;
    
    prop = (struct V8Property *)context->vc_Properties.mlh_Head;
    while ((next = (struct V8Property *)prop->vp_Node.mln_Succ))
    {
        Remove((struct Node *)&prop->vp_Node);
        if (prop->vp_Name)
            FreeVec(prop->vp_Name);
        if (prop->vp_Value)
            FreeVec(prop->vp_Value);
        FreeVec(prop);
        prop = next;
    }
}
