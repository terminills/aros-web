/*
    Copyright (C) 2026, The AROS Development Team. All rights reserved.

    Desc: node.library bridge - Node 22 embedder entry points and library
          lifecycle glue
*/

#include <aros/libcall.h>
#include <aros/symbolsets.h>
#include <clib/exec_protos.h>
#include <exec/libraries.h>
#include <libraries/node.h>
#include <proto/node-v8.h>
#include <proto/v8.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <pthread.h>

#include <node.h>
#include <node_version.h>
#include <uv.h>

/* genmodule's libdefs are plain C: they declare the per-task base lookup
   (__aros_getoffsettable) that node_start.c defines with C linkage. */
extern "C" {
#include "node_libdefs.h"
}

/*
 * Diagnostics.
 *
 * kprintf() lives in the kernel's debug output path and pulls in
 * _arosdebuglock, which no linklib in a non-debug tree defines - node.library
 * then fails to link with a single undefined symbol after everything else has
 * built. Route the bridge's diagnostics through NodeBridgeLog() instead, which
 * is a no-op unless the library is built with -DNODE_AROS_BRIDGE_DEBUG.
 *
 * These are error and lifecycle messages rather than hot-path logging, so
 * turning them on costs nothing but a debug-capable tree.
 */
#ifdef NODE_AROS_BRIDGE_DEBUG
extern "C" int kprintf(const char *format, ...);
#define NodeBridgeLog(...) kprintf(__VA_ARGS__)
#else
#define NodeBridgeLog(...) ((void)0)
#endif
extern "C" int getentropy(void *buffer, size_t length);

/*
 * OpenSSL probes getentropy through a weak reference.  A weak-only undefined
 * symbol does not pull the AROS PosixC linklib wrapper from its archive, while
 * the ELF loader still has to relocate the call site.  Keep one strong
 * reference so bundled OpenSSL resolves to the shared PosixC implementation.
 */
static auto *const NodeGetEntropyAnchor __attribute__((used)) = &getentropy;

/*
 * Expunge runs on whichever task asked exec for memory when lddemon flushed
 * the library list - after C:Node has exited that is typically a Chromium
 * thread.  node's static destructors (node::Mutex and friends) call into
 * uv1.library, a pertaskbase library whose internal base lookups go through
 * the *current* task's storage slot, not the base the caller jumped through.
 * A task that never opened uv1 (and whose parent did not either) has no slot,
 * so uv_mutex_destroy() faulted in uv1's own memset relwrapper with
 * CR2 = the StdCBase offset inside the missing base.
 *
 * The expunge cannot give itself a base either: lddemon's LDFlush holds
 * exec's LibList WRITE lock across RemLibrary()/Expunge, and
 * OpenLibrary("uv1.library") takes the same lock in READ mode - on SMP the
 * expunging task then spins forever in KrnSpinLock on its own lock (the
 * pc-x86_64 guest froze that way on the first Chromium launch after C:Node
 * had run, 2026-09-14: CPU0 in KrnSpinLock, s_Owner == the Chromium
 * process, TDNestCnt 2).  Anything Expunge does must hold no exec list
 * lock and must not load.
 *
 * Worse than the base: the expunge SLEEPS.  The pthread emulation linked
 * into node.library registers __pthread_Exit_Func in the EXIT set, which
 * reaps every thread it still knows about with Delay(1)/pthread_join - a
 * Wait() under LDFlush's LibList write lock.  The next OpenLibrary on any
 * task then spins in LDRequestObject's ObtainSystemLock(LibList, READ,
 * LOCKF_FORBID); with the sleeper READY but pinned to the same CPU as the
 * spinner (both Chromium threads on CPU0), it never runs again and the
 * whole guest stops taking input (rtest6, 2026-09-14, logs/rtest6-
 * deadlock.md).  Checking "does this task have a uv1 base" was not enough:
 * the flushing renderer thread DID have node's uv1 dup base in its slot.
 *
 * So the expunge is only allowed from node.library's own last closer.
 * node.library is a pertaskbase module (node.conf): genmodule's CloseLib
 * runs the CLOSELIB set on a task's last close of its dup base, before it
 * drops the root open count and, when that reaches 0 with LIBF_DELEXP set,
 * expunges from that same task.  The hook is handed the dup base, whose
 * counters say nothing about the root, so it records every last-per-task
 * closer's exec unique ID (task pointers are recycled; C:Node's was already
 * a Chromium thread by the time of the flush) - the latest record is the
 * task that is about to expunge, exactly as uv1's hook does.  The
 * EXPUNGELIB hook passes only for that task, and lddemon's CloseLibrary
 * path holds no list lock (Forbid only), so the sleep in
 * __pthread_Exit_Func is harmless there.  Any other caller - LDFlush on
 * whatever task's AllocMem failed - is vetoed: LIBF_DELEXP stays set and
 * the library stays resident until a node process closes it, which is the
 * only outcome that is safe from a foreign task under the list lock.
 */
extern "C" int uv_aros_task_has_base(void);

static ULONG NodeCloserID;

static int NodeCloseLast(LIBBASETYPEPTR lh)
{
  (void)lh;
  NodeCloserID = GetETaskID(FindTask(NULL));
  return 1;
}

static int NodeExpungePrepare(LIBBASETYPEPTR lh)
{
  (void)lh;
  ULONG closer = NodeCloserID;
  NodeCloserID = 0;
  return closer != 0 && closer == GetETaskID(FindTask(NULL)) &&
         uv_aros_task_has_base();
}

ADD2CLOSELIB(NodeCloseLast, 0)
ADD2EXPUNGELIB(NodeExpungePrepare, 0)

/*
 * Per-task base binding for the entry points below.
 *
 * Everything node.library calls in its C runtime and in libuv goes through
 * the rellib bases stored in the dup base that genmodule keeps in the
 * CALLING task's storage slot (posixc, stdc, uv1, z1 in node.conf), so a
 * Node process gets its own fd table, environment and uv default loop, not
 * those of whichever task loaded the library first.  OpenLibrary() fills
 * that slot for the opener, and a task that never opened node.library
 * borrows its creator's slot (__aros_getoffsettable, one et_Parent level).
 * An entry point is regcall, so unlike uv1's stackcall entries nothing sets
 * the slot on the way in: an embedder thread two creations away from the
 * opener (an ElectronShell renderer helper) would reach the first
 * relwrapper with no base and fault at CR2 = the rellib offset.  Such a
 * caller still jumped in through its process's own dup base, so bind that.
 */
extern "C" void __aros_setoffsettable(void *base);

