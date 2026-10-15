/*
 * Copyright (C) 2026, The AROS Development Team. All rights reserved.
 */

#ifndef CEF_BLINK_INTERN_H
#define CEF_BLINK_INTERN_H

#include <exec/libraries.h>
#include <exec/semaphores.h>

enum CEFBlinkEngineState
{
    CEFBLINK_ENGINE_UNINITIALIZED,
    CEFBLINK_ENGINE_INITIALIZING,
    CEFBLINK_ENGINE_RUNNING,
    CEFBLINK_ENGINE_SHUTTING_DOWN
};

struct CEFBlinkBase
{
    struct Library cb_Lib;
    struct SignalSemaphore cb_EngineLock;
    enum CEFBlinkEngineState cb_EngineState;
    BOOL cb_EngineWasShutDown;
    struct Task *cb_EngineOwner;
};

#endif /* CEF_BLINK_INTERN_H */
