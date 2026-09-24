/*
 * Node-side compatibility for Abseil facilities that are compiled out when
 * ABSL_HAVE_MMAP is false.
 *
 * AROS has a native allocator but no mmap API. Node's Abseil Mutex still
 * references LowLevelAlloc, its deadlock graph, thread identities and the
 * per-thread semaphore, and Node's tools/v8_gypfiles Abseil build leaves all of
 * those as empty units.  This single translation unit supplies the whole
 * surface (malloc-backed arena, no-op cycle diagnostics, pthread-backed
 * semaphore) and is the only member of libnode-absl-compat.a; it is compiled
 * with -DABSL_HAVE_MMAP so the headers declare the classes it defines.  This
 * remains Node adapter code; it is not part of pristine v8.library.
 */

#include "absl/base/internal/low_level_alloc.h"
#include "absl/base/internal/thread_identity.h"
#include "absl/synchronization/internal/graphcycles.h"
#include "absl/synchronization/internal/per_thread_sem.h"
#include "absl/synchronization/internal/waiter_base.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <new>
#include <pthread.h>

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace base_internal {

struct LowLevelAlloc::Arena {
  unsigned int flags;
};

void* LowLevelAlloc::Alloc(size_t size) {
  return size == 0 ? nullptr : std::malloc(size);
}

void* LowLevelAlloc::AllocWithArena(size_t size, Arena* arena) {
  (void)arena;
  return Alloc(size);
}

void LowLevelAlloc::Free(void* storage) {
  std::free(storage);
}

LowLevelAlloc::Arena* LowLevelAlloc::NewArena(uint32_t flags) {
  Arena* arena = static_cast<Arena*>(std::malloc(sizeof(*arena)));
  if (arena != nullptr)
    arena->flags = flags;
  return arena;
}

bool LowLevelAlloc::DeleteArena(Arena* arena) {
  if (arena == DefaultArena())
    return false;
  std::free(arena);
  return true;
}

LowLevelAlloc::Arena* LowLevelAlloc::DefaultArena() {
  static Arena arena = {0};
  return &arena;
}

}  // namespace base_internal
ABSL_NAMESPACE_END
}  // namespace absl

