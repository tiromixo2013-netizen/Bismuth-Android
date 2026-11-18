#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

#include "dobby.h"
#include "InterceptRouting/InterceptRouting.h"

namespace {
void replacement_function();

std::array<std::uint8_t, 32> trampoline_storage{};

Trampoline *fail_trampoline_allocation(addr_t, addr_t) {
    return nullptr;
}

Trampoline *provide_test_trampoline(addr_t, addr_t) {
    return new Trampoline(TRAMPOLINE_X64_JMP,
                          CodeMemBlock(reinterpret_cast<addr_t>(trampoline_storage.data()),
                                       trampoline_storage.size()));
}

void fail_instruction_relocation(void *, CodeMemBlock *, CodeMemBlock *) {
    // Intentionally leave the relocated block empty to simulate decoder/
    // relocation failure before Dobby is allowed to patch the target.
}

void provide_relocated_code(void *, CodeMemBlock *, CodeMemBlock *relocated) {
    static std::array<std::uint8_t, 32> storage{};
    *relocated = CodeMemBlock(reinterpret_cast<addr_t>(storage.data()), storage.size());
}

void *fail_backup_allocation(std::size_t) {
    return nullptr;
}

void run_backup_failure_case() {
    std::array<std::uint8_t, 32> target{};
    target.fill(0x90);
    const auto original = target;

    void *origin = reinterpret_cast<void *>(0x1);
    dobby_test_generate_normal_trampoline = provide_test_trampoline;
    dobby_test_relocate_code_and_branch = provide_relocated_code;
    dobby_test_allocate_backup = fail_backup_allocation;

    const int result = DobbyHook(target.data(),
                                 reinterpret_cast<void *>(&replacement_function),
                                 &origin);

    assert(result != 0);
    assert(origin == nullptr);
    assert(target == original);

    dobby_test_generate_normal_trampoline = nullptr;
    dobby_test_relocate_code_and_branch = nullptr;
    dobby_test_allocate_backup = nullptr;
}

void replacement_function() {
}

int fail_code_restore(void *, std::uint8_t *, std::uint32_t) {
    return -1;
}

extern "C" int hooked_function() {
    return 99;
}

void run_successful_hook_lifecycle() {
    const long pageSize = sysconf(_SC_PAGESIZE);
    assert(pageSize > 0);
    auto *code = static_cast<std::uint8_t *>(
        mmap(nullptr, static_cast<std::size_t>(pageSize),
             PROT_READ | PROT_WRITE | PROT_EXEC,
             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    assert(code != MAP_FAILED);

    // x86-64: mov eax, 42; ret; followed by safe padding for relocation.
    std::memset(code, 0x90, static_cast<std::size_t>(pageSize));
    const std::array<std::uint8_t, 6> body{0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3};
    std::memcpy(code, body.data(), body.size());

    using Function = int (*)();
    auto target = reinterpret_cast<Function>(code);
    void *origin = nullptr;
    assert(target() == 42);
    assert(DobbyHook(code, reinterpret_cast<void *>(&hooked_function), &origin) == 0);
    assert(origin != nullptr);
    assert(target() == 99);
    assert(reinterpret_cast<Function>(origin)() == 42);

    // A failed restore must keep the interceptor entry registered so a later
    // retry can restore the target instead of losing the recovery record.
    dobby_test_restore_code_patch = fail_code_restore;
    assert(DobbyDestroy(code) != 0);
    assert(target() == 99);
    dobby_test_restore_code_patch = nullptr;
    assert(DobbyDestroy(code) == 0);
    assert(target() == 42);
    assert(munmap(code, static_cast<std::size_t>(pageSize)) == 0);
}

void run_failure_case(bool fail_allocation) {
    std::array<std::uint8_t, 32> target{};
    target.fill(0x90);
    const auto original = target;

    void *origin = reinterpret_cast<void *>(0x1);
    dobby_test_generate_normal_trampoline =
        fail_allocation ? fail_trampoline_allocation : provide_test_trampoline;
    dobby_test_relocate_code_and_branch =
        fail_allocation ? nullptr : fail_instruction_relocation;

    const int result = DobbyHook(target.data(),
                                 reinterpret_cast<void *>(&replacement_function),
                                 &origin);

    assert(result != 0);
    assert(origin == nullptr);
    assert(target == original);

    dobby_test_generate_normal_trampoline = nullptr;
    dobby_test_relocate_code_and_branch = nullptr;
}
} // namespace

int main() {
    run_failure_case(true);
    run_failure_case(false);
    run_backup_failure_case();
    run_successful_hook_lifecycle();
    std::puts("Dobby trampoline, relocation, backup, and lifecycle tests passed");
    return 0;
}
