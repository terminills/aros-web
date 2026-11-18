#include "../v8_allocator.cpp"
#include "aros-native-task.h"
#include <proto/dos.h>
#include <proto/kernel.h>
#include <stdio.h>
static constexpr unsigned rounds=200;
static char target[64];
static Task *controller;
static ULONG entered_mask, started_mask;
static unsigned removals, frees;
struct Worker { Task *task; ULONG wake, done; unsigned cpu; bool remove; } workers[2];
static BOOL owns(void *p) { return p==target; }
static void release(void *p) {
    if(p!=target) return;
    Signal(controller,entered_mask);
    Wait(workers[0].wake);
    __atomic_add_fetch(&frees,1,__ATOMIC_RELEASE);
}
static void worker(void *arg) {
    auto *w=static_cast<Worker*>(arg);
    BYTE sig=AllocSignal(-1);
    w->task=FindTask(nullptr);w->wake=sig<0?0:ULONG(1)<<sig;
    APTR KernelBase=OpenResource((CONST_STRPTR)"kernel.resource");
    w->cpu=KrnGetCPUNumber();
    Signal(controller,w->done);
    if(sig<0)return;
    for(unsigned n=0;n<rounds;++n) {
        Wait(w->wake);
        if(w->remove) {
            Signal(controller,started_mask);
            if(V8OwnerCheckRemoveImpl(owns))
                __atomic_add_fetch(&removals,1,__ATOMIC_RELEASE);
        } else V8HeapRelease(target);
        Signal(controller,w->done);
    }
    Wait(w->wake);FreeSignal(sig);
}
static void wait_all(ULONG mask) {
    ULONG seen=0;while(seen!=mask)seen|=Wait(mask&~seen);
}
int main() {
    V8OwnerRangesInitImpl();controller=FindTask(nullptr);
    BYTE signals[4];ULONG masks[4];
    for(unsigned i=0;i<4;++i) {signals[i]=AllocSignal(-1);if(signals[i]<0)return 10;masks[i]=ULONG(1)<<signals[i];}
    workers[0].done=masks[0];workers[1].done=masks[1];workers[1].remove=true;
    entered_mask=masks[2];started_mask=masks[3];
    auto *a=ArosNativeTaskStart(worker,&workers[0],262144,"Owner free worker",0);
    auto *b=ArosNativeTaskStart(worker,&workers[1],262144,"Owner retire worker",0);
    if(!a||!b)return 11;
    ULONG done=masks[0]|masks[1];wait_all(done);
    if(!workers[0].wake||!workers[1].wake)return 12;
    unsigned errors=V8OwnerRangeAddImpl((APTR)(~(IPTR)0-15),32,release)?1:0;
    for(unsigned n=0;n<rounds;++n) {
        if(!V8OwnerCheckAddImpl(owns,release))return 13;
        Signal(workers[0].task,workers[0].wake);Wait(entered_mask);
        Signal(workers[1].task,workers[1].wake);Wait(started_mask);
        Delay(1);
        if(__atomic_load_n(&removals,__ATOMIC_ACQUIRE)!=n)++errors;
        Signal(workers[0].task,workers[0].wake);wait_all(done);
        if(removals!=n+1||frees!=n+1||g_owner_count!=0)++errors;
    }
    Signal(workers[0].task,workers[0].wake);Signal(workers[1].task,workers[1].wake);
    ArosNativeTaskJoin(a);ArosNativeTaskJoin(b);
    if(workers[0].cpu==workers[1].cpu)++errors;
    printf("Owner retirement: rounds=%u frees=%u removals=%u errors=%u CPUs=%u,%u\n",rounds,frees,removals,errors,workers[0].cpu,workers[1].cpu);
    puts(errors?"FAIL owner retirement":"PASS native Exec owner retirement, callback lifetime and slot reuse");
    for(unsigned i=0;i<4;++i)FreeSignal(signals[i]);
    return errors?20:0;
}