namespace {

/*
 * AROS pthread mutexes and condition variables contain native Exec
 * SignalSemaphores. Together they make Abseil's inline PthreadWaiter 296
 * bytes, while ThreadIdentity reserves a stable 256-byte WaiterState ABI.
 * Keep only a pointer in that slot and place the native synchronization
 * objects out of line.
 */
struct ArosPerThreadSemState {
  pthread_mutex_t mutex;
  pthread_cond_t condition;
  unsigned int wakeups;
  bool poked;
};

ArosPerThreadSemState* GetSemState(
    absl::base_internal::ThreadIdentity* identity) {
  ArosPerThreadSemState* state;
  std::memcpy(&state, identity->waiter_state.data, sizeof(state));
  return state;
}

void SetSemState(absl::base_internal::ThreadIdentity* identity,
                 ArosPerThreadSemState* state) {
  std::memcpy(identity->waiter_state.data, &state, sizeof(state));
}

void ResetThreadIdentity(absl::base_internal::ThreadIdentity* identity) {
  absl::base_internal::PerThreadSynch* synch = &identity->per_thread_synch;
  synch->next = nullptr;
  synch->skip = nullptr;
  synch->may_skip = false;
  synch->wake = false;
  synch->cond_waiter = false;
  synch->maybe_unlocking = false;
  synch->suppress_fatal_errors = false;
  synch->priority = 0;
  synch->state.store(absl::base_internal::PerThreadSynch::State::kAvailable,
                     std::memory_order_relaxed);
  synch->waitp = nullptr;
  synch->readers = 0;
  synch->next_priority_read_cycles = 0;
  synch->all_locks = nullptr;
  identity->blocked_count_ptr = nullptr;
  identity->ticker.store(0, std::memory_order_relaxed);
  identity->wait_start.store(0, std::memory_order_relaxed);
  identity->is_idle.store(false, std::memory_order_relaxed);
  identity->next = nullptr;
}

void ReclaimThreadIdentity(void*) {
  /*
   * ThreadIdentity storage intentionally persists for process lifetime, as it
   * does upstream. Clear the TLS association so a later destructor cannot
   * observe the terminating thread's identity.
   */
  absl::base_internal::ClearCurrentThreadIdentity();
}

}  // namespace

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace synchronization_internal {

GraphCycles::GraphCycles() : rep_(nullptr) {}
GraphCycles::~GraphCycles() = default;

GraphId GraphCycles::GetId(void* ptr) {
  uintptr_t handle = reinterpret_cast<uintptr_t>(ptr);
  return GraphId{handle == 0 ? 1 : static_cast<uint64_t>(handle)};
}

void GraphCycles::RemoveNode(void*) {}

void* GraphCycles::Ptr(GraphId id) {
  return id.handle <= 1 ? nullptr
                        : reinterpret_cast<void*>(
                              static_cast<uintptr_t>(id.handle));
}

bool GraphCycles::InsertEdge(GraphId, GraphId) {
  /*
   * The graph is diagnostic deadlock detection, not mutex correctness.
   * Accepting the edge explicitly disables that diagnostic on AROS.
   */
  return true;
}

void GraphCycles::RemoveEdge(GraphId, GraphId) {}
bool GraphCycles::HasNode(GraphId) { return false; }
bool GraphCycles::HasEdge(GraphId, GraphId) const { return false; }
bool GraphCycles::IsReachable(GraphId, GraphId) const { return false; }
int GraphCycles::FindPath(GraphId, GraphId, int, GraphId[]) const { return 0; }
void GraphCycles::UpdateStackTrace(GraphId, int, int (*)(void**, int)) {}
int GraphCycles::GetStackTrace(GraphId, void*** ptr) {
  if (ptr != nullptr)
    *ptr = nullptr;
  return 0;
}
bool GraphCycles::CheckInvariants() const { return true; }

base_internal::ThreadIdentity* CreateThreadIdentity() {
  const size_t alignment = base_internal::PerThreadSynch::kAlignment;
  void* allocation = base_internal::LowLevelAlloc::Alloc(
      sizeof(base_internal::ThreadIdentity) + alignment - 1);
  if (allocation == nullptr)
    return nullptr;

  uintptr_t address = reinterpret_cast<uintptr_t>(allocation);
  address = (address + alignment - 1) & ~(alignment - 1);
  base_internal::ThreadIdentity* identity =
      reinterpret_cast<base_internal::ThreadIdentity*>(address);
  new (identity) base_internal::ThreadIdentity;
  std::memset(identity->waiter_state.data, 0,
              sizeof(identity->waiter_state.data));
  ResetThreadIdentity(identity);
  AbslInternalPerThreadSemInit(identity);
  base_internal::SetCurrentThreadIdentity(identity, ReclaimThreadIdentity);
  return identity;
}

void PerThreadSem::SetThreadBlockedCounter(std::atomic<int>* counter) {
  base_internal::ThreadIdentity* identity =
      GetOrCreateCurrentThreadIdentity();
  identity->blocked_count_ptr = counter;
}

std::atomic<int>* PerThreadSem::GetThreadBlockedCounter() {
  base_internal::ThreadIdentity* identity =
      GetOrCreateCurrentThreadIdentity();
  return identity->blocked_count_ptr;
}

void PerThreadSem::Tick(base_internal::ThreadIdentity* identity) {
  const int ticker =
      identity->ticker.fetch_add(1, std::memory_order_relaxed) + 1;
  const int wait_start =
      identity->wait_start.load(std::memory_order_relaxed);
  if (wait_start != 0 &&
      ticker - wait_start > WaiterBase::kIdlePeriods &&
      !identity->is_idle.load(std::memory_order_relaxed)) {
    AbslInternalPerThreadSemPoke(identity);
  }
}

}  // namespace synchronization_internal
ABSL_NAMESPACE_END
}  // namespace absl

