#pragma once

#include "bismuth/atlas_observation.hpp"
#include "bismuth/gpu_timer_query.hpp"
#include <string>

namespace bismuth {

// Installs build-ID-gated, passive hooks for the verified Geometry Dash
// 2.2081 ARM64 Cocos2d symbols. Call after libcocos2dcpp.so is loaded and
// before its render path starts executing.
bool installCocosPassiveHooks(AtlasPassiveObserver* observer, std::string* error);

// Detaches telemetry safely. The pass-through hooks remain installed for the
// process lifetime because unpatching while game threads execute is unsafe.
// Runtime switch for passive counters/timers. Disabling skips measurement work;
// installed pass-through trampolines remain in place for process safety.
void setPassiveTelemetryEnabled(bool enabled) noexcept;
bool isPassiveTelemetryEnabled() noexcept;

// Experimental full-visible GPU EBO bridge. Disabled by default; enable only
// for controlled on-device validation after passive hooks are confirmed.
void setGpuDrawBridgeEnabled(bool enabled) noexcept;
bool isGpuDrawBridgeEnabled() noexcept;

// Optional asynchronous GPU timing around the original atlas draw. Queries
// are disabled by default and never wait for completion.
void setGpuTimingEnabled(bool enabled) noexcept;
bool isGpuTimingEnabled() noexcept;
GpuTimingSnapshot gpuTimingSnapshot() noexcept;

void detachCocosPassiveHooks(AtlasPassiveObserver* observer) noexcept;

} // namespace bismuth
