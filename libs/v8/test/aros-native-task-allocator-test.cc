// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "aros-native-task.h"
#include <exec/memory.h>
#include <exec/semaphores.h>
#include <exec/tasks.h>
#include <dos/dostags.h>
#include <resources/task.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/kernel.h>
#include <proto/task.h>

#ifdef AROS_NATIVE_TASK_TRACE
extern "C" void ArosNativeTrace(const char*) asm("KPutStr");
#define TASK_TRACE(s) ArosNativeTrace("[NativeTask] " s "\n")
#else
#define TASK_TRACE(s) ((void)0)
#endif

struct ArosNativeTask {
  SignalSemaphore gate;
  Task* task;
  ULONG id;
  void (*entry)(void*);
  void* argument;
  Task* waiter;
  ULONG signal;
  bool finished;
};

namespace {
// Existing AROS CRT registry ABI. Native tasks need the same per-task library
// initialization as any other caller; this does not create a POSIX thread.
struct RuntimeHook {
  MinNode node;
  void (*enter)();
  void (*leave)();
};
struct RuntimeHooks {
  SignalSemaphore lock;
  MinList hooks;
};
void TaskEntry() {
  TASK_TRACE("entry");
  auto* control = static_cast<ArosNativeTask*>(FindTask(nullptr)->tc_UserData);
  ObtainSemaphore(&control->gate);
  ReleaseSemaphore(&control->gate);
  void (*enter[8])() = {};
  void (*leave[8])() = {};
  unsigned count = 0;
  auto* hooks = reinterpret_cast<RuntimeHooks*>(
      FindSemaphore(reinterpret_cast<CONST_STRPTR>("crt.thread-hooks.v1")));
  if (hooks) {
    ObtainSemaphoreShared(&hooks->lock);
    for (auto* hook = reinterpret_cast<RuntimeHook*>(hooks->hooks.mlh_Head);
         hook->node.mln_Succ && count < 8;
         hook = reinterpret_cast<RuntimeHook*>(hook->node.mln_Succ)) {
      enter[count] = hook->enter;
      leave[count++] = hook->leave;
    }
    ReleaseSemaphore(&hooks->lock);
  }
  TASK_TRACE("CRT enter");
  for (unsigned i = 0; i < count; ++i) if (enter[i]) enter[i]();
  TASK_TRACE("user entry");
  control->entry(control->argument);
  TASK_TRACE("CRT leave");
  while (count) { --count; if (leave[count]) leave[count](); }
  ObtainSemaphore(&control->gate);
  TASK_TRACE("finished");
  control->finished = true;
  if (control->waiter) Signal(control->waiter, control->signal);
  ReleaseSemaphore(&control->gate);
}
}

ArosNativeTask* ArosNativeTaskStart(void (*entry)(void*), void* argument,
                                  size_t stack_size, const char* name,
                                  int priority) {
  APTR KernelBase = OpenResource(reinterpret_cast<CONST_STRPTR>("kernel.resource"));
  APTR TaskResBase = OpenResource(reinterpret_cast<CONST_STRPTR>("task.resource"));
  if (!entry || !KernelBase || !TaskResBase || stack_size > 0xffffffffu)
    return nullptr;
  auto* control = static_cast<ArosNativeTask*>(
      AllocVec(sizeof(ArosNativeTask), MEMF_PUBLIC | MEMF_CLEAR));
  if (!control) return nullptr;
  InitSemaphore(&control->gate);
  control->entry = entry;
  control->argument = argument;
  APTR mask = KrnAllocCPUMask();
  if (!mask) { FreeVec(control); return nullptr; }
  static unsigned next_cpu = 0;
  const unsigned cpus = KrnGetCPUCount();
  const unsigned slot = __atomic_fetch_add(&next_cpu, 1u, __ATOMIC_RELAXED);
  // Test-only placement: put racing workers on distinct CPUs, including CPU 0.
  KrnGetCPUMask(cpus > 0 ? slot % cpus : 0, mask);
  ObtainSemaphore(&control->gate);
  TASK_TRACE("create");
  auto* process = CreateNewProcTags(
      NP_Entry, reinterpret_cast<IPTR>(TaskEntry),
      NP_UserData, reinterpret_cast<IPTR>(control),
      NP_Name, reinterpret_cast<IPTR>(name),
      NP_StackSize, stack_size ? stack_size : 2 * 1024 * 1024,
      NP_Priority, priority, NP_Affinity, reinterpret_cast<IPTR>(mask), TAG_DONE);
  TASK_TRACE("created");
  if (process) {
    control->task = reinterpret_cast<Task*>(process);
    control->id = control->task->tc_UnionETask.tc_ETask->et_UniqueID;
  }
  ReleaseSemaphore(&control->gate);
  if (!process) { KrnFreeCPUMask(mask); FreeVec(control); return nullptr; }
  return control;
}

void ArosNativeTaskJoin(ArosNativeTask* control) {
  if (!control) return;
  APTR TaskResBase = OpenResource(reinterpret_cast<CONST_STRPTR>("task.resource"));
  TASK_TRACE("join");
  const BYTE bit = AllocSignal(-1);
  for (;;) {
    ObtainSemaphore(&control->gate);
    const bool finished = control->finished;
    if (!finished && bit >= 0) {
      control->waiter = FindTask(nullptr);
      control->signal = 1UL << bit;
    }
    ReleaseSemaphore(&control->gate);
    if (finished) break;
    if (bit >= 0) Wait(1UL << bit); else Delay(1);
  }
  if (bit >= 0) FreeSignal(bit);
  // A completion notification can reach another CPU before TaskEntry returns.
  // Keep the code and control alive until AROS has removed that exact task.
  for (;;) {
    bool alive = false;
    TASK_TRACE("lock task list");
    TaskList* list = LockTaskList(LTF_ALL);
    if (!list) { Delay(1); continue; }
    while (Task* task = NextTaskEntry(list, LTF_ALL)) {
      if (task == control->task && task->tc_UnionETask.tc_ETask->et_UniqueID == control->id) {
        alive = true;
        break;
      }
    }
    UnLockTaskList(list, LTF_ALL);
    if (!alive) break;
    Delay(1);
  }
  TASK_TRACE("joined");
  FreeVec(control);
}
