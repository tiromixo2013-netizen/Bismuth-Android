#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/EditorPauseLayer.hpp>

#include "bismuth/cocos_hooks.hpp"

using namespace geode::prelude;

namespace {
bismuth::AtlasPassiveObserver g_observer;
bool g_hooksReady = false;

void applySettings() {
    const bool telemetry = Mod::get()->getSettingValue<bool>("passive-telemetry");
    const bool gpuTiming = Mod::get()->getSettingValue<bool>("gpu-timing");

    // The all-visible EBO bridge is deliberately kept disabled. It is not
    // real sprite culling and has not passed on-device visual/performance tests.
    bismuth::setGpuDrawBridgeEnabled(false);
    bismuth::setPassiveTelemetryEnabled(telemetry && g_hooksReady);
    bismuth::setGpuTimingEnabled(gpuTiming && g_hooksReady);
}

void addPauseSettingsButton(CCNode* layer, CCObject* target, SEL_MenuHandler selector) {
    auto icon = CCSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png");
    if (!icon) {
        log::warn("Bismuth: pause settings icon sprite was not found.");
        return;
    }

    icon->setScale(0.62f);
    auto button = CCMenuItemSpriteExtra::create(icon, target, selector);
    if (!button) return;

    auto menu = CCMenu::create();
    if (!menu) return;
    menu->addChild(button);
    menu->setPosition(CCPointZero);
    menu->setID("bismuth.android/pause-settings-menu"_spr);
    layer->addChild(menu, 1000);

    // Keep it in the left rail, below the usual top-left mod-button position.
    // A separate menu prevents changing or reordering another mod's controls.
    const auto size = CCDirector::sharedDirector()->getWinSize();
    button->setPosition({28.f, size.height - 88.f});
    button->setID("bismuth.android/pause-settings"_spr);
}
}

class $modify(BismuthPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        addPauseSettingsButton(this, this, menu_selector(BismuthPauseLayer::onBismuthSettings));
    }

    void onBismuthSettings(CCObject*) {
        openSettingsPopup(Mod::get());
    }
};

class $modify(BismuthEditorPauseLayer, EditorPauseLayer) {
    void customSetup() {
        EditorPauseLayer::customSetup();
        addPauseSettingsButton(this, this, menu_selector(BismuthEditorPauseLayer::onBismuthSettings));
    }

    void onBismuthSettings(CCObject*) {
        openSettingsPopup(Mod::get());
    }
};

$on_mod(Loaded) {
    std::string error;
    g_hooksReady = bismuth::installCocosPassiveHooks(&g_observer, &error);
    if (g_hooksReady) {
        log::info("Bismuth: verified 2.2081 ARM64 passive Cocos hooks installed.");
    } else {
        log::warn("Bismuth: passive hooks unavailable; original renderer remains active. {}", error);
    }

    applySettings();
    listenForSettingChanges<bool>("passive-telemetry", [](bool) { applySettings(); });
    listenForSettingChanges<bool>("gpu-timing", [](bool) { applySettings(); });
    listenForSettingChanges<bool>("diagnostics", [](bool enabled) {
        if (enabled) {
            log::info("Bismuth diagnostics: hooks ready={}, GPU timing supported={}",
                g_hooksReady, bismuth::gpuTimingSnapshot().supported);
        }
    });
}