/*
 * Callbacks another library makes on a thread that is none of ours.
 *
 * The wrappers below carry the base down every thread NODE creates, but a
 * global callback node registers with V8 is also called on threads Chromium
 * creates.  V8::SetEntropySource() is one: Blink starts a service worker,
 * that thread builds its own Isolate, Heap::SetUpSpaces constructs a
 * v8::base::RandomNumberGenerator, and the entropy source runs node's
 * OpenSSL CSPRNG - inside node.library, on a task that never opened it and
 * whose creator did not either.  Its first rel-wrapper then read a NULL base
 * and faulted dereferencing the rellib offset (desktop VS Code gate 11,
 * 2026-09-15: __strcmp_StdCBase_relwrapper, RAX = 0xF0, under
 * ossl_drbg_get_ctx_params_no_lock <- ncrypto::CSPRNG <- v8.library
 * RandomNumberGenerator <- Isolate::Init on a "ServiceWorker thread").
 *
 * Such a thread has no business owning a Node runtime, so it borrows the
 * base of the Node that installed the callback - remembered here while a
 * valid base is in hand.  The call it makes is a leaf into OpenSSL, which
 * keeps its state in the library rather than in the base, so borrowing is
 * enough to make it work and is never a thread's permanent identity: a task
 * that later opens node.library for real gets its own base as usual.
 *
 * The base is not reference-counted here; it stays valid because the process
 * that started Node keeps node.library open for its whole life, which is
 * also as long as any Blink thread of that process lives.
 */
static void *NodeCallbackTaskBase;

extern "C" void __node_aros_bind_callback_task_base(void)
{
  if (__aros_getoffsettable() == nullptr && NodeCallbackTaskBase != nullptr)
    __aros_setoffsettable(NodeCallbackTaskBase);
}

/*
 * The same lend, reached from genmodule's per-task base lookup instead of
 * from a call site.
 *
 * The explicit calls above cover the paths node knows about (its own thread
 * wrappers, V8's entropy source, threadpool work).  A Blink thread can enter
 * node.library through paths node never sees - a "ServiceWorker thread"
 * faulted in __malloc_StdCBase_relwrapper+0x1D with its own slot AND its
 * creator's slot empty (alpha9, VS Code + GitHub Desktop, 2026-09-17, the
 * fault that surfaced once uv1.library stopped faulting first).  Answering
 * the lookup's last resort covers every such path at once, with the same
 * reasoning that justifies the call sites: a thread with no base of its own
 * is not running a Node runtime, and what it can reach is the stateless end
 * of the C runtime, for which any valid base answers identically.
 */
extern "C" char *__aros_borrow_offsettable(void)
{
  return (char *)NodeCallbackTaskBase;
}

static inline void NodeBindTaskBase(LIBBASETYPEPTR lh)
{
  if (__aros_getoffsettable() == nullptr)
    __aros_setoffsettable(lh);
  /* Every entry point passes through here, so this is where the base a
     foreign-thread callback borrows is kept up to date - NodeStart() and the
     embedder path (ElectronShell) alike. */
  NodeCallbackTaskBase = __aros_getoffsettable();
}

/*
 * Threads node starts itself get the base handed down at creation.
 *
 * The one-level borrow above covers a thread the opener creates, and no
 * more.  A worker_threads Worker (VS Code's extension host) is such a
 * thread; the CA-certificate loader that node's crypto init starts FROM the
 * Worker is two creations from the opener: its own slot is empty, its
 * creator's slot is empty, and its first relwrapper faulted at CR2 = 0xF0
 * (the StdCBase offset) in __malloc_StdCBase_relwrapper under
 * LoadCACertificates (desktop VS Code run 25, 2026-09-15).  No entry point
 * of node.library runs on such a thread, so NodeBindTaskBase() never sees
 * it.
 *
 * include-aros/aros-node-compat.h renames uv_thread_create(),
 * uv_thread_create_ex() and pthread_create() in node's own units to the
 * wrappers below.  Each wrapper records the CREATING thread's effective base
 * (own slot or the borrowed one) and the new thread binds it before running
 * the entry, so the base follows every chain of creations however deep.
 * The start block is exec memory: it is freed on the new thread before its
 * slot is bound, where a stdc relwrapper would have no base to free through.
 */
struct NodeThreadStart {
  void (*uv_entry)(void *);
  void *(*pthread_entry)(void *);
  void *arg;
  void *base;
};

static NodeThreadStart *NodeThreadStartCreate(void *arg)
{
  auto *start = static_cast<NodeThreadStart *>(
      AllocVec(sizeof(NodeThreadStart), MEMF_ANY | MEMF_CLEAR));
  if (start == nullptr)
    return nullptr;
  start->arg = arg;
  start->base = __aros_getoffsettable();
  return start;
}

static NodeThreadStart NodeThreadStartBind(void *raw)
{
  auto *start = static_cast<NodeThreadStart *>(raw);
  NodeThreadStart local = *start;
  FreeVec(start);
  if (local.base != nullptr && __aros_getoffsettable() == nullptr)
    __aros_setoffsettable(local.base);
  return local;
}

static void NodeUvThreadEntry(void *raw)
{
  NodeThreadStart local = NodeThreadStartBind(raw);
  local.uv_entry(local.arg);
}

static void *NodePthreadEntry(void *raw)
{
  NodeThreadStart local = NodeThreadStartBind(raw);
  return local.pthread_entry(local.arg);
}

/* uv1.conf does not export the thread creators; node reached them through
   the lazy thunks the uv1 ABI generator emits for what libnode.a references.
   This unit is not a generator input, so resolve the one it needs itself,
   the way those thunks do. */
extern "C" void *uv_aros_find_internal_symbol(const char *symbol_name);

using NodeUvThreadCreateExFn = int (*)(uv_thread_t *,
                                       const uv_thread_options_t *,
                                       uv_thread_cb, void *);
static NodeUvThreadCreateExFn NodeUvThreadCreateEx;

extern "C" int aros_node_uv_thread_create_ex(uv_thread_t *tid,
                                             const uv_thread_options_t *params,
                                             uv_thread_cb entry, void *arg)
{
  if (NodeUvThreadCreateEx == nullptr) {
    NodeUvThreadCreateEx = reinterpret_cast<NodeUvThreadCreateExFn>(
        uv_aros_find_internal_symbol("uv_thread_create_ex"));
    if (NodeUvThreadCreateEx == nullptr)
      return UV_ENOSYS;
  }
  NodeThreadStart *start = NodeThreadStartCreate(arg);
  if (start == nullptr)
    return UV_ENOMEM;
  start->uv_entry = entry;
  int r = NodeUvThreadCreateEx(tid, params, NodeUvThreadEntry, start);
  if (r != 0)
    FreeVec(start);
  return r;
}

extern "C" int aros_node_uv_thread_create(uv_thread_t *tid, uv_thread_cb entry,
                                          void *arg)
{
  uv_thread_options_t params;
  params.flags = UV_THREAD_NO_FLAGS;
  params.stack_size = 0;
  return aros_node_uv_thread_create_ex(tid, &params, entry, arg);
}

extern "C" int aros_node_pthread_create(pthread_t *thread,
                                        const pthread_attr_t *attr,
                                        void *(*entry)(void *), void *arg)
{
  NodeThreadStart *start = NodeThreadStartCreate(arg);
  if (start == nullptr)
    return EAGAIN;
  start->pthread_entry = entry;
  int r = pthread_create(thread, attr, NodePthreadEntry, start);
  if (r != 0)
    FreeVec(start);
  return r;
}

