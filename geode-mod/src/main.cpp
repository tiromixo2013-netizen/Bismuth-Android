#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/EditorPauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <unistd.h>

using namespace geode::prelude;

namespace {
CCLabelTTF* g_bismuthHud = nullptr;
std::array<float, 120> g_frameIntervalsMs{};
std::array<double, 120> g_cpuUpdateMs{};
std::size_t g_cpuSampleCount = 0;
std::size_t g_cpuSampleCursor = 0;
double g_cpuP50Ms = 0.0, g_cpuP95Ms = 0.0, g_cpuP99Ms = 0.0;
std::size_t g_frameIntervalCount = 0;
std::size_t g_frameIntervalCursor = 0;
std::size_t g_updateCount = 0;
std::size_t g_hitchCount = 0;
float g_hudRefreshAccumulator = 0.f;
float g_cachedP50Ms = 0.f;
float g_cachedP95Ms = 0.f;
float g_cachedP99Ms = 0.f;

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

void refreshPercentiles() {
    if (!g_frameIntervalCount) return;
    auto samples = g_frameIntervalsMs;
    std::sort(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(g_frameIntervalCount));
    const auto percentile = [&](double p) {
        const auto rank = static_cast<std::size_t>(
            std::ceil(p * static_cast<double>(g_frameIntervalCount)));
        return samples[std::max<std::size_t>(1, rank) - 1];
    };
    g_cachedP50Ms = percentile(0.50);
    g_cachedP95Ms = percentile(0.95);
    g_cachedP99Ms = percentile(0.99);
    if (g_cpuSampleCount) {
        auto cpu = g_cpuUpdateMs;
        std::sort(cpu.begin(), cpu.begin() + static_cast<std::ptrdiff_t>(g_cpuSampleCount));
        const auto cpuPercentile = [&](double p) {
            const auto rank = static_cast<std::size_t>(std::ceil(p * static_cast<double>(g_cpuSampleCount)));
            return cpu[std::max<std::size_t>(1, rank) - 1];
        };
        g_cpuP50Ms = cpuPercentile(0.50);
        g_cpuP95Ms = cpuPercentile(0.95);
        g_cpuP99Ms = cpuPercentile(0.99);
    }
}

double readProcessRssMb() {
    // Linux /proc/self/statm reports resident pages for this process.
    // This is process RSS, not native-heap-only memory.
    FILE* statm = std::fopen("/proc/self/statm", "r");
    if (!statm) return -1.0;
    long totalPages = 0;
    long residentPages = 0;
    const int parsed = std::fscanf(statm, "%ld %ld", &totalPages, &residentPages);
    std::fclose(statm);
    const long pageSize = ::sysconf(_SC_PAGESIZE);
    if (parsed != 2 || residentPages < 0 || pageSize <= 0) return -1.0;
    return (static_cast<double>(residentPages) * static_cast<double>(pageSize)) / (1024.0 * 1024.0);
}

std::string makeFrameGraph() {
    constexpr std::size_t width = 24;
    std::string graph(width, '.');
    const auto available = std::min(g_frameIntervalCount, width);
    const auto start = (g_frameIntervalCursor + g_frameIntervalsMs.size() - available) % g_frameIntervalsMs.size();
    for (std::size_t i = 0; i < available; ++i) {
        const float ms = g_frameIntervalsMs[(start + i) % g_frameIntervalsMs.size()];
        graph[width - available + i] = ms < 16.7f ? '.' : (ms < 25.f ? '-' : (ms < 33.3f ? '=' : '#'));
    }
    return graph;
}

void createHud(CCNode* layer) {
    g_bismuthHud = nullptr;
    if (!Mod::get()->getSettingValue<bool>("hud-enabled")) return;

    // Use a standalone TTF label rather than BMFont: the recorded crash involved
    // CCLabelBMFont building sprite children through CCSpriteBatchNode::appendChild.
    auto label = CCLabelTTF::create("BISMUTH | collecting samples", "Arial", 10.f);
    if (!label) {
        log::warn("Bismuth: could not create the standalone telemetry label.");
        return;
    }

    label->setAnchorPoint({0.f, 1.f});
    label->setOpacity(225);
    label->setColor({220, 240, 255});
    const auto size = CCDirector::sharedDirector()->getWinSize();
    label->setPosition({8.f, size.height - 8.f});
    label->setZOrder(20000);
    label->setID("bismuth.android/live-hud"_spr);
    layer->addChild(label, 20000);
    g_bismuthHud = label;
}

void updateHud(float dt, double updateCpuMs) {
    if (!g_bismuthHud) return;

    if (std::isfinite(updateCpuMs) && updateCpuMs >= 0.0 && updateCpuMs < 10000.0) {
        g_cpuUpdateMs[g_cpuSampleCursor] = updateCpuMs;
        g_cpuSampleCursor = (g_cpuSampleCursor + 1) % g_cpuUpdateMs.size();
        g_cpuSampleCount = std::min(g_cpuSampleCount + 1, g_cpuUpdateMs.size());
    }

    if (std::isfinite(dt) && dt > 0.f && dt < 10.f) {
        const float frameMs = dt * 1000.f;
        g_frameIntervalsMs[g_frameIntervalCursor] = frameMs;
        g_frameIntervalCursor = (g_frameIntervalCursor + 1) % g_frameIntervalsMs.size();
        g_frameIntervalCount = std::min(g_frameIntervalCount + 1, g_frameIntervalsMs.size());
        if (frameMs > 33.3f) ++g_hitchCount;
        ++g_updateCount;
        if ((g_updateCount % 12u) == 0u) refreshPercentiles();
    }

    // Avoid rebuilding TTF textures on every game update; refresh at 4 Hz.
    g_hudRefreshAccumulator += std::max(0.f, dt);
    if (g_hudRefreshAccumulator < 0.25f) return;
    g_hudRefreshAccumulator = std::fmod(g_hudRefreshAccumulator, 0.25f);

    const float frameMs = (std::isfinite(dt) && dt > 0.f) ? dt * 1000.f : 0.f;
    const float loopFps = frameMs > 0.f ? 1000.f / frameMs : 0.f;
    const float lowFpsEstimate = g_cachedP99Ms > 0.f ? 1000.f / g_cachedP99Ms : 0.f;

    const double processRssMb = Mod::get()->getSettingValue<bool>("hud-memory") ? readProcessRssMb() : -1.0;
    char text[768];
    if (Mod::get()->getSettingValue<bool>("hud-advanced")) {
        if (Mod::get()->getSettingValue<bool>("hud-graph")) {
            const auto graph = makeFrameGraph();
            std::snprintf(text, sizeof(text),
                "BISMUTH | GAME LOOP %.1f FPS | %.2f ms\n"
                "1%% LOW* %.1f FPS | CPU update %.3f ms\n"
                "Frame p50/p95/p99: %.2f / %.2f / %.2f ms\n"
                "CPU p50/p95/p99: %.3f / %.3f / %.3f ms\n"
                "Hitches >33.3ms: %zu | Samples: %zu/120\n"
                "Process RSS: %s | GPU usage/time: N/A\n"
                "Draw calls: N/A",
                loopFps, frameMs, lowFpsEstimate, updateCpuMs,
                g_cachedP50Ms, g_cachedP95Ms, g_cachedP99Ms,
                g_cpuP50Ms, g_cpuP95Ms, g_cpuP99Ms,
                g_hitchCount, g_frameIntervalCount,
                processRssMb >= 0.0 ? (std::to_string(processRssMb) + " MB").c_str() : "N/A");
        } else {
            std::snprintf(text, sizeof(text),
                "BISMUTH | GAME LOOP %.1f FPS | %.2f ms\n"
                "1%% LOW* %.1f FPS | CPU update %.3f ms\n"
                "Frame p50/p95/p99: %.2f / %.2f / %.2f ms\n"
                "Hitches >33.3ms: %zu | Samples: %zu/120\n"
                "Process RSS: %s | GPU usage/time: N/A | Draw calls: N/A",
                loopFps, frameMs, lowFpsEstimate, updateCpuMs,
                g_cachedP50Ms, g_cachedP95Ms, g_cachedP99Ms,
                g_hitchCount, g_frameIntervalCount,
                processRssMb >= 0.0 ? (std::to_string(processRssMb) + " MB").c_str() : "N/A");
        }
    } else {
        std::snprintf(text, sizeof(text),
            "BISMUTH | LOOP %.1f FPS | %.2f ms\n"
            "1%% LOW* %.1f | CPU update %.3f ms | RSS %s",
            loopFps, frameMs, lowFpsEstimate, updateCpuMs,
            processRssMb >= 0.0 ? (std::to_string(processRssMb) + " MB").c_str() : "N/A");
    }
    g_bismuthHud->setString(text);
}
}

class $modify(BismuthPlayLayer, PlayLayer) {
    void onEnter() {
        PlayLayer::onEnter();
        g_frameIntervalsMs.fill(0.f);
        g_cpuUpdateMs.fill(0.0);
        g_cpuSampleCount = 0;
        g_cpuSampleCursor = 0;
        g_cpuP50Ms = g_cpuP95Ms = g_cpuP99Ms = 0.0;
        g_frameIntervalCount = 0;
        g_frameIntervalCursor = 0;
        g_updateCount = 0;
        g_hitchCount = 0;
        g_hudRefreshAccumulator = 0.f;
        g_cachedP50Ms = g_cachedP95Ms = g_cachedP99Ms = 0.f;
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
        log::info("Bismuth diagnostics: HUD reports PlayLayer update cadence and CPU wall time only; GPU and draw-call metrics remain unavailable.");
    }
}
