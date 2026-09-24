/*
    Copyright (C) 2025, The AROS Development Team. All rights reserved.

    Desc: V8 Library C++ Bridge - Header for C code to call C++ V8 Platform
*/

#ifndef V8_BRIDGE_H
#define V8_BRIDGE_H

#ifndef EXEC_TYPES_H
#include <exec/types.h>
#endif

/* V8_CPP_BRIDGE_INTERFACE
 * AROS_IMPL: Pure C header for bridge functions
 * DESIGN: Allows C library code to call C++ V8 platform
 */

#ifdef __cplusplus
extern "C" {
#endif

/****************************************************************************/
/* Platform Initialization Functions                                        */
/****************************************************************************/

/* Initialize V8 platform */
BOOL V8Bridge_InitializePlatform(void);

/* Shutdown V8 platform */
void V8Bridge_ShutdownPlatform(void);

/****************************************************************************/
/* Isolate Management Functions                                            */
/****************************************************************************/

/* Create V8 isolate */
APTR V8Bridge_CreateIsolate(void);

/* Destroy V8 isolate */
void V8Bridge_DestroyIsolate(APTR isolate);

/****************************************************************************/
/* Context Management Functions                                            */
/****************************************************************************/

/* Create V8 context */
APTR V8Bridge_CreateContext(APTR isolate);

/* Destroy V8 context */
void V8Bridge_DestroyContext(APTR context);

/****************************************************************************/
/* JavaScript Execution Functions                                           */
/****************************************************************************/

/* Execute JavaScript code */
LONG V8Bridge_ExecuteScript(APTR isolate, APTR context, 
                           CONST_STRPTR script, CONST_STRPTR name,
                           STRPTR result, ULONG resultSize);

/* Compile JavaScript script */
APTR V8Bridge_CompileScript(APTR isolate, APTR context,
                           CONST_STRPTR source, CONST_STRPTR name);

/* Run compiled script */
LONG V8Bridge_RunScript(APTR isolate, APTR context, APTR script,
                       STRPTR result, ULONG resultSize);

/* Free compiled script */
void V8Bridge_FreeScript(APTR script);

/****************************************************************************/
/* Property Management Functions                                            */
/****************************************************************************/

/* Set object property */
LONG V8Bridge_SetProperty(APTR isolate, APTR context, APTR object,
                         CONST_STRPTR name, CONST_STRPTR value);

/* Get object property */
LONG V8Bridge_GetProperty(APTR isolate, APTR context, APTR object,
                         CONST_STRPTR name, STRPTR result, ULONG resultSize);

/* Get global object */
APTR V8Bridge_GetGlobalObject(APTR isolate, APTR context);

/* Call JavaScript function */
LONG V8Bridge_CallFunction(APTR isolate, APTR context, APTR object,
                          CONST_STRPTR name, STRPTR result, ULONG resultSize);

#ifdef __cplusplus
}
#endif

#endif /* V8_BRIDGE_H */
