#pragma once

#include <atomic>

namespace bismuth {

// Controls optional passive-hook measurements without unpatching live code.
// Relaxed ordering is sufficient: the flag publishes no associated data.
class TelemetryGate {
public:
    bool enabled() const noexcept {
        return enabled_.load(std::memory_order_relaxed);
    }
    void setEnabled(bool enabled) noexcept {
        enabled_.store(enabled, std::memory_order_relaxed);
    }
private:
    std::atomic<bool> enabled_{true};
};

} // namespace bismuth
