#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>

using namespace geode::prelude;

namespace {
bool g_prototypePreference = false;

void reportState() {
    auto diagnostics = Mod::get()->getSettingValue<bool>("diagnostics");
    if (diagnostics) {
        log::info("Bismuth Geode shell: prototype preference is {}. Renderer bridge is not connected in this package.",
            g_prototypePreference ? "ON" : "OFF");
    }
}
}

$on_mod(Loaded) {
    g_prototypePreference = Mod::get()->getSettingValue<bool>("prototype-enabled");
    reportState();

    listenForSettingChanges("prototype-enabled", [](bool enabled) {
        g_prototypePreference = enabled;
        reportState();
        if (Mod::get()->getSettingValue<bool>("diagnostics")) {
            Notification::create(
                enabled
                    ? "Bismuth preference set to ON (renderer bridge not connected)"
                    : "Bismuth preference set to OFF",
                NotificationIcon::Info
            )->show();
        }
    });

    listenForSettingChanges("diagnostics", [](bool) {
        reportState();
    });
}
