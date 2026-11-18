#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "dobby.h"
#include "InterceptRouting/InterceptRouting.h"

namespace {
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

void replacement_function() {
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
    std::puts("Dobby trampoline/relocation fail-closed tests passed");
    return 0;
}