/*
 * One Node runtime PER APP, not per library.
 *
 * node.library is a single library shared by every AROS process, so these
 * used to be plain file statics: one loop, one isolate, one Environment for
 * the whole machine. That is invisible while one Electron app runs and fatal
 * the moment a second one starts - it finds an Environment already there and
 * every attach path refuses (the second app died at "prepare Node rc=20" with
 * no window).
 *
 * What is genuinely process-global stays global: node's InitializeOncePerProcess
 * result and the V8 platform, because V8 cannot be initialised twice in one
 * address space (see the __AROS__ note in node.cc's TearDownOncePerProcess).
 * Everything that belongs to ONE runtime - its loop, isolate, isolate data,
 * Environment, context and allocator - moves into a per-owner record below.
 *
 * The owner is the exec unique ID of the task that prepared the embedder, not
 * the task pointer: task pointers are recycled, and an ID that has gone away
 * must not match a later task that happens to reuse the address. Entries are
 * looked up by the CALLING task with the same one-level parent fallback the
 * per-task base uses, so the threads a runtime creates find their own runtime
 * rather than a neighbour's.
 */
static std::shared_ptr<node::InitializationResult> NodeEmbedInitialization;
static LONG NodeEmbedConfiguredWorkerThreads;
/* ONE platform for the library, deliberately: node::MultiIsolatePlatform
   exists to host several isolates, V8::InitializePlatform and
   cppgc::InitializeProcess are process-wide one-time calls, and a second app
   creating its own freed the first app's platform under its running isolate
   (instruction fetch from NULL, ERR=0x14). Each app still gets its own
   isolate, environment and loop; they share the scheduler underneath. */
static std::unique_ptr<node::MultiIsolatePlatform> NodeEmbedPlatform;
static bool NodeEmbedPlatformInitialized;

struct NodeEmbedderState {
  ULONG owner;
  node::IsolateData *isolate_data;
  node::Environment *environment;
  uv_loop_t *loop;
  bool loop_initialized;
  v8::Isolate *isolate;
  v8::Global<v8::Context> *context;
  node::ArrayBufferAllocator *allocator;
  bool owns_isolate;
};

/* Electron apps are whole AROS processes; a handful covers every app a
   machine can usefully run at once, and a fixed array keeps the lookup free
   of allocation on paths that must not allocate. */
static constexpr size_t NodeEmbedderCapacity = 8;
static NodeEmbedderState NodeEmbedders[NodeEmbedderCapacity];
static NodeEmbedderState NodeEmbedderFallback;

/*
 * A host-selected slot, per calling task: 0 means "use my own runtime".
 *
 * Per TASK, not one static: the renderer thread selects its own runtime for
 * nodeIntegration while the main task is running the app's, and a single
 * static would have each stealing the other's. Small fixed table, looked up
 * by exec unique ID, same as the runtime records.
 */
struct NodeSlotSelection {
  ULONG task;
  LONG slot;
};

static constexpr size_t NodeSlotSelectionCapacity = 16;
static NodeSlotSelection NodeSlotSelections[NodeSlotSelectionCapacity];

static LONG NodeGetSelectedSlot(void)
{
  ULONG task = GetETaskID(FindTask(NULL));

  for (size_t index = 0; index < NodeSlotSelectionCapacity; ++index) {
    if (NodeSlotSelections[index].task == task)
      return NodeSlotSelections[index].slot;
  }
  return 0;
}

static void NodeSetSelectedSlot(LONG slot)
{
  ULONG task = GetETaskID(FindTask(NULL));
  size_t free_index = NodeSlotSelectionCapacity;

  for (size_t index = 0; index < NodeSlotSelectionCapacity; ++index) {
    if (NodeSlotSelections[index].task == task) {
      NodeSlotSelections[index].slot = slot;
      if (slot == 0)
        NodeSlotSelections[index].task = 0;
      return;
    }
    if (NodeSlotSelections[index].task == 0 &&
        free_index == NodeSlotSelectionCapacity)
      free_index = index;
  }
  if (slot != 0 && free_index < NodeSlotSelectionCapacity) {
    NodeSlotSelections[free_index].task = task;
    NodeSlotSelections[free_index].slot = slot;
  }
}

/* Slot records are not owned by a task, so they are tagged with a value no
   GetETaskID can produce (IDs are small and never have the top bit set). */
static ULONG NodeSlotOwnerTag(size_t index)
{
  return 0x80000000u | (ULONG)(index + 1);
}

static NodeEmbedderState *NodeFindEmbedder(ULONG owner)
{
  if (owner == 0)
    return nullptr;
  for (size_t index = 0; index < NodeEmbedderCapacity; ++index) {
    if (NodeEmbedders[index].owner == owner)
      return &NodeEmbedders[index];
  }
  return nullptr;
}

/* The record for the calling task: its own, else its creator's (a thread the
   runtime started), else a free slot claimed for it, else a shared fallback
   so a ninth app degrades instead of scribbling on someone else's runtime. */
static NodeEmbedderState *NodeCurrentEmbedder(void)
{
  {
    LONG selected = NodeGetSelectedSlot();

    if (selected >= 1 && (size_t)selected <= NodeEmbedderCapacity)
      return &NodeEmbedders[selected - 1];
  }

  struct Task *self = FindTask(NULL);
  ULONG owner = GetETaskID(self);
  NodeEmbedderState *state = NodeFindEmbedder(owner);

  if (state != nullptr)
    return state;

  {
    struct ETask *etask = GetETask(self);
    struct Task *parent = etask != nullptr ?
        (struct Task *)etask->et_Parent : nullptr;

    if (parent != nullptr) {
      state = NodeFindEmbedder(GetETaskID(parent));
      if (state != nullptr)
        return state;
    }
  }

  for (size_t index = 0; index < NodeEmbedderCapacity; ++index) {
    if (NodeEmbedders[index].owner == 0) {
      NodeEmbedders[index].owner = owner;
      return &NodeEmbedders[index];
    }
  }

  NodeBridgeLog("[NodeEmbed] no free embedder slot for task %p\n", self);
  return &NodeEmbedderFallback;
}

/* The rest of the bridge keeps its original names; each one now reads the
   calling task's record instead of a library-wide static. */
#define NodeEmbedIsolateData (NodeCurrentEmbedder()->isolate_data)
#define NodeEmbedEnvironment (NodeCurrentEmbedder()->environment)
#define NodeEmbedLoop (NodeCurrentEmbedder()->loop)
#define NodeEmbedLoopInitialized (NodeCurrentEmbedder()->loop_initialized)
#define NodeEmbedIsolate (NodeCurrentEmbedder()->isolate)
#define NodeEmbedContext (NodeCurrentEmbedder()->context)
#define NodeEmbedArrayBufferAllocator (NodeCurrentEmbedder()->allocator)
#define NodeEmbedOwnsIsolate (NodeCurrentEmbedder()->owns_isolate)
/* Node 22's highest reserved ContextEmbedderIndex (NODE_CONTEXT_TAG). */
static constexpr int NodeContextTagEmbedderIndex = 39;
static constexpr size_t NodeLinkedBindingCapacity = 8;
static constexpr size_t NodeLinkedBindingNameCapacity = 64;

