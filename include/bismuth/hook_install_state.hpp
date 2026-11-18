#pragma once

#include <cstdint>

namespace bismuth {

enum class HookInstallState : std::uint8_t {
    Clean,
    Installing,
    Partial,
    Installed
};

// If an installation attempt exits before commit(), the process is marked
// Partial. The caller must hold its install mutex for the guard's lifetime.
class HookInstallAttemptGuard final {
public:
    explicit HookInstallAttemptGuard(HookInstallState& state) noexcept : state_(state) {}
    ~HookInstallAttemptGuard() {
        if (!committed_ && state_ == HookInstallState::Installing)
            state_ = HookInstallState::Partial;
    }
    void commit() noexcept {
        state_ = HookInstallState::Installed;
        committed_ = true;
    }
    HookInstallAttemptGuard(const HookInstallAttemptGuard&) = delete;
    HookInstallAttemptGuard& operator=(const HookInstallAttemptGuard&) = delete;
private:
    HookInstallState& state_;
    bool committed_ = false;
};

} // namespace bismuth
