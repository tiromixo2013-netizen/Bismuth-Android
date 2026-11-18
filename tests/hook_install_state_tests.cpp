#include <cassert>
#include "bismuth/hook_install_state.hpp"

int main() {
    using namespace bismuth;

    HookInstallState failed = HookInstallState::Clean;
    failed = HookInstallState::Installing;
    {
        HookInstallAttemptGuard attempt(failed);
        // Simulate an early return after the first detour attempt.
    }
    assert(failed == HookInstallState::Partial);

    HookInstallState successful = HookInstallState::Clean;
    successful = HookInstallState::Installing;
    {
        HookInstallAttemptGuard attempt(successful);
        attempt.commit();
    }
    assert(successful == HookInstallState::Installed);

    // A committed install remains Installed when its observer is detached;
    // detach is not an unhook/rollback operation.
    assert(successful == HookInstallState::Installed);
    return 0;
}