extern "C" {

void AbslInternalPerThreadSemInit(
    absl::base_internal::ThreadIdentity* identity) {
  ArosPerThreadSemState* state = static_cast<ArosPerThreadSemState*>(
      std::malloc(sizeof(ArosPerThreadSemState)));
  if (state == nullptr)
    __builtin_trap();
  if (pthread_mutex_init(&state->mutex, nullptr) != 0 ||
      pthread_cond_init(&state->condition, nullptr) != 0) {
    __builtin_trap();
  }
  state->wakeups = 0;
  state->poked = false;
  SetSemState(identity, state);
}

void AbslInternalPerThreadSemPost(
    absl::base_internal::ThreadIdentity* identity) {
  ArosPerThreadSemState* state = GetSemState(identity);
  pthread_mutex_lock(&state->mutex);
  ++state->wakeups;
  pthread_cond_signal(&state->condition);
  pthread_mutex_unlock(&state->mutex);
}

void AbslInternalPerThreadSemPoke(
    absl::base_internal::ThreadIdentity* identity) {
  ArosPerThreadSemState* state = GetSemState(identity);
  pthread_mutex_lock(&state->mutex);
  state->poked = true;
  pthread_cond_signal(&state->condition);
  pthread_mutex_unlock(&state->mutex);
}

bool AbslInternalPerThreadSemWait(
    absl::synchronization_internal::KernelTimeout timeout) {
  using absl::base_internal::ThreadIdentity;
  using absl::synchronization_internal::GetOrCreateCurrentThreadIdentity;

  ThreadIdentity* identity = GetOrCreateCurrentThreadIdentity();
  ArosPerThreadSemState* state = GetSemState(identity);
  int ticker = identity->ticker.load(std::memory_order_relaxed);
  identity->wait_start.store(ticker == 0 ? 1 : ticker,
                             std::memory_order_relaxed);
  identity->is_idle.store(false, std::memory_order_relaxed);
  if (identity->blocked_count_ptr != nullptr)
    identity->blocked_count_ptr->fetch_add(1, std::memory_order_relaxed);

  bool acquired = false;
  pthread_mutex_lock(&state->mutex);
  while (state->wakeups == 0) {
    int result;
    if (timeout.has_timeout()) {
      struct timespec deadline = timeout.MakeAbsTimespec();
      result = pthread_cond_timedwait(&state->condition, &state->mutex,
                                      &deadline);
    } else {
      result = pthread_cond_wait(&state->condition, &state->mutex);
    }
    if (state->poked) {
      state->poked = false;
      identity->is_idle.store(true, std::memory_order_relaxed);
      continue;
    }
    if (result == ETIMEDOUT)
      break;
  }
  if (state->wakeups != 0) {
    --state->wakeups;
    acquired = true;
  }
  pthread_mutex_unlock(&state->mutex);

  if (identity->blocked_count_ptr != nullptr)
    identity->blocked_count_ptr->fetch_sub(1, std::memory_order_relaxed);
  identity->is_idle.store(false, std::memory_order_relaxed);
  identity->wait_start.store(0, std::memory_order_relaxed);
  return acquired;
}

}  // extern "C"

/*
 * Chromium V8's V8_Fatal gained file/line parameters after Node 22's embedded
 * V8 snapshot. The old entry is only a last-ditch invariant failure path.
 */
extern "C" void NodeV8LegacyFatal(const char* format, ...)
    __asm__("_Z8V8_FatalPKcz");

extern "C" void NodeV8LegacyFatal(const char* format, ...) {
  (void)format;
  __builtin_trap();
}

/*
 * Node's C++ credential source includes the generated usergroup prototype
 * without C linkage. Keep that historical header defect out of the public
 * library ABI by bridging its one mangled base getter back to the native
 * linklib entry.
 */
extern "C" void* __aros_getbase_UserGroupBase(void);
extern "C" void* NodeV8UserGroupBase(void)
    __asm__("_Z28__aros_getbase_UserGroupBasev");

extern "C" void* NodeV8UserGroupBase(void) {
  return __aros_getbase_UserGroupBase();
}
