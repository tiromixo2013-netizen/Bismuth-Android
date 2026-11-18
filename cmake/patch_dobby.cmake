if(NOT DEFINED DOBBY_SOURCE_DIR OR NOT EXISTS "${DOBBY_SOURCE_DIR}/source/dobby.cpp")
  message(FATAL_ERROR "DOBBY_SOURCE_DIR does not point to the fetched Dobby source")
endif()

function(replace_once relative old_text new_text)
  set(path "${DOBBY_SOURCE_DIR}/${relative}")
  file(READ "${path}" content)
  string(FIND "${content}" "${new_text}" already)
  if(NOT already EQUAL -1)
    return()
  endif()
  string(FIND "${content}" "${old_text}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Dobby patch context not found in ${relative}; refusing a partial/unknown patch")
  endif()
  string(REPLACE "${old_text}" "${new_text}" content "${content}")
  file(WRITE "${path}" "${content}")
endfunction()

replace_once("source/InterceptRouting/InterceptRouting.h"
[=[  addr_t trampoline_addr() {
    if (near_trampoline)
      return near_trampoline->addr();
    return trampoline->addr();
  }]=]
[=[  addr_t trampoline_addr() {
    if (near_trampoline)
      return near_trampoline->addr();
    if (!trampoline)
      return 0;
    return trampoline->addr();
  }]=])

replace_once("source/InterceptRouting/InterceptRouting.h"
[=[  size_t trampoline_size() {
    if (near_trampoline)
      return near_trampoline->size();
    return trampoline->size();
  }]=]
[=[  size_t trampoline_size() {
    if (near_trampoline)
      return near_trampoline->size();
    if (!trampoline)
      return 0;
    return trampoline->size();
  }]=])

replace_once("source/InterceptRouting/InterceptRouting.h"
[=[  virtual void Active() {
    __FUNC_CALL_TRACE__();
    auto ret = DobbyCodePatch((void *)entry->addr, (uint8_t *)trampoline_addr(), trampoline_size());
    error |= (ret != 0);
  }]=]
[=[  virtual void Active() {
    __FUNC_CALL_TRACE__();
    if (error || !trampoline_addr() || !trampoline_size() || !entry->patched.addr() || !entry->patched.size) {
      error = 1;
      return;
    }
    auto ret = DobbyCodePatch((void *)entry->addr, (uint8_t *)trampoline_addr(), trampoline_size());
    error |= (ret != 0);
  }]=])

replace_once("source/InterceptRouting/InterceptRouting.h"
[=[    if (!near_trampoline) {
      trampoline = GenerateNormalTrampolineBuffer(from, to);
    }
    return true;
  }

  void GenerateRelocatedCode() {]=]
[=[    if (!near_trampoline) {
#if defined(DOBBY_TESTING)
      if (dobby_test_generate_normal_trampoline)
        trampoline = dobby_test_generate_normal_trampoline(from, to);
      else
#endif
        trampoline = GenerateNormalTrampolineBuffer(from, to);
    }
    if (!trampoline_addr() || !trampoline_size()) {
      error = 1;
      ERROR_LOG("failed to allocate trampoline buffer");
      return false;
    }
    return true;
  }

  bool GenerateRelocatedCode() {]=])

replace_once("source/InterceptRouting/InterceptRouting.h"
[=[    if (trampoline_addr() == 0) {
      ERROR_LOG("GenerateTrampoline must be called first");
      error = 1;
    }]=]
[=[    if (!trampoline_addr() || !trampoline_size()) {
      ERROR_LOG("GenerateTrampoline must be called first");
      error = 1;
      return false;
    }]=])

replace_once("source/InterceptRouting/InterceptRouting.h"
[=[    if (relocated.size == 0) {
      error = 1;
      return;
    }]=]
[=[    if (!origin.size || !relocated.addr() || !relocated.size) {
      error = 1;
      ERROR_LOG("instruction relocation failed");
      return false;
    }]=])

replace_once("source/InterceptRouting/InterceptRouting.h"
[=[    entry->patched = origin;
    entry->relocated = relocated;
  }

  void BackupOriginCode() {
    __FUNC_CALL_TRACE__();
    entry->backup_orig_code();
  }]=]
