#pragma once

#include "bismuth/atlas_observation.hpp"
#include <string>

namespace bismuth {

// Installs build-ID-gated, passive hooks for the verified Geometry Dash
// 2.2081 ARM64 Cocos2d symbols. Call after libcocos2dcpp.so is loaded and
// before its render path starts executing.
bool installCocosPassiveHooks(AtlasPassiveObserver* observer, std::string* error);

// Detaches telemetry safely. The pass-through hooks remain installed for the
// process lifetime because unpatching while game threads execute is unsafe.
// Experimental full-visible GPU EBO bridge. Disabled by default; enable only
// for controlled on-device validation after passive hooks are confirmed.
void setGpuDrawBridgeEnabled(bool enabled) noexcept;
bool isGpuDrawBridgeEnabled() noexcept;

void detachCocosPassiveHooks(AtlasPassiveObserver* observer) noexcept;

} // namespace bismuth
