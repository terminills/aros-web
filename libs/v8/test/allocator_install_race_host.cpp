// Standalone allocator-selection regression; includes the implementation to
// reset its state only between completed worker rounds. Not an engine test.
#include "../v8_allocator.cpp"
#include <pthread.h>
#include <stdio.h>
#include <assert.h>
static pthread_barrier_t phase;
static unsigned custom_allocations, custom_frees;
static BOOL installed;
static const int rounds = 4000;
static void *CustomAlloc(size_t size) { __atomic_add_fetch(&custom_allocations,1,__ATOMIC_RELAXED); return malloc(size); }
static void CustomFree(void *p) { __atomic_add_fetch(&custom_frees,1,__ATOMIC_RELAXED); free(p); }
static void Reset() {
    g_alloc=DefaultAlloc; g_free=DefaultFree;
    __atomic_store_n(&g_allocator_used,FALSE,__ATOMIC_RELEASE);
    __atomic_store_n(&g_allocator_installed,FALSE,__ATOMIC_RELEASE);
    custom_allocations=custom_frees=0; installed=FALSE;
}
static void *AllocateWorker(void *) {
    for(int i=0;i<rounds;++i) {
        pthread_barrier_wait(&phase);
        void *p=V8HeapAllocate(64); assert(p); V8HeapRelease(p);
        pthread_barrier_wait(&phase);
    }
    return nullptr;
}
static void *InstallWorker(void *) {
    for(int i=0;i<rounds;++i) {
        pthread_barrier_wait(&phase);
        installed=V8AllocatorInstallImpl(CustomAlloc,CustomFree);
        pthread_barrier_wait(&phase);
    }
    return nullptr;
}
int main() {
    V8OwnerRangesInitImpl();
    pthread_barrier_init(&phase,nullptr,3);
    pthread_t a,b;
    assert(!pthread_create(&a,nullptr,AllocateWorker,nullptr));
    assert(!pthread_create(&b,nullptr,InstallWorker,nullptr));
    unsigned install_wins=0, allocation_wins=0;
    for(int i=0;i<rounds;++i) {
        Reset();
        pthread_barrier_wait(&phase);
        pthread_barrier_wait(&phase);
        assert(custom_allocations == (installed ? 1u : 0u));
        assert(custom_frees == custom_allocations);
        assert(V8AllocatorIsInstalledImpl()==installed);
        if(installed) {
            ++install_wins;
            assert(V8AllocatorInstallImpl(CustomAlloc,CustomFree));
            assert(!V8AllocatorInstallImpl(DefaultAlloc,DefaultFree));
        } else {
            ++allocation_wins;
            assert(!V8AllocatorInstallImpl(CustomAlloc,CustomFree));
        }
    }
    pthread_join(a,nullptr);pthread_join(b,nullptr);
    Reset();
    __atomic_store_n(&g_allocator_lock_ready,FALSE,__ATOMIC_RELEASE);
    void *p=V8HeapAllocate(8); V8HeapRelease(p);
    assert(!V8AllocatorInstallImpl(CustomAlloc,CustomFree));
    __atomic_store_n(&g_allocator_lock_ready,TRUE,__ATOMIC_RELEASE);
    assert(!V8AllocatorInstallImpl(CustomAlloc,CustomFree));
    assert(install_wins && allocation_wins);
    printf("PASS host race: %d rounds, install wins=%u, allocation wins=%u; matching allocation/free pairs, idempotence, late install rejection, early constructor freeze\n",rounds,install_wins,allocation_wins);
}