[=[    entry->patched = origin;
    entry->relocated = relocated;
    return true;
  }

  bool BackupOriginCode() {
    __FUNC_CALL_TRACE__();
    if (!entry->patched.addr() || !entry->patched.size || !entry->backup_orig_code()) {
      error = 1;
      ERROR_LOG("failed to back up original instructions");
      return false;
    }
    return true;
  }]=])

replace_once("source/InterceptRouting/InterceptRouting.h"
[=[  void GenerateRelocatedCode() {]=]
[=[  bool GenerateRelocatedCode() {]=])

replace_once("source/InterceptRouting/InlineHookRouting.h"
[=[  void BuildRouting() {
    __FUNC_CALL_TRACE__();

    GenerateTrampoline();

    GenerateRelocatedCode();

    BackupOriginCode();
  }]=]
[=[  bool BuildRouting() {
    __FUNC_CALL_TRACE__();
    if (!GenerateTrampoline() || error)
      return false;
    if (!GenerateRelocatedCode() || error)
      return false;
    if (!BackupOriginCode() || error)
      return false;
    return true;
  }]=])

replace_once("source/InterceptRouting/InlineHookRouting.h"
[=[  if (!address) {
    ERROR_LOG("address is 0x0");
    return -1;
  }

  features::apple::arm64e_pac_strip(address);]=]
[=[  if (out_origin_func)
    *out_origin_func = nullptr;
  if (!address) {
    ERROR_LOG("address is 0x0");
    return -1;
  }

  features::apple::arm64e_pac_strip(address);]=])

replace_once("source/InterceptRouting/InlineHookRouting.h"
[=[  auto routing = new InlineHookRouting(entry, (addr_t)fake_func);
  routing->BuildRouting();
  routing->Active();
  entry->routing = routing;

  if (routing->error) {
    ERROR_LOG("build routing error.");
    return -1;
  }]=]
[=[  auto routing = new InlineHookRouting(entry, (addr_t)fake_func);
  entry->routing = routing;
  if (!routing->BuildRouting()) {
    ERROR_LOG("build routing failed before target patch");
    delete routing;
    delete entry;
    return -1;
  }
  routing->Active();
  if (routing->error) {
    ERROR_LOG("target patch failed; attempting original-byte restore");
    if (!entry->restore_orig_code())
      ERROR_LOG("CRITICAL: original-byte restore failed; process must be treated as compromised");
    delete routing;
    delete entry;
    return -1;
  }]=])

replace_once("source/InterceptRouting/InstrumentRouting.h"
[=[  void BuildRouting() {
    __FUNC_CALL_TRACE__();

    GenerateInstrumentClosureTrampoline();

    GenerateTrampoline();

    GenerateRelocatedCode();

    BackupOriginCode();
  }]=]
[=[  bool BuildRouting() {
    __FUNC_CALL_TRACE__();
    GenerateInstrumentClosureTrampoline();
    if (!instrument_tramp || !instrument_tramp->addr()) {
      error = 1;
      return false;
    }
    if (!GenerateTrampoline() || error)
      return false;
    if (!GenerateRelocatedCode() || error)
      return false;
    if (!BackupOriginCode() || error)
      return false;
    return true;
  }]=])

replace_once("source/InterceptRouting/InstrumentRouting.h"
[=[  auto routing = new InstrumentRouting(entry, pre_handler);
  routing->BuildRouting();
  routing->Active();
  entry->routing = routing;

  if (routing->error) {
    ERROR_LOG("build routing error.");
    return -1;
  }]=]
[=[  auto routing = new InstrumentRouting(entry, pre_handler);
  entry->routing = routing;
  if (!routing->BuildRouting()) {
    ERROR_LOG("instrument routing failed before target patch");
    delete routing;
    delete entry;
    return -1;
  }
  routing->Active();
  if (routing->error) {
    ERROR_LOG("instrument target patch failed; attempting original-byte restore");
    if (!entry->restore_orig_code())
      ERROR_LOG("CRITICAL: instrument restore failed; process must be treated as compromised");
    delete routing;
    delete entry;
    return -1;
  }]=])

replace_once("source/Interceptor.h"
[=[#include "TrampolineBridge/Trampoline/Trampoline.h"]=]
[=[#include "TrampolineBridge/Trampoline/Trampoline.h"
#include <stdlib.h>]=])

replace_once("source/Interceptor.h"
[=[    ~Entry() {
    }]=]
[=[    ~Entry() {
      free(origin_code_);
      origin_code_ = nullptr;
    }]=])

replace_once("source/Interceptor.h"
[=[    void backup_orig_code() {
      __FUNC_CALL_TRACE__();
      auto orig = (uint8_t *)this->addr;
      uint32_t tramp_size = this->patched.size;
      origin_code_ = (uint8_t *)operator new(tramp_size);
      memcpy(origin_code_, orig, tramp_size);
    }

    void restore_orig_code() {
      __FUNC_CALL_TRACE__();
      DobbyCodePatch((void *)patched.addr(), origin_code_, patched.size);
      operator delete(origin_code_);
      origin_code_ = nullptr;
    }]=]
[=[    bool backup_orig_code() {
      __FUNC_CALL_TRACE__();
      if (!patched.addr() || !patched.size)
        return false;
      auto orig = (uint8_t *)this->addr;
      uint32_t tramp_size = this->patched.size;
      origin_code_ = (uint8_t *)malloc(tramp_size);
      if (!origin_code_)
        return false;
      memcpy(origin_code_, orig, tramp_size);
      return true;
    }

    bool restore_orig_code() {
      __FUNC_CALL_TRACE__();
      if (!origin_code_ || !patched.addr() || !patched.size)
        return false;
      auto result = DobbyCodePatch((void *)patched.addr(), origin_code_, patched.size);
      if (result != 0)
        return false;
      free(origin_code_);
      origin_code_ = nullptr;
      return true;
    }]=])

replace_once("source/dobby.cpp"
[=[  auto entry = gInterceptor.find((addr_t)address);
  if (entry) {
    gInterceptor.remove((addr_t)address);
    entry->restore_orig_code();]=]
[=[  auto entry = gInterceptor.find((addr_t)address);
  if (entry) {
    if (!entry->restore_orig_code()) {
      ERROR_LOG("failed to restore original instructions; hook remains registered");
      return -1;
    }
    gInterceptor.remove((addr_t)address);]=])

replace_once("source/Backend/UserMode/ExecMemory/code-patch-tool-posix.cc"
[=[  // change page permission as rwx
  mprotect((void *)patch_page, page_size, PROT_READ | PROT_WRITE | PROT_EXEC);
  if (patch_page != patch_end_page) {
    mprotect((void *)patch_end_page, page_size, PROT_READ | PROT_WRITE | PROT_EXEC);
  }

  // patch buffer
  memcpy(address, buffer, buffer_size);

  // restore page permission
  mprotect((void *)patch_page, page_size, PROT_READ | PROT_EXEC);
  if (patch_page != patch_end_page) {
    mprotect((void *)patch_end_page, page_size, PROT_READ | PROT_EXEC);
  }

  addr_t clear_start_ = (addr_t)address;
  ClearCache((void *)clear_start_, (void *)(clear_start_ + buffer_size));]=]
[=[  // Do not write any target bytes unless every page is writable.
  if (mprotect((void *)patch_page, page_size, PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
    return -1;
  if (patch_page != patch_end_page) {
    if (mprotect((void *)patch_end_page, page_size, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
      mprotect((void *)patch_page, page_size, PROT_READ | PROT_EXEC);
      return -1;
    }
  }

  // Once bytes are written, the detour is live. Never report a false failure
  // that would leave a patched target unregistered and without its trampoline.
  memcpy(address, buffer, buffer_size);
  addr_t clear_start_ = (addr_t)address;
  ClearCache((void *)clear_start_, (void *)(clear_start_ + buffer_size));

  // Restore the executable/read-only permissions used by the original code.
  int restore_first = mprotect((void *)patch_page, page_size, PROT_READ | PROT_EXEC);
  int restore_second = 0;
  if (patch_page != patch_end_page)
    restore_second = mprotect((void *)patch_end_page, page_size, PROT_READ | PROT_EXEC);
  if (restore_first != 0 || restore_second != 0)
    ERROR_LOG("code patch is active, but restoring RX page permissions failed");]=])

replace_once("source/InterceptRouting/InlineHookRouting.h"
[=[  if (out_origin_func) {
    *out_origin_func = (void *)entry->relocated.addr();
  }
  features::apple::arm64e_pac_strip_and_sign(*out_origin_func);]=]
[=[  if (out_origin_func) {
    *out_origin_func = (void *)entry->relocated.addr();
    features::apple::arm64e_pac_strip_and_sign(*out_origin_func);
  }]=])

replace_once("source/InterceptRouting/InlineHookRouting.h"
[=[  if (routing->error) {
    ERROR_LOG("target patch failed; attempting original-byte restore");
    if (!entry->restore_orig_code())
      ERROR_LOG("CRITICAL: original-byte restore failed; process must be treated as compromised");
    delete routing;
    delete entry;
    return -1;
  }]=]
[=[  if (routing->error) {
    // POSIX DobbyCodePatch reports failure only before memcpy, so no target bytes changed.
    ERROR_LOG("target patch failed before any target bytes were written");
    delete routing;
    delete entry;
    return -1;
  }]=])

replace_once("source/InterceptRouting/InstrumentRouting.h"
[=[  if (routing->error) {
    ERROR_LOG("instrument target patch failed; attempting original-byte restore");
    if (!entry->restore_orig_code())
      ERROR_LOG("CRITICAL: instrument restore failed; process must be treated as compromised");
    delete routing;
    delete entry;
    return -1;
  }]=]
[=[  if (routing->error) {
    // POSIX DobbyCodePatch reports failure only before memcpy, so no target bytes changed.
    ERROR_LOG("instrument target patch failed before any target bytes were written");
    delete routing;
    delete entry;
    return -1;
  }]=])
replace_once("source/Backend/UserMode/ExecMemory/code-patch-tool-posix.cc"
[=[  int page_size = (int)sysconf(_SC_PAGESIZE);
  uintptr_t patch_page = ALIGN_FLOOR(address, page_size);
  uintptr_t patch_end_page = ALIGN_FLOOR((uintptr_t)address + buffer_size, page_size);]=]
[=[  if (!address || !buffer || !buffer_size)
    return -1;
  long page_size_long = sysconf(_SC_PAGESIZE);
  if (page_size_long <= 0 || (uintptr_t)address > UINTPTR_MAX - buffer_size)
    return -1;
  int page_size = (int)page_size_long;
  uintptr_t patch_page = ALIGN_FLOOR(address, page_size);
  uintptr_t patch_end_page = ALIGN_FLOOR((uintptr_t)address + buffer_size - 1, page_size);]=])

replace_once("source/Backend/UserMode/ExecMemory/code-patch-tool-posix.cc"
[=[      mprotect((void *)patch_page, page_size, PROT_READ | PROT_EXEC);
      return -1;]=]
[=[      if (mprotect((void *)patch_page, page_size, PROT_READ | PROT_EXEC) != 0)
        ERROR_LOG("failed to restore first page permissions after second-page mprotect failure");
      return -1;]=])

replace_once("source/InterceptRouting/InterceptRouting.h"
[=[Trampoline *GenerateNearTrampolineBuffer(addr_t src, addr_t dst);
Trampoline *GenerateNormalTrampolineBuffer(addr_t from, addr_t to);]=]
[=[Trampoline *GenerateNearTrampolineBuffer(addr_t src, addr_t dst);
Trampoline *GenerateNormalTrampolineBuffer(addr_t from, addr_t to);

#if defined(DOBBY_TESTING)
inline Trampoline *(*dobby_test_generate_normal_trampoline)(addr_t, addr_t) = nullptr;
inline void (*dobby_test_relocate_code_and_branch)(void *, CodeMemBlock *, CodeMemBlock *) = nullptr;
#endif]=])

replace_once("source/InterceptRouting/InterceptRouting.h"
[=[    GenRelocateCodeAndBranch((void *)code_addr, &origin, &relocated);]=]
[=[#if defined(DOBBY_TESTING)
    if (dobby_test_relocate_code_and_branch)
      dobby_test_relocate_code_and_branch((void *)code_addr, &origin, &relocated);
    else
#endif
      GenRelocateCodeAndBranch((void *)code_addr, &origin, &relocated);]=])