using NodeV8InitializePlatformFn = void (*)(v8::Platform *);
using NodeV8InitializeCppgcFn = void (*)(v8::PageAllocator *, size_t);
using NodeV8GetCurrentPlatformFn = v8::Platform *(*)();
using NodeV8SetCurrentPlatformFn = void (*)(v8::Platform *);
using NodeV8TryCatchStackTraceFn = v8::MaybeLocal<v8::Value> (*)(
    const v8::TryCatch *, v8::Local<v8::Context>);

static NodeV8GetCurrentPlatformFn NodeV8GetCurrentPlatform;
static NodeV8SetCurrentPlatformFn NodeV8SetCurrentPlatform;

class NodeV8PlatformScope {
 public:
  explicit NodeV8PlatformScope(v8::Platform *platform)
      : previous_(nullptr), active_(false) {
    if (NodeV8GetCurrentPlatform == nullptr ||
        NodeV8SetCurrentPlatform == nullptr)
      return;
    previous_ = NodeV8GetCurrentPlatform();
    if (platform != nullptr)
      NodeV8SetCurrentPlatform(platform);
    active_ = true;
  }

  ~NodeV8PlatformScope() {
    if (active_)
      NodeV8SetCurrentPlatform(previous_);
  }

  NodeV8PlatformScope(const NodeV8PlatformScope &) = delete;
  NodeV8PlatformScope &operator=(const NodeV8PlatformScope &) = delete;

 private:
  v8::Platform *previous_;
  bool active_;
};

struct NodePendingLinkedBinding {
  char name[NodeLinkedBindingNameCapacity];
  NodeEmbedderLinkedBindingInvoke invoke;
  void *private_data;
};

static NodePendingLinkedBinding
    NodePendingLinkedBindings[NodeLinkedBindingCapacity];
static size_t NodePendingLinkedBindingCount;

static void NodeInvokeLinkedBinding(
    const v8::FunctionCallbackInfo<v8::Value>& arguments)
{
  if (arguments.Data().IsEmpty() || !arguments.Data()->IsExternal())
    return;

  auto *binding = static_cast<NodePendingLinkedBinding *>(
      v8::Local<v8::External>::Cast(arguments.Data())->Value());
  if (binding == nullptr || binding->invoke == nullptr)
    return;

  v8::Isolate *isolate = arguments.GetIsolate();
  v8::String::Utf8Value method(
      isolate, arguments.Length() > 0 ? arguments[0] : v8::Local<v8::Value>());
  v8::String::Utf8Value payload(
      isolate, arguments.Length() > 1 ? arguments[1] : v8::Local<v8::Value>());
  CONST_STRPTR result = binding->invoke(
      *method != nullptr ? *method : "",
      *payload != nullptr ? *payload : "",
      binding->private_data);
  if (result == nullptr)
    result = "";

  v8::Local<v8::String> value;
  if (v8::String::NewFromUtf8(
          isolate,
          reinterpret_cast<const char *>(result),
          v8::NewStringType::kNormal).ToLocal(&value))
    arguments.GetReturnValue().Set(value);
}

static void NodeInitializeLinkedBinding(v8::Local<v8::Object> exports,
                                        v8::Local<v8::Value> module,
                                        v8::Local<v8::Context> context,
                                        void *private_data)
{
  auto *binding = static_cast<NodePendingLinkedBinding *>(private_data);
  v8::Isolate *isolate = context->GetIsolate();
  v8::Local<v8::String> name;
  v8::Local<v8::Function> function;

  (void)module;
  if (!v8::String::NewFromUtf8(
          isolate, "invoke", v8::NewStringType::kNormal).ToLocal(&name))
    return;
  if (!v8::Function::New(
          context,
          NodeInvokeLinkedBinding,
          v8::External::New(isolate, binding)).ToLocal(&function))
    return;
  v8::Maybe<bool> set_result = exports->Set(context, name, function);
  (void)set_result;
}

static LONG NodeEnsureEmbedLoop()
{
  if (NodeEmbedLoopInitialized)
    return 0;

  NodeEmbedLoop =
      static_cast<uv_loop_t *>(std::calloc(1, uv_loop_size()));
  if (NodeEmbedLoop == nullptr)
    return UV_ENOMEM;

  int uv_rc = uv_loop_init(NodeEmbedLoop);
  if (uv_rc != 0)
  {
    std::free(NodeEmbedLoop);
    NodeEmbedLoop = nullptr;
    return uv_rc;
  }
  NodeEmbedLoopInitialized = true;
  return 0;
}

static void NodeCleanupFailedAttach()
{
  if (NodeEmbedEnvironment != nullptr) {
    node::FreeEnvironment(NodeEmbedEnvironment);
    NodeEmbedEnvironment = nullptr;
  }
  if (NodeEmbedIsolateData != nullptr) {
    node::FreeIsolateData(NodeEmbedIsolateData);
    NodeEmbedIsolateData = nullptr;
  }
  if (NodeEmbedLoopInitialized) {
    int loop_rc = uv_loop_close(NodeEmbedLoop);
    if (loop_rc == 0) {
      std::free(NodeEmbedLoop);
      NodeEmbedLoop = nullptr;
      NodeEmbedLoopInitialized = false;
    } else {
      NodeBridgeLog("[NodeEmbed] failed-attach uv_loop_close rc=%d\n", loop_rc);
    }
  }
}

static LONG NodeAttachV8Context(v8::Isolate *isolate,
                                v8::Local<v8::Context> context,
                                CONST_STRPTR script,
                                bool defer_failed_cleanup)
{
  LONG loop_rc = NodeEnsureEmbedLoop();
  if (loop_rc != 0)
    return loop_rc;

  NodeEmbedIsolateData =
      node::CreateIsolateData(
          isolate, NodeEmbedLoop, NodeEmbedPlatform.get(),
          NodeEmbedArrayBufferAllocator);
  if (NodeEmbedIsolateData == nullptr) {
    uv_loop_close(NodeEmbedLoop);
    std::free(NodeEmbedLoop);
    NodeEmbedLoop = nullptr;
    NodeEmbedLoopInitialized = false;
    return 20;
  }

  const uint64_t environment_flags =
      node::EnvironmentFlags::kDefaultFlags |
      node::EnvironmentFlags::kNoRegisterESMLoader |
      node::EnvironmentFlags::kNoNativeAddons |
      node::EnvironmentFlags::kNoCreateInspector |
      node::EnvironmentFlags::kNoStartDebugSignalHandler |
      node::EnvironmentFlags::kNoWaitForInspectorFrontend;

  NodeEmbedEnvironment = node::CreateEnvironment(
      NodeEmbedIsolateData,
      context,
      NodeEmbedInitialization->args(),
      NodeEmbedInitialization->exec_args(),
      static_cast<node::EnvironmentFlags::Flags>(environment_flags));
  if (NodeEmbedEnvironment == nullptr)
    goto fail;

  for (size_t index = 0; index < NodePendingLinkedBindingCount; ++index) {
    NodePendingLinkedBinding& binding = NodePendingLinkedBindings[index];
    node::AddLinkedBinding(NodeEmbedEnvironment,
                           binding.name,
                           NodeInitializeLinkedBinding,
                           &binding);
  }

  if (script != nullptr) {
    /*
     * This context shares Chromium's isolate but is deliberately not a Blink
     * context.  Never let a startup exception reach Blink's isolate-global
     * message listener: it expects its ScriptState embedder slot and treats
     * an isolated Node context as a security invariant violation.
     */
    v8::TryCatch try_catch(isolate);
    if (node::LoadEnvironment(
            NodeEmbedEnvironment, reinterpret_cast<const char *>(script))
            .IsEmpty()) {
      v8::String::Utf8Value exception(isolate, try_catch.Exception());
      NodeBridgeLog("[NodeEmbed] startup exception: %s\n",
              *exception != nullptr ? *exception : "<unavailable>");
      auto stack_trace_fn = reinterpret_cast<NodeV8TryCatchStackTraceFn>(
          NodeV8ResolveEngineSymbol(
              "_ZNK2v88TryCatch10StackTraceENS_5LocalINS_7ContextEEE"));
      v8::Local<v8::Value> stack_trace;
      if (stack_trace_fn != nullptr &&
          stack_trace_fn(&try_catch, context).ToLocal(&stack_trace)) {
        v8::String::Utf8Value stack(isolate, stack_trace);
        NodeBridgeLog("[NodeEmbed] startup stack: %s\n",
                *stack != nullptr ? *stack : "<unavailable>");
      }
      goto fail;
    }
  }

  NodeEmbedIsolate = isolate;
  NodeEmbedContext = new v8::Global<v8::Context>(isolate, context);
  NodeBridgeLog("[NodeEmbed] attached isolate=%p context=%p env=%p\n",
          isolate,
          *context,
          NodeEmbedEnvironment);
  return 0;

fail:
  if (!defer_failed_cleanup)
    NodeCleanupFailedAttach();
  return 20;
}

