#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>

#include "bismuth/cocos_hooks.hpp"

using namespace geode::prelude;

namespace {
bismuth::AtlasPassiveObserver g_observer;
bool g_hooksReady = false;

void applyGpuBridgeSetting(bool enabled) {
    // The draw replacement is intentionally locked off until real per-quad
    // clip-space bounds and runtime validation are available.
    (void)enabled;
    bismuth::setGpuDrawBridgeEnabled(false);
    log::info("Bismuth GPU draw bridge: OFF (awaiting calibrated bounds and device validation)");
}
}

$on_mod(Loaded) {
    std::string error;
    g_hooksReady = bismuth::installCocosPassiveHooks(&g_observer, &error);
    if (g_hooksReady) {
        log::info("Bismuth: verified Geometry Dash 2.2081 Cocos hooks installed.");
    } else {
        log::warn("Bismuth: GPU bridge unavailable; original renderer remains active. Reason: {}", error);
    }

    const bool enabled = Mod::get()->getSettingValue<bool>("gpu-draw-bridge-enabled");
    applyGpuBridgeSetting(enabled);

    listenForSettingChanges<bool>("gpu-draw-bridge-enabled", [](bool value) {
        applyGpuBridgeSetting(value);
        if (Mod::get()->getSettingValue<bool>("diagnostics")) {
            Notification::create(
                "GPU draw bridge remains locked until culling is validated",
                NotificationIcon::Info
            )->show();
        }
    });

    listenForSettingChanges<bool>("diagnostics", [](bool enabled) {
        if (enabled) {
            log::info("Bismuth diagnostics enabled; hooks ready: {}, GPU bridge: {}",
                g_hooksReady, bismuth::isGpuDrawBridgeEnabled());
        }
    });
}
