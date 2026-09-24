/*
 * Exact-build internal symbol resolver for uv1.library ABI thunks.
 *
 * libuv has a broad, stable C ABI. AROS callers normally use the LVO linklib,
 * but Node needs substantially more entry points than the initial curated
 * function list. This resolver lets a generated native thunk archive reach
 * functions already present in uv1.library without duplicating libuv in the
 * executable. Missing capabilities must still be implemented explicitly.
 */

#include <aros/asmcall.h>
#include <exec/types.h>
#include <libraries/debug.h>
#include <proto/debug.h>
#include <proto/exec.h>

#include <stdlib.h>
#include <string.h>

struct uv_aros_symbol_entry {
  const char* name;
  void* address;
};

struct uv_aros_symbol_index {
  struct uv_aros_symbol_entry* entries;
  unsigned long count;
  unsigned long capacity;
  const char* last_module;
  int last_is_uv1;
  int failed;
};

static struct uv_aros_symbol_index uv_aros_symbols;
static volatile long uv_aros_symbol_state;

static int uv_aros_is_library_module(const char* module_name) {
  static const char suffix[] = "uv1.library";
  size_t module_length;
  const size_t suffix_length = sizeof(suffix) - 1;

  if (module_name == NULL)
    return 0;
  module_length = strlen(module_name);
  return module_length >= suffix_length &&
         strcmp(module_name + module_length - suffix_length, suffix) == 0;
}

AROS_UFH3(static void, uv_aros_index_symbol_hook,
    AROS_UFHA(struct Hook*, hook, A0),
    AROS_UFHA(void*, object, A2),
    AROS_UFHA(struct SymbolInfo*, symbol, A1)) {
  AROS_USERFUNC_INIT

  struct uv_aros_symbol_index* index =
      (struct uv_aros_symbol_index*)hook->h_Data;

  (void)object;
  if (index->failed || symbol == NULL || symbol->si_SymbolName == NULL)
    return;

  if (symbol->si_ModuleName != index->last_module) {
    index->last_module = symbol->si_ModuleName;
    index->last_is_uv1 = uv_aros_is_library_module(symbol->si_ModuleName);
  }
  if (!index->last_is_uv1)
    return;

  if (index->count == index->capacity) {
    unsigned long capacity = index->capacity ? index->capacity * 2 : 512;
    struct uv_aros_symbol_entry* entries =
        realloc(index->entries, capacity * sizeof(*entries));
    if (entries == NULL) {
      index->failed = 1;
      return;
    }
    index->entries = entries;
    index->capacity = capacity;
  }

  index->entries[index->count].name = symbol->si_SymbolName;
  index->entries[index->count].address = symbol->si_SymbolStart;
  index->count++;

  AROS_USERFUNC_EXIT
}

static int uv_aros_compare_symbols(const void* left, const void* right) {
  const struct uv_aros_symbol_entry* left_entry = left;
  const struct uv_aros_symbol_entry* right_entry = right;
  return strcmp(left_entry->name, right_entry->name);
}

static int uv_aros_build_symbol_index(void) {
  struct Hook hook = {0};

  memset(&uv_aros_symbols, 0, sizeof(uv_aros_symbols));
  hook.h_Entry = (HOOKFUNC)uv_aros_index_symbol_hook;
  hook.h_Data = &uv_aros_symbols;
  EnumerateSymbolsA(&hook, NULL);

  if (!uv_aros_symbols.failed && uv_aros_symbols.count != 0)
    qsort(uv_aros_symbols.entries, uv_aros_symbols.count,
          sizeof(uv_aros_symbols.entries[0]), uv_aros_compare_symbols);

  return !uv_aros_symbols.failed && uv_aros_symbols.count != 0;
}

static int uv_aros_ensure_symbol_index(void) {
  for (;;) {
    long state = __atomic_load_n(&uv_aros_symbol_state, __ATOMIC_ACQUIRE);
    if (state == 2)
      return 1;
    if (state == 3)
      return 0;

    if (state == 0) {
      long expected = 0;
      if (__atomic_compare_exchange_n(&uv_aros_symbol_state, &expected, 1, 0,
                                      __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        int built = uv_aros_build_symbol_index();
        __atomic_store_n(&uv_aros_symbol_state, built ? 2 : 3,
                         __ATOMIC_RELEASE);
        return built;
      }
    }
    Reschedule();
  }
}

void* uv_aros_find_internal_symbol(const char* symbol_name) {
  unsigned long low = 0;
  unsigned long high;

  if (symbol_name == NULL || symbol_name[0] == '\0' ||
      !uv_aros_ensure_symbol_index())
    return NULL;

  high = uv_aros_symbols.count;
  while (low < high) {
    unsigned long middle = low + (high - low) / 2;
    int comparison =
        strcmp(symbol_name, uv_aros_symbols.entries[middle].name);
    if (comparison < 0)
      high = middle;
    else if (comparison > 0)
      low = middle + 1;
    else
      return uv_aros_symbols.entries[middle].address;
  }
  return NULL;
}