/*
 * Bundled c-ares uses the classic AROS SocketBase symbol.  Keep that base
 * owned by node.library instead of pulling in the bsdsocket link library:
 * the latter makes loading Node fail before AROSTCP has started.
 */
extern "C" {
struct Library *SocketBase = nullptr;
}

static void NodeTraceOpenUvHandle(uv_handle_t *handle, void *arg)
{
  (void)arg;
  uv_handle_type type = handle->type;

  NodeBridgeLog("[NodeLibrary] uv handle=%p type=%d active=%d closing=%d\n",
          handle,
          static_cast<int>(type),
          uv_is_active(handle),
          uv_is_closing(handle));
}

/*
 * node::Start() runs V8's parser/compiler and the bootstrap on the calling
 * task's stack, and V8 sizes its own stack limit from the entry sp assuming
 * the megabytes a Linux main thread has.  A Shell hands a command
 * AROS_STACKSIZE (40 KiB) of it, and a posixc exec'd child inherits the
 * CLI's cli_DefaultStack, so C:Node started without a `Stack` line overruns
 * the hunk allocated below its stack - the same class as ElectronShell's
 * shell_windows[] smash.  Run node on a stack
 * of our own unless the caller already provided a big one.
 */
#define NODE_STACK_MIN  (8 * 1024 * 1024)
#define NODE_STACK_SIZE (16 * 1024 * 1024)

struct NodeStartArgs {
  LONG argc;
  STRPTR *argv;
};

static LONG NodeStartOnStack(struct NodeStartArgs *args);

static IPTR NodeStartStackEntry(struct NodeStartArgs *args)
{
  return static_cast<IPTR>(NodeStartOnStack(args));
}

extern "C" {

AROS_LH2(LONG, NodeStart,
    AROS_LHA(LONG, argc, D0),
    AROS_LHA(STRPTR *, argv, A0),
    LIBBASETYPEPTR, LIBBASE, 5, Node)
{
  AROS_LIBFUNC_INIT
  NodeBindTaskBase(LIBBASE);

  struct Task *me = FindTask(nullptr);
  IPTR have = reinterpret_cast<IPTR>(me->tc_SPUpper) -
      reinterpret_cast<IPTR>(me->tc_SPLower);
  struct NodeStartArgs args = { argc, argv };

  if (have >= NODE_STACK_MIN)
    return NodeStartOnStack(&args);

  APTR stack = AllocMem(NODE_STACK_SIZE, MEMF_ANY);
  if (stack == nullptr) {
    NodeBridgeLog("[NodeLibrary] no %lu KiB stack, running on the caller's "
            "%lu KiB\n",
            static_cast<unsigned long>(NODE_STACK_SIZE / 1024),
            static_cast<unsigned long>(have / 1024));
    return NodeStartOnStack(&args);
  }

  struct StackSwapStruct sss;
  struct StackSwapArgs swap_args;
  swap_args.Args[0] = reinterpret_cast<IPTR>(&args);
  sss.stk_Lower = stack;
  sss.stk_Upper = reinterpret_cast<APTR>(
      reinterpret_cast<IPTR>(stack) + NODE_STACK_SIZE);
  sss.stk_Pointer = sss.stk_Upper;
  LONG rc = static_cast<LONG>(NewStackSwap(&sss,
      reinterpret_cast<APTR>(NodeStartStackEntry), &swap_args));
  FreeMem(stack, NODE_STACK_SIZE);
  return rc;
  AROS_LIBFUNC_EXIT
}

}  /* extern "C" */

static LONG NodeStartOnStack(struct NodeStartArgs *args)
{
  LONG argc = args->argc;
  STRPTR *argv = args->argv;
  bool close_socket_base = false;

  /*
   * uv_default_loop() here is this process's loop: uv1 is a rellib of this
   * pertaskbase module, so the call goes through the uv1 base that
   * OpenLibrary("node.library") opened on this task, and uv1 keeps the
   * default loop in that base.  Before node.library was pertaskbase its one
   * global UV1Base re-pointed every caller's uv1 slot to the first loader's
   * base, and a C:Node spawned by another Node process tore down its
   * parent's loop: uv_run() drove the parent's check handle into the
   * parent's isolate and V8 aborted the child with "Entering the V8 API
   * without proper locking in place".
   */
  if (SocketBase == nullptr) {
    SocketBase = OpenLibrary("bsdsocket.library", 4);
    close_socket_base = SocketBase != nullptr;
  }

  int rc = node::Start(
      static_cast<int>(argc), reinterpret_cast<char **>(argv));
  uv_loop_t *loop = uv_default_loop();

  uv_run(loop, UV_RUN_NOWAIT);
  int loop_rc = uv_loop_close(loop);

  if (loop_rc != 0) {
    NodeBridgeLog("[NodeLibrary] uv_loop_close failed rc=%d; open handles follow\n",
            loop_rc);
    uv_walk(loop, NodeTraceOpenUvHandle, nullptr);
  }
  if (rc == 0 && loop_rc != 0)
    rc = loop_rc;
  if (close_socket_base) {
    CloseLibrary(SocketBase);
    SocketBase = nullptr;
  }
  return static_cast<LONG>(rc);
}

