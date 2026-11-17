#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/EditorPauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>

using namespace geode::prelude;

namespace {
CCLabelBMFont* g_bismuthHud = nullptr;
std::array<float, 120> g_frameIntervalsMs{};
std::size_t g_frameIntervalCount = 0;
std::size_t g_frameIntervalCursor = 0;

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

    const auto size = CCDirector::sharedDirector()->getWinSize();
    button->setPosition({28.f, size.height - 88.f});
    button->setID("bismuth.android/pause-settings"_spr);
}

void createHud(CCNode* layer) {
    g_bismuthHud = nullptr;
    if (!Mod::get()->getSettingValue<bool>("hud-enabled")) return;

    auto label = CCLabelBMFont::create("BISMUTH | waiting for frame samples", "bigFont.fnt");
    if (!label) {
        log::warn("Bismuth: could not create the in-game telemetry label.");
        return;
    }

    label->setAnchorPoint({0.f, 1.f});
    label->setScale(0.34f);
    label->setOpacity(220);
    const auto size = CCDirector::sharedDirector()->getWinSize();
    label->setPosition({8.f, size.height - 8.f});
    label->setZOrder(20000);
    label->setID("bismuth.android/live-hud"_spr);
    layer->addChild(label, 20000);
    g_bismuthHud = label;
}

void updateHud(float dt, double updateCpuMs) {
    if (!g_bismuthHud) return;

    if (std::isfinite(dt) && dt > 0.f && dt < 10.f) {
        g_frameIntervalsMs[g_frameIntervalCursor] = dt * 1000.f;
        g_frameIntervalCursor = (g_frameIntervalCursor + 1) % g_frameIntervalsMs.size();
        g_frameIntervalCount = std::min(g_frameIntervalCount + 1, g_frameIntervalsMs.size());
    }

    const float frameMs = (std::isfinite(dt) && dt > 0.f) ? dt * 1000.f : 0.f;
    const float loopFps = frameMs > 0.f ? 1000.f / frameMs : 0.f;

    float p99Ms = 0.f;
    if (g_frameIntervalCount) {
        auto samples = g_frameIntervalsMs;
        std::sort(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(g_frameIntervalCount));
        const auto rank = static_cast<std::size_t>(
            std::ceil(0.99 * static_cast<double>(g_frameIntervalCount)));
        p99Ms = samples[std::max<std::size_t>(1, rank) - 1];
    }
    const float lowFpsEstimate = p99Ms > 0.f ? 1000.f / p99Ms : 0.f;

    char text[256];
    std::snprintf(
        text, sizeof(text),
        "BISMUTH | LOOP %.1f FPS | %.2f ms\n"
        "1%% LOW* %.1f | PlayLayer CPU %.3f ms\n"
        "GPU usage: N/A | GPU time: N/A\n"
        "Draw calls: N/A | Samples: %zu/120",
        loopFps, frameMs, lowFpsEstimate, updateCpuMs, g_frameIntervalCount
    );
    g_bismuthHud->setString(text);
}
}

class $modify(BismuthPlayLayer, PlayLayer) {
    void onEnter() {
        PlayLayer::onEnter();
        g_frameIntervalCount = 0;
        g_frameIntervalCursor = 0;
        createHud(this);
    }

    void update(float dt) {
        const auto start = std::chrono::steady_clock::now();
        PlayLayer::update(dt);
        const auto end = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration<double, std::milli>(end - start).count();
        updateHud(dt, elapsed);
    }

    void onExit() {
        g_bismuthHud = nullptr;
        PlayLayer::onExit();
    }
};

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
    // Keep renderer hooks disabled in the Geode package until the pre-render
    // installation path and target binary have been validated on-device.
    log::info("Bismuth: Geode UI loaded in safe mode; native Cocos hooks are not installed.");
    if (Mod::get()->getSettingValue<bool>("diagnostics")) {
        log::info("Bismuth diagnostics: live HUD uses PlayLayer::update delta time; GPU and draw-call metrics are unavailable in this package.");
    }
}
