/*
    Copyright (C) 2025, The AROS Development Team. All rights reserved.

    Desc: V8 JavaScript Engine Library - Internal Definitions
*/

#ifndef V8_INTERN_H
#define V8_INTERN_H

#ifndef EXEC_LIBRARIES_H
#include <exec/libraries.h>
#endif

#ifndef EXEC_SEMAPHORES_H
#include <exec/semaphores.h>
#endif

#ifndef EXEC_LISTS_H
#include <exec/lists.h>
#endif

#ifndef DOS_DOS_H
#include <dos/dos.h>
#endif

#ifndef LIBRARIES_V8_H
#include <libraries/v8.h>
#endif

/****************************************************************************/

/* V8_INTERNAL_STRUCTURES
 * AROS_IMPL: Uses exec.library structures and conventions
 * DESIGN: Track isolates, contexts, and compiled scripts
 * MEMORY: All allocations through exec.library
 * THREAD_SAFETY: Semaphore protection for library base and isolate lists
 */

/****************************************************************************/

/* Library Base Structure */
struct V8Base
{
    struct Library          v8_Lib;         /* Standard library node */
    BPTR                    v8_SegList;     /* Segment list for library */
    
    struct SignalSemaphore  v8_Semaphore;   /* Protection for library data */
    struct MinList          v8_Isolates;    /* List of active isolates */
    
    BOOL                    v8_Initialized; /* V8 platform initialized */
    APTR                    v8_Platform;    /* V8 platform pointer (opaque) */
    APTR                    v8_ArrayBuffer; /* Array buffer allocator */
};

/****************************************************************************/

/* Internal Isolate Structure */
struct V8Isolate
{
    struct MinNode          vi_Node;        /* Node for isolate list */
    APTR                    vi_Isolate;     /* V8 Isolate pointer (opaque) */
    struct MinList          vi_Contexts;    /* List of contexts in this isolate */
    struct MinList          vi_Scripts;     /* List of compiled scripts */
    struct SignalSemaphore  vi_Semaphore;   /* Protection for isolate data */
    ULONG                   vi_Flags;       /* Isolate flags */
};

/****************************************************************************/

/* Property Entry for simple property storage */
struct V8Property
{
    struct MinNode          vp_Node;        /* Node for property list */
    STRPTR                  vp_Name;        /* Property name */
    STRPTR                  vp_Value;       /* Property value (as string) */
};

/* Internal Context Structure */
struct V8Context
{
    struct MinNode          vc_Node;        /* Node for context list */
    APTR                    vc_Context;     /* V8 Context pointer (opaque) */
    struct V8Isolate       *vc_Isolate;     /* Parent isolate */
    struct MinList          vc_Properties;  /* Simple property storage */
    APTR                    vc_GlobalObject;/* Global object handle */
};

/****************************************************************************/

/* Internal Script Structure */
struct V8Script
{
    struct MinNode          vs_Node;        /* Node for script list */
    APTR                    vs_Script;      /* V8 Script pointer (opaque) */
    struct V8Isolate       *vs_Isolate;     /* Parent isolate */
    STRPTR                  vs_Name;        /* Script name/identifier */
    ULONG                   vs_Flags;       /* Script flags */
};

/****************************************************************************/

/* Internal Function Prototypes */

/* Isolate management */
struct V8Isolate *V8_AllocIsolate(struct V8Base *V8Base, ULONG flags);
void V8_FreeIsolate(struct V8Base *V8Base, struct V8Isolate *isolate);

/* Context management */
struct V8Context *V8_AllocContext(struct V8Isolate *isolate);
void V8_FreeContext(struct V8Context *context);

/* Script management */
struct V8Script *V8_AllocScript(struct V8Isolate *isolate, CONST_STRPTR name, ULONG flags);
void V8_FreeScript(struct V8Script *script);

/* Property management */
BOOL V8_SetPropertyValue(struct V8Context *context, CONST_STRPTR name, CONST_STRPTR value);
CONST_STRPTR V8_GetPropertyValue(struct V8Context *context, CONST_STRPTR name);
void V8_FreeProperties(struct V8Context *context);

/* Simple JavaScript evaluator (for basic expressions until V8 is ported) */
LONG V8_EvaluateSimpleExpression(CONST_STRPTR expr, struct V8Context *context, 
                                  STRPTR result, ULONG resultSize);

/* V8 wrapper functions - these will wrap actual V8 C++ API calls */
BOOL V8_InitializePlatform(struct V8Base *V8Base);
void V8_ShutdownPlatform(struct V8Base *V8Base);

/****************************************************************************/

/* Convenience Macros */
#define V8B(lb)  ((struct V8Base *)lb)

/****************************************************************************/

#endif /* V8_INTERN_H */
