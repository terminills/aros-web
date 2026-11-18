// Standalone allocator-selection regression; includes the implementation to
// reset its state only between completed worker rounds. Not an engine test.
#include "../v8_allocator.cpp"
#include "aros-native-task.h"
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/kernel.h>
#include <stdio.h>
static const unsigned rounds = 400;
static unsigned allocations, frees;
static BOOL installed;
static Task *controller;
struct Worker { Task *task; ULONG wake; ULONG done; bool installer; unsigned cpu; } workers[2];
static void *CustomAlloc(size_t n) { __atomic_add_fetch(&allocations,1,__ATOMIC_RELAXED); return malloc(n); }
static void CustomFree(void *p) { __atomic_add_fetch(&frees,1,__ATOMIC_RELAXED); free(p); }
static void Reset() {
    g_alloc=DefaultAlloc; g_free=DefaultFree;
    __atomic_store_n(&g_allocator_used,FALSE,__ATOMIC_RELEASE);
    __atomic_store_n(&g_allocator_installed,FALSE,__ATOMIC_RELEASE);
    allocations=frees=0; installed=FALSE;
}
static void RunWorker(void *arg) {
    auto *w=static_cast<Worker*>(arg);
    const BYTE signal=AllocSignal(-1);
    w->task=FindTask(nullptr);
    APTR KernelBase=OpenResource((CONST_STRPTR)"kernel.resource");
    w->cpu=KrnGetCPUNumber();
    w->wake=signal<0 ? 0 : ULONG(1)<<signal;
    Signal(controller,w->done);
    if(signal<0)return;
    for(unsigned i=0;i<rounds;++i) {
        Wait(w->wake);
        if(w->installer && (i&1)) Delay(1);
        if(w->installer) installed=V8AllocatorInstallImpl(CustomAlloc,CustomFree);
        else {void *p=V8HeapAllocate(64);V8HeapRelease(p);}
        Signal(controller,w->done);
    }
    Wait(w->wake);
    FreeSignal(signal);
}
static void WaitBoth(ULONG mask) {
    ULONG observed=0;
    while(observed!=mask)observed|=Wait(mask&~observed);
}
int main() {
    V8OwnerRangesInitImpl();controller=FindTask(nullptr);
    BYTE a_signal=AllocSignal(-1),b_signal=AllocSignal(-1);
    if(a_signal<0||b_signal<0)return 10;
    workers[0].done=ULONG(1)<<a_signal;workers[1].done=ULONG(1)<<b_signal;
    workers[1].installer=true;
    auto *a=ArosNativeTaskStart(RunWorker,&workers[0],262144,"V8 allocator allocation",0);
    auto *b=ArosNativeTaskStart(RunWorker,&workers[1],262144,"V8 allocator installation",0);
    if(!a||!b)return 11;
    ULONG done=workers[0].done|workers[1].done;WaitBoth(done);
    if(!workers[0].wake||!workers[1].wake)return 12;
    unsigned install_wins=0,allocation_wins=0,errors=0;
    for(unsigned i=0;i<rounds;++i) {
        Reset();
        unsigned first=i&1,second=first^1;
        Signal(workers[first].task,workers[first].wake);
        Signal(workers[second].task,workers[second].wake);
        WaitBoth(done);
        if(allocations!=(installed?1u:0u)||frees!=allocations||
           V8AllocatorIsInstalledImpl()!=installed)++errors;
        if(installed) {
            ++install_wins;
            if(!V8AllocatorInstallImpl(CustomAlloc,CustomFree)||
               V8AllocatorInstallImpl(DefaultAlloc,DefaultFree))++errors;
        } else {
            ++allocation_wins;
            if(V8AllocatorInstallImpl(CustomAlloc,CustomFree))++errors;
        }
    }
    Signal(workers[0].task,workers[0].wake);Signal(workers[1].task,workers[1].wake);
    ArosNativeTaskJoin(a);ArosNativeTaskJoin(b);
    FreeSignal(a_signal);FreeSignal(b_signal);
    if(workers[0].cpu==workers[1].cpu || !install_wins || !allocation_wins)++errors;
    printf("Worker CPUs: allocation=%u installation=%u\n",workers[0].cpu,workers[1].cpu);
    printf("Native allocator race: rounds=%u install_wins=%u allocation_wins=%u errors=%u\n",rounds,install_wins,allocation_wins,errors);
    puts(errors ? "FAIL: allocator selection" : "PASS: native Exec allocator selection and matching allocation/free pairs");
    return errors?20:0;
}
