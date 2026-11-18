#include <proto/exec.h>
static inline void ObtainSemaphoreShared(SignalSemaphore *s) { ObtainSemaphore(s); }
#include "../v8_allocator.cpp"
#include <pthread.h>
#include <assert.h>
#include <stdio.h>
#include <sched.h>
static char target[64];
static int callback_entered, callback_release, removal_started, removal_done, frees;
static BOOL owns(void *p) { return p == target; }
static void release(void *p) {
    assert(p == target);
    __atomic_store_n(&callback_entered,1,__ATOMIC_RELEASE);
    while(!__atomic_load_n(&callback_release,__ATOMIC_ACQUIRE)) sched_yield();
    ++frees;
}
static void *free_worker(void *) { V8HeapRelease(target); return nullptr; }
static void *remove_worker(void *) {
    __atomic_store_n(&removal_started,1,__ATOMIC_RELEASE);
    assert(V8OwnerCheckRemoveImpl(owns));
    __atomic_store_n(&removal_done,1,__ATOMIC_RELEASE);
    return nullptr;
}
int main() {
    V8OwnerRangesInitImpl();
    assert(!V8OwnerRangeAddImpl((APTR)(~(IPTR)0-15),32,release));
    for(int n=0;n<500;++n) {
        callback_entered=callback_release=removal_started=removal_done=0;
        assert(V8OwnerCheckAddImpl(owns,release));
        pthread_t a,b;
        assert(!pthread_create(&a,nullptr,free_worker,nullptr));
        while(!__atomic_load_n(&callback_entered,__ATOMIC_ACQUIRE)) sched_yield();
        assert(!pthread_create(&b,nullptr,remove_worker,nullptr));
        while(!__atomic_load_n(&removal_started,__ATOMIC_ACQUIRE)) sched_yield();
        for(int i=0;i<100;++i) sched_yield();
        assert(!__atomic_load_n(&removal_done,__ATOMIC_ACQUIRE));
        __atomic_store_n(&callback_release,1,__ATOMIC_RELEASE);
        pthread_join(a,nullptr);pthread_join(b,nullptr);
        assert(removal_done && g_owner_count==0);
    }
    assert(frees==500);
    puts("PASS host owner retirement: 500 callbacks survive concurrent unregister; slots reused; overflow range rejected");
}