extern "C" {

AROS_LH0(CONST_STRPTR, NodeVersion,
    LIBBASETYPEPTR, LIBBASE, 6, Node)
{
  AROS_LIBFUNC_INIT
  NodeBindTaskBase(LIBBASE);
  return NODE_VERSION;
  AROS_LIBFUNC_EXIT
}

AROS_LH0(LONG, NodePrepareEmbedder,
    LIBBASETYPEPTR, LIBBASE, 7, Node)
{
  AROS_LIBFUNC_INIT
  NodeBindTaskBase(LIBBASE);

  if (NodeEmbedInitialization)
    return 0;

  const std::vector<std::string> args{"AROS-Electron"};
  NodeEmbedInitialization = node::InitializeOncePerProcess(
      args,
      {
          node::ProcessInitializationFlags::kNoDefaultSignalHandling,
          node::ProcessInitializationFlags::kNoStdioInitialization,
          node::ProcessInitializationFlags::kNoInitializeV8,
          node::ProcessInitializationFlags::kNoInitializeNodeV8Platform,
          node::ProcessInitializationFlags::kNoInitializeCppgc,
          node::ProcessInitializationFlags::kNoInitOpenSSL,
          node::ProcessInitializationFlags::kNoParseGlobalDebugVariables,
          node::ProcessInitializationFlags::kNoAdjustResourceLimits,
          node::ProcessInitializationFlags::kNoUseLargePages,
      });

  if (!NodeEmbedInitialization)
    return 20;
  for (const std::string& error : NodeEmbedInitialization->errors())
    NodeBridgeLog("[NodeEmbed] initialize: %s\n", error.c_str());
  if (NodeEmbedInitialization->early_return()) {
    LONG rc = NodeEmbedInitialization->exit_code();
    NodeEmbedInitialization.reset();
    return rc != 0 ? rc : 20;
  }

  NodeBridgeLog("[NodeEmbed] process state prepared without V8 ownership\n");
  return 0;
  AROS_LIBFUNC_EXIT
}

AROS_LH1(LONG, NodeAttachCurrentV8Context,
    AROS_LHA(CONST_STRPTR, script, A0),
    LIBBASETYPEPTR, LIBBASE, 8, Node)
{
  AROS_LIBFUNC_INIT
  NodeBindTaskBase(LIBBASE);

  if (!NodeEmbedInitialization || NodeEmbedEnvironment != nullptr)
    return 20;

  v8::Isolate *isolate = v8::Isolate::GetCurrent();
  if (isolate == nullptr)
    return 20;
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  if (context.IsEmpty())
    return 20;

  return NodeAttachV8Context(isolate, context, script, false);
  AROS_LIBFUNC_EXIT
}

AROS_LH0(LONG, NodePumpEmbedder,
    LIBBASETYPEPTR, LIBBASE, 9, Node)
{
  AROS_LIBFUNC_INIT
  NodeBindTaskBase(LIBBASE);
  if (!NodeEmbedLoopInitialized || NodeEmbedEnvironment == nullptr ||
      NodeEmbedIsolate == nullptr || NodeEmbedContext == nullptr)
    return 20;

  v8::Isolate *isolate = NodeEmbedIsolate;
  NodeV8PlatformScope platform_scope(
      NodeEmbedOwnsIsolate ? NodeEmbedPlatform.get() : nullptr);
  v8::Isolate::Scope isolate_scope(isolate);
  v8::HandleScope handle_scope(isolate);
  v8::Local<v8::Context> context =
      v8::Local<v8::Context>::New(isolate, *NodeEmbedContext);
  v8::Context::Scope context_scope(context);

  /*
   * Blink keeps renderer isolates on kScoped, while Node's callback machinery
   * requires kExplicit.  Match Electron's UvRunOnce boundary: change policy
   * only while entering Node through uv_run, then restore the policy belonging
   * to the isolate owner.
   */
  isolate->SetMicrotasksPolicy(v8::MicrotasksPolicy::kExplicit);
  const LONG result = uv_run(NodeEmbedLoop, UV_RUN_NOWAIT);
  /*
   * Only for an isolate this platform actually manages.
   *
   * A runtime attached to someone else's isolate - a nodeIntegration renderer
   * attaches to Blink's - was never registered with our platform (nothing
   * calls RegisterIsolate; owned isolates get the platform passed at creation,
   * and the scope above and the teardown below already make that distinction).
   * Draining a foreground queue that does not exist does not return: pumping a
   * renderer's Node loop this way wedged the whole shell, and the browser
   * side's own pump stopped after a single iteration. Blink drives foreground
   * tasks for its own isolate.
   */
  if (NodeEmbedOwnsIsolate && NodeEmbedPlatform != nullptr)
    NodeEmbedPlatform->DrainTasks(isolate);
  context->GetMicrotaskQueue()->PerformCheckpoint(isolate);
  isolate->SetMicrotasksPolicy(NodeEmbedOwnsIsolate
      ? v8::MicrotasksPolicy::kExplicit
      : v8::MicrotasksPolicy::kScoped);
  return result;
  AROS_LIBFUNC_EXIT
}

AROS_LH0(void, NodeDetachEmbedder,
    LIBBASETYPEPTR, LIBBASE, 10, Node)
{
  AROS_LIBFUNC_INIT
  NodeBindTaskBase(LIBBASE);

  v8::Isolate *isolate = NodeEmbedIsolate;
  const bool owned_isolate = NodeEmbedOwnsIsolate;
  {
    NodeV8PlatformScope platform_scope(
        owned_isolate ? NodeEmbedPlatform.get() : nullptr);
    if (isolate != nullptr) {
      v8::Isolate::Scope isolate_scope(isolate);
      if (NodeEmbedEnvironment != nullptr) {
        node::Stop(NodeEmbedEnvironment,
                   node::StopFlags::kDoNotTerminateIsolate);
        node::FreeEnvironment(NodeEmbedEnvironment);
        NodeEmbedEnvironment = nullptr;
      }
      if (NodeEmbedContext != nullptr) {
        NodeEmbedContext->Reset();
        delete NodeEmbedContext;
        NodeEmbedContext = nullptr;
      }
    }
    if (NodeEmbedIsolateData != nullptr) {
      node::FreeIsolateData(NodeEmbedIsolateData);
      NodeEmbedIsolateData = nullptr;
    }
    if (owned_isolate && NodeEmbedPlatform && isolate != nullptr)
      NodeEmbedPlatform->UnregisterIsolate(isolate);
    if (NodeEmbedLoopInitialized) {
      int loop_rc = uv_loop_close(NodeEmbedLoop);
      if (loop_rc != 0)
        NodeBridgeLog("[NodeEmbed] uv_loop_close rc=%d\n", loop_rc);
      else {
        std::free(NodeEmbedLoop);
        NodeEmbedLoop = nullptr;
        NodeEmbedLoopInitialized = false;
      }
    }
    if (owned_isolate && isolate != nullptr) {
      isolate->Dispose();
      node::FreeArrayBufferAllocator(NodeEmbedArrayBufferAllocator);
    }
  }
  /* The platform and node's process-wide state are shared with every other
     app in this library now, so one app detaching must not tear them down -
     that is the same "first app's engine freed under it" failure seen when
     each app created its own platform. They go with the library. */
  NodeEmbedArrayBufferAllocator = nullptr;
  NodeEmbedArrayBufferAllocator = nullptr;
  NodeEmbedOwnsIsolate = false;
  NodeEmbedIsolate = nullptr;
  NodeBridgeLog("[NodeEmbed] detached environment owned_isolate=%d\n",
          owned_isolate ? 1 : 0);
  AROS_LIBFUNC_EXIT
}

AROS_LH1(LONG, NodeAttachIsolatedV8Context,
    AROS_LHA(CONST_STRPTR, script, A0),
    LIBBASETYPEPTR, LIBBASE, 11, Node)
{
  AROS_LIBFUNC_INIT
  NodeBindTaskBase(LIBBASE);

  if (!NodeEmbedInitialization || NodeEmbedEnvironment != nullptr)
    return 20;

  v8::Isolate *isolate = v8::Isolate::GetCurrent();
  if (isolate == nullptr)
    return 20;

  v8::HandleScope handle_scope(isolate);
  v8::Local<v8::Context> context = v8::Context::New(isolate);
  if (context.IsEmpty())
    return 20;

  /*
   * CreateEnvironment() grows the embedder-data array through Node's context
   * tag slot.  V8 fills newly-created lower slots with tagged `undefined`,
   * which Blink's isolate-global callbacks can mistake for a ScriptState
   * pointer in gin's slot.  Grow the array first, then initialize every slot
   * as an aligned null pointer before Node assigns its reserved fields.
  */
  context->SetAlignedPointerInEmbedderData(
      NodeContextTagEmbedderIndex, nullptr);
  const int inherited_fields = context->GetNumberOfEmbedderDataFields();
  for (int index = 0; index < inherited_fields; ++index)
    context->SetAlignedPointerInEmbedderData(index, nullptr);
  v8::Context::Scope context_scope(context);
  if (node::InitializeContext(context).IsNothing())
    return 20;

  return NodeAttachV8Context(isolate, context, script, false);
  AROS_LIBFUNC_EXIT
}

AROS_LH3(LONG, NodeRegisterEmbedderLinkedBinding,
    AROS_LHA(CONST_STRPTR, name, A0),
    AROS_LHA(APTR, invoke, A1),
    AROS_LHA(APTR, private_data, A2),
    LIBBASETYPEPTR, LIBBASE, 12, Node)
{
  AROS_LIBFUNC_INIT
  NodeBindTaskBase(LIBBASE);

  if (name == nullptr || name[0] == '\0' || invoke == nullptr)
    return 20;
  /*
   * A binding has to be registered before CreateEnvironment, so an existing
   * environment normally means "too late". But node.library is one library
   * serving every AROS process, and a SECOND Electron app registers the very
   * same binding electron.library always registers ("aros_electron", the same
   * invoke function) before preparing its own embedder. Refusing that made the
   * second app die at its first call with rc=20 and no window - the whole
   * reason two Electron apps could not run at once.
   *
   * So: an identical re-registration is accepted as the no-op it is, and only
   * a DIFFERENT binding arriving after the environment exists is refused,
   * because that one genuinely cannot be honoured.
   */
  if (NodeEmbedEnvironment != nullptr) {
    for (size_t index = 0; index < NodePendingLinkedBindingCount; ++index) {
      const NodePendingLinkedBinding& binding = NodePendingLinkedBindings[index];
      if (std::strcmp(binding.name, reinterpret_cast<const char *>(name)) == 0 &&
          binding.invoke ==
              reinterpret_cast<NodeEmbedderLinkedBindingInvoke>(invoke))
        return 0;
    }
    return 20;
  }

  for (size_t index = 0; index < NodePendingLinkedBindingCount; ++index) {
    NodePendingLinkedBinding& binding = NodePendingLinkedBindings[index];
    if (std::strcmp(binding.name, reinterpret_cast<const char *>(name)) == 0) {
      binding.invoke =
          reinterpret_cast<NodeEmbedderLinkedBindingInvoke>(invoke);
      binding.private_data = private_data;
      return 0;
    }
  }

  if (NodePendingLinkedBindingCount == NodeLinkedBindingCapacity ||
      std::strlen(reinterpret_cast<const char *>(name)) >=
          NodeLinkedBindingNameCapacity)
    return 20;

  NodePendingLinkedBinding& binding =
      NodePendingLinkedBindings[NodePendingLinkedBindingCount++];
  std::strcpy(binding.name, reinterpret_cast<const char *>(name));
  binding.invoke = reinterpret_cast<NodeEmbedderLinkedBindingInvoke>(invoke);
  binding.private_data = private_data;
  return 0;
  AROS_LIBFUNC_EXIT
}

AROS_LH1(LONG, NodeAttachOwnedV8Context,
    AROS_LHA(CONST_STRPTR, script, A0),
    LIBBASETYPEPTR, LIBBASE, 13, Node)
{
  AROS_LIBFUNC_INIT
  NodeBindTaskBase(LIBBASE);

  if (!NodeEmbedInitialization || NodeEmbedEnvironment != nullptr ||
      NodeEmbedIsolate != nullptr)
    return 20;

  NodeV8GetCurrentPlatform =
      reinterpret_cast<NodeV8GetCurrentPlatformFn>(
          NodeV8ResolveEngineSymbol("__aros_v8_get_current_platform"));
  NodeV8SetCurrentPlatform =
      reinterpret_cast<NodeV8SetCurrentPlatformFn>(
          NodeV8ResolveEngineSymbol("__aros_v8_set_current_platform"));
  if (NodeV8GetCurrentPlatform == nullptr ||
      NodeV8SetCurrentPlatform == nullptr)
    return 20;

  /*
   * V8's platform context is task-local on AROS. Keep Chromium's platform
   * installed on the browser/UI task outside Node entry points so renderer
   * Processes inherit the platform whose worker count configured their heap.
   */
  v8::Platform *browser_platform = NodeV8GetCurrentPlatform();
  NodeV8PlatformScope browser_platform_restore(nullptr);

  /*
   * The resident engine keeps V8 process initialization global while its AROS
   * platform pointer is task/Process-local. Browser and renderer Processes
   * therefore install their own scheduler platform without copying the
   * engine. An Electron main Process has no gin renderer to do that first,
   * so initialize the authoritative v8.library platform before allocating its
   * owned isolate.
   */
  if (V8Initialize() == nullptr) {
    NodeBridgeLog("[NodeEmbed] shared v8.library platform initialization failed\n");
    return 20;
  }

  /*
   * NodePlatform includes one delayed-task scheduler in its reported worker
   * count in addition to the requested pool. V8 caches this count globally in
   * GC paths, so match the Chromium platform already installed in this
   * Process. Otherwise the later renderer observes a different core count and
   * mark-compact aborts even though each task has the correct platform.
   */
  int node_pool_size = 4;
  if (NodeEmbedConfiguredWorkerThreads > 1) {
    node_pool_size =
        static_cast<int>(NodeEmbedConfiguredWorkerThreads) - 1;
  } else if (browser_platform != nullptr) {
    const int browser_workers = browser_platform->NumberOfWorkerThreads();
    if (browser_workers > 1)
      node_pool_size = browser_workers - 1;
  }
  if (!NodeEmbedPlatform) {
    NodeEmbedPlatform = node::MultiIsolatePlatform::Create(node_pool_size);
    if (!NodeEmbedPlatform)
      return 20;
  }
  NodeBridgeLog("[NodeEmbed] platform workers configured=%ld browser=%d node=%d\n",
          NodeEmbedConfiguredWorkerThreads,
          browser_platform != nullptr
              ? browser_platform->NumberOfWorkerThreads() : -1,
          NodeEmbedPlatform->NumberOfWorkerThreads());
  auto initialize_platform = reinterpret_cast<NodeV8InitializePlatformFn>(
      NodeV8ResolveEngineSymbol(
          "_ZN2v82V818InitializePlatformEPNS_8PlatformE"));
  if (initialize_platform == nullptr) {
    if (!NodeEmbedPlatformInitialized)
      if (!NodeEmbedPlatformInitialized)
        if (!NodeEmbedPlatformInitialized)
      NodeEmbedPlatform.reset();
    return 20;
  }
  /* Once per library: a second InitializePlatform with a live isolate on the
     first one is what took app 1 down when a second app started. */
  if (!NodeEmbedPlatformInitialized)
    initialize_platform(NodeEmbedPlatform.get());

  /*
   * Node's IsolateData creates a CppHeap for an owned main isolate. The
   * renderer normally initializes cppgc through gin/Blink, but this browser
   * Process runs first. AROS v8.library makes cppgc process initialization
   * resident and idempotent, so initialize that shared engine state here.
   */
  auto initialize_cppgc = reinterpret_cast<NodeV8InitializeCppgcFn>(
      NodeV8ResolveEngineSymbol(
          "_ZN5cppgc17InitializeProcessEPN2v813PageAllocatorEm"));
  if (initialize_cppgc == nullptr) {
    if (!NodeEmbedPlatformInitialized)
      if (!NodeEmbedPlatformInitialized)
        if (!NodeEmbedPlatformInitialized)
      NodeEmbedPlatform.reset();
    return 20;
  }
  if (!NodeEmbedPlatformInitialized)
    initialize_cppgc(nullptr, 0);
  NodeEmbedPlatformInitialized = true;

  node::ArrayBufferAllocator *allocator =
      node::CreateArrayBufferAllocator();
  if (allocator == nullptr)
    return 20;

  LONG loop_rc = NodeEnsureEmbedLoop();
  if (loop_rc != 0) {
    node::FreeArrayBufferAllocator(allocator);
    if (!NodeEmbedPlatformInitialized)
      NodeEmbedPlatform.reset();
    return loop_rc;
  }

  v8::Isolate *isolate =
      node::NewIsolate(allocator, NodeEmbedLoop, NodeEmbedPlatform.get());
  if (isolate == nullptr) {
    node::FreeArrayBufferAllocator(allocator);
    if (!NodeEmbedPlatformInitialized)
      NodeEmbedPlatform.reset();
    return 20;
  }
  NodeEmbedArrayBufferAllocator = allocator;

  LONG result;
  {
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = v8::Context::New(isolate);
    if (context.IsEmpty()) {
      result = 20;
    } else {
      v8::Context::Scope context_scope(context);
      if (node::InitializeContext(context).IsNothing())
        result = 20;
      else
        result = NodeAttachV8Context(isolate, context, script, true);
    }
  }

  if (result != 0) {
    NodeCleanupFailedAttach();
    NodeEmbedPlatform->UnregisterIsolate(isolate);
    isolate->Dispose();
    node::FreeArrayBufferAllocator(allocator);
    NodeEmbedArrayBufferAllocator = nullptr;
    if (!NodeEmbedPlatformInitialized)
      NodeEmbedPlatform.reset();
    return result;
  }
  NodeEmbedOwnsIsolate = true;
  NodeBridgeLog("[NodeEmbed] owns dedicated embedder isolate=%p\n", isolate);
  return 0;
  AROS_LIBFUNC_EXIT
}

AROS_LH1(LONG, NodeConfigureOwnedV8WorkerThreads,
    AROS_LHA(LONG, worker_threads, D0),
    LIBBASETYPEPTR, LIBBASE, 14, Node)
{
  AROS_LIBFUNC_INIT
  NodeBindTaskBase(LIBBASE);

  if (worker_threads < 2 || NodeEmbedIsolate != nullptr ||
      NodeEmbedEnvironment != nullptr)
    return 20;
  NodeEmbedConfiguredWorkerThreads = worker_threads;
  return 0;
  AROS_LIBFUNC_EXIT
}

/*
 * Several apps inside ONE process.
 *
 * The per-app records above are found by the owning task, which is the right
 * key when an app is an AROS process. CEF is not built that way: it keeps one
 * browser context and one UI thread per process, so the way to run several
 * Electron apps on one engine is to host them in a single process - one
 * CefInitialize, one UI thread, a window and a Node runtime each. Then every
 * app shares a task, and "which runtime is this" can no longer be answered by
 * asking which task is calling.
 *
 * So a host can take slots explicitly: create one per app, select it around
 * the calls that belong to that app (attach, pump, detach), and release it
 * when the app closes. A task with no slot selected keeps the old behaviour
 * exactly - its own record, found by task - so C:Node and a single-app
 * ElectronShell are unaffected.
 */
AROS_LH0(LONG, NodeCreateRuntimeSlot,
    LIBBASETYPEPTR, LIBBASE, 15, Node)
{
  AROS_LIBFUNC_INIT
  NodeBindTaskBase(LIBBASE);

  for (size_t index = 0; index < NodeEmbedderCapacity; ++index) {
    if (NodeEmbedders[index].owner == 0) {
      NodeEmbedders[index].owner = NodeSlotOwnerTag(index);
      return (LONG)(index + 1);
    }
  }
  return 0;
  AROS_LIBFUNC_EXIT
}

AROS_LH1(LONG, NodeSelectRuntimeSlot,
    AROS_LHA(LONG, slot, D0),
    LIBBASETYPEPTR, LIBBASE, 16, Node)
{
  AROS_LIBFUNC_INIT
  NodeBindTaskBase(LIBBASE);

  /* Slot 0 means "back to this task's own runtime". */
  if (slot == 0) {
    NodeSetSelectedSlot(0);
    return 0;
  }
  if (slot < 1 || (size_t)slot > NodeEmbedderCapacity ||
      NodeEmbedders[slot - 1].owner != NodeSlotOwnerTag((size_t)slot - 1))
    return 20;
  NodeSetSelectedSlot(slot);
  return 0;
  AROS_LIBFUNC_EXIT
}

AROS_LH1(LONG, NodeReleaseRuntimeSlot,
    AROS_LHA(LONG, slot, D0),
    LIBBASETYPEPTR, LIBBASE, 17, Node)
{
  AROS_LIBFUNC_INIT
  NodeBindTaskBase(LIBBASE);

  if (slot < 1 || (size_t)slot > NodeEmbedderCapacity ||
      NodeEmbedders[slot - 1].owner != NodeSlotOwnerTag((size_t)slot - 1))
    return 20;
  /* The caller is expected to have detached the runtime already; this only
     gives the slot back, and never while it is the selected one. */
  if (NodeGetSelectedSlot() == slot)
    NodeSetSelectedSlot(0);
  NodeEmbedders[slot - 1].owner = 0;
  return 0;
  AROS_LIBFUNC_EXIT
}

}  // extern "C"
