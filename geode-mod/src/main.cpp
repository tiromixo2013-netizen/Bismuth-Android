#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/EditorPauseLayer.hpp>
#include <Geode/modify/CCNode.hpp>
#include <Geode/modify/CCSpriteBatchNode.hpp>

#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <arpa/inet.h>

using namespace geode::prelude;

namespace {
CCLabelTTF* g_bismuthHud = nullptr;
CCLabelTTF* g_bismuthFpsLabel = nullptr;
CCLabelTTF* g_bismuthCpuLabel = nullptr;
CCLabelTTF* g_bismuthGpuLabel = nullptr;
CCLabelTTF* g_bismuthRamLabel = nullptr;
CCLabelTTF* g_bismuthThermalLabel = nullptr;
CCLabelTTF* g_bismuthAdvancedLabel = nullptr;
CCLabelTTF* g_bismuthGraphLabel = nullptr;
CCLayerColor* g_bismuthCpuFill = nullptr;
CCLayerColor* g_bismuthGpuFill = nullptr;
CCLayerColor* g_bismuthHudPanel = nullptr;
std::string g_lastHudText;
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
std::atomic<std::uint64_t> g_batchDrawCallsCurrent{0};
std::atomic<std::uint64_t> g_batchChildrenCurrent{0};
std::atomic<std::uint64_t> g_batchCpuNsCurrent{0};
std::uint64_t g_batchDrawCallsLast = 0;
std::uint64_t g_batchChildrenLast = 0;
std::uint64_t g_batchCpuNsLast = 0;

bool g_loggedFirstPlayUpdate = false;

struct ShizukuSnapshot {
    double deviceCpu = -1.0, gpu = -1.0, cpuTemp = -1.0, gpuTemp = -1.0;
    double ramUsedMb = -1.0, ramTotalMb = -1.0;
};
ShizukuSnapshot g_shizukuSnapshot;
int g_shizukuSocket = -1;
bool g_shizukuConnecting = false;
bool g_shizukuConnected = false;
std::string g_shizukuRx;
std::string g_shizukuLastError;
auto g_shizukuNextPoll = std::chrono::steady_clock::time_point{};
auto g_shizukuLastSample = std::chrono::steady_clock::time_point{};
auto g_shizukuConnectStarted = std::chrono::steady_clock::time_point{};

void closeShizukuSocket() {
    if (g_shizukuSocket >= 0) ::close(g_shizukuSocket);
    g_shizukuSocket = -1;
    g_shizukuConnecting = false;
    g_shizukuConnected = false;
    g_shizukuRx.clear();
    // Never leave stale device telemetry visible after a disconnect or opt-out.
    g_shizukuSnapshot = ShizukuSnapshot{};
}

void parseShizukuLine(const std::string& line) {
    std::istringstream in(line);
    std::array<double, 6> values{};
    for (std::size_t i = 0; i < values.size(); ++i) {
        std::string field;
        if (!std::getline(in, field, ',')) return;
        try {
            std::size_t parsed = 0;
            values[i] = std::stod(field, &parsed);
            if (parsed != field.size() || !std::isfinite(values[i])) return;
        } catch (...) { return; }
    }
    // Reject extra columns and impossible values; -1 is the protocol's N/A sentinel.
    std::string extra;
    if (std::getline(in, extra, ',')) return;
    const auto validPercent = [](double v) { return v == -1.0 || (v >= 0.0 && v <= 100.0); };
    const auto validTemp = [](double v) { return v == -1.0 || (v >= 0.0 && v <= 150.0); };
    const auto validMemory = [](double v) { return v == -1.0 || v >= 0.0; };
    if (!validPercent(values[0]) || !validPercent(values[1]) ||
        !validTemp(values[2]) || !validTemp(values[3]) ||
        !validMemory(values[4]) || !validMemory(values[5])) return;
    if (values[4] >= 0.0 && values[5] >= 0.0 && values[4] > values[5]) return;
    g_shizukuSnapshot.deviceCpu = values[0];
    g_shizukuSnapshot.gpu = values[1];
    g_shizukuSnapshot.cpuTemp = values[2];
    g_shizukuSnapshot.gpuTemp = values[3];
    g_shizukuSnapshot.ramUsedMb = values[4];
    g_shizukuSnapshot.ramTotalMb = values[5];
    g_shizukuConnected = true;
    g_shizukuLastSample = std::chrono::steady_clock::now();
    g_shizukuLastError.clear();
}

void pollShizukuBridge() {
    if (!Mod::get()->getSettingValue<bool>("shizuku-bridge-enabled")) {
        if (g_shizukuSocket >= 0) closeShizukuSocket();
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now < g_shizukuNextPoll) return;
    g_shizukuNextPoll = now + std::chrono::milliseconds(250);
    if (g_shizukuConnected && now - g_shizukuLastSample > std::chrono::seconds(2)) {
        closeShizukuSocket();
        g_shizukuLastError = "telemetry timeout after 2s";
    }

    if (g_shizukuSocket < 0) {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) { g_shizukuLastError = std::string("socket: ") + std::strerror(errno); return; }
        const int flags = ::fcntl(fd, F_GETFL, 0);
        if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
            g_shizukuLastError = std::string("fcntl: ") + std::strerror(errno);
            ::close(fd);
            return;
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(38471);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        const int result = ::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
        if (result < 0 && errno != EINPROGRESS) {
            g_shizukuLastError = std::string("connect: ") + std::strerror(errno);
            ::close(fd); return;
        }
        g_shizukuSocket = fd;
        g_shizukuConnecting = result < 0;
        g_shizukuConnectStarted = now;
        if (!g_shizukuConnecting) g_shizukuLastError.clear();
    }

    if (g_shizukuConnecting) {
        pollfd pfd{g_shizukuSocket, static_cast<short>(POLLOUT | POLLERR | POLLHUP), 0};
        const int ready = ::poll(&pfd, 1, 0);
        if (ready < 0) {
            if (errno == EINTR) return;
            g_shizukuLastError = std::string("poll: ") + std::strerror(errno);
            closeShizukuSocket(); return;
        }
        if (ready == 0) {
            if (now - g_shizukuConnectStarted > std::chrono::seconds(3)) {
                g_shizukuLastError = "connect timeout after 3s";
                closeShizukuSocket();
            }
            return;
        }
        int error = 0; socklen_t length = sizeof(error);
        if (::getsockopt(g_shizukuSocket, SOL_SOCKET, SO_ERROR, &error, &length) < 0 || error) {
            const int code = error ? error : errno;
            g_shizukuLastError = std::string("connect: ") + std::strerror(code);
            closeShizukuSocket(); return;
        }
        g_shizukuLastError.clear();
        g_shizukuConnecting = false;
    }

    char buffer[512];
    for (;;) {
        const auto count = ::recv(g_shizukuSocket, buffer, sizeof(buffer), 0);
        if (count > 0) {
            g_shizukuRx.append(buffer, static_cast<std::size_t>(count));
            if (g_shizukuRx.size() > 4096) g_shizukuRx.erase(0, g_shizukuRx.size() - 2048);
            std::size_t newline = 0;
            while ((newline = g_shizukuRx.find('\n')) != std::string::npos) {
                parseShizukuLine(g_shizukuRx.substr(0, newline));
                g_shizukuRx.erase(0, newline + 1);
            }
            continue;
        }
        if (count == 0) {
            closeShizukuSocket();
            g_shizukuLastError = "server closed connection";
        } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            g_shizukuLastError = std::string("recv: ") + std::strerror(errno);
            closeShizukuSocket();
        }
        break;
    }
}

double sampleProcessCpuPercent() {
    static std::uint64_t previousTicks = 0;
    static auto previousTime = std::chrono::steady_clock::now();
    std::ifstream input("/proc/self/stat");
    std::string line;
    if (!std::getline(input, line)) return -1.0;
    const auto close = line.rfind(')');
    if (close == std::string::npos) return -1.0;
    std::istringstream fields(line.substr(close + 2));
    std::string token;
    std::uint64_t user = 0, system = 0;
    for (int i = 0; i <= 12; ++i) {
        if (!(fields >> token)) return -1.0;
        if (i == 11) { try { user = std::stoull(token); } catch (...) { return -1.0; } }
        if (i == 12) { try { system = std::stoull(token); } catch (...) { return -1.0; } }
    }
    const auto now = std::chrono::steady_clock::now();
    if (previousTicks == 0) { previousTicks = user + system; previousTime = now; return -1.0; }
    const double seconds = std::chrono::duration<double>(now - previousTime).count();
    const long hz = ::sysconf(_SC_CLK_TCK);
    const long cores = ::sysconf(_SC_NPROCESSORS_ONLN);
    const std::uint64_t ticks = user + system;
    const std::uint64_t delta = ticks >= previousTicks ? ticks - previousTicks : 0;
    previousTicks = ticks;
    previousTime = now;
    if (seconds <= 0.0 || hz <= 0 || cores <= 0) return -1.0;
    return std::clamp((static_cast<double>(delta) / hz) / seconds / cores * 100.0, 0.0, 100.0);
}

double sampleGpuPercent() {
    // Prefer cumulative devfreq counters exposed by Mali drivers. Never treat GPU frequency as load.
    static std::string path;
    static std::uint64_t lastBusy = 0, lastTotal = 0;
    if (path.empty()) {
        DIR* dir = ::opendir("/sys/class/devfreq");
        if (!dir) return -1.0;
        while (auto* entry = ::readdir(dir)) {
            if (entry->d_name[0] == '.') continue;
            const std::string base = std::string("/sys/class/devfreq/") + entry->d_name + "/";
            std::ifstream nameFile(base + "name");
            std::string name; std::getline(nameFile, name);
            name += " "; name += entry->d_name;
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            if (name.find("gpu") == std::string::npos && name.find("mali") == std::string::npos) continue;
            std::ifstream b(base + "busy_time"), t(base + "total_time");
            std::uint64_t bv = 0, tv = 0;
            if (b >> bv && t >> tv) { path = base; break; }
        }
        ::closedir(dir);
    }
    if (path.empty()) return -1.0;
    std::ifstream b(path + "busy_time"), t(path + "total_time");
    std::uint64_t busy = 0, total = 0;
    if (!(b >> busy) || !(t >> total)) { path.clear(); lastBusy = lastTotal = 0; return -1.0; }
    if (!lastTotal || total <= lastTotal || busy < lastBusy) { lastBusy = busy; lastTotal = total; return -1.0; }
    const auto db = busy - lastBusy, dt = total - lastTotal;
    lastBusy = busy; lastTotal = total;
    return dt ? std::clamp(static_cast<double>(db) / static_cast<double>(dt) * 100.0, 0.0, 100.0) : -1.0;
}

double readThermalZoneC(const char* wanted) {
    DIR* dir = ::opendir("/sys/class/thermal");
    if (!dir) return -1.0;
    double hottest = -1.0;
    while (auto* entry = ::readdir(dir)) {
        if (std::string(entry->d_name).find("thermal_zone") != 0) continue;
        const std::string base = std::string("/sys/class/thermal/") + entry->d_name + "/";
        std::ifstream typeFile(base + "type");
        std::string type;
        if (!std::getline(typeFile, type)) continue;
        std::transform(type.begin(), type.end(), type.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (type.find(wanted) == std::string::npos) continue;
        std::ifstream tempFile(base + "temp");
        long milliC = 0;
        if (tempFile >> milliC && milliC > 0 && milliC < 150000)
            hottest = std::max(hottest, static_cast<double>(milliC) / 1000.0);
    }
    ::closedir(dir);
    return hottest;
}

double readEngineTargetFps() {
    auto* director = CCDirector::sharedDirector();
    if (!director) return -1.0;
    const double interval = director->getAnimationInterval();
    return interval > 0.0 ? 1.0 / interval : -1.0;
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

void createHud(CCNode* controller) {
    if (!Mod::get()->getSettingValue<bool>("hud-enabled") || !controller) return;
    const auto size = CCDirector::sharedDirector()->getWinSize();

    // Compact cool-white, high-contrast HUD pinned to the upper-left corner.
    auto panel = CCLayerColor::create({226, 239, 246, 244}, 210.f, 158.f);
    if (!panel) return;
    panel->setAnchorPoint({0.f, 1.f});
    panel->setPosition({10.f, size.height - 10.f});
    panel->setID("bismuth.android/performance-overlay"_spr);
    controller->addChild(panel, 1);
    g_bismuthHudPanel = panel;

    auto addLabel = [&](const char* value, float y, float fontSize, ccColor3B color) -> CCLabelTTF* {
        auto* label = CCLabelTTF::create(value, "Roboto", fontSize);
        if (!label) return nullptr;
        label->setAnchorPoint({0.f, 0.5f});
        label->setPosition({8.f, y});
        label->setColor(color);
        panel->addChild(label);
        return label;
    };

    addLabel("BISMUTH  •  LIVE", 146.f, 8.f, {45, 91, 115});
    g_bismuthFpsLabel = addLabel("FPS --", 126.f, 17.f, {20, 112, 82});
    g_bismuthHud = addLabel("FRAME -- ms", 109.f, 8.f, {35, 52, 65});
    g_bismuthCpuLabel = addLabel("CPU  --", 92.f, 8.f, {30, 77, 105});
    g_bismuthGpuLabel = addLabel("GPU  N/A", 71.f, 8.f, {75, 62, 125});
    g_bismuthRamLabel = addLabel("RAM  N/A", 49.f, 7.5f, {35, 52, 65});
    g_bismuthThermalLabel = addLabel("TEMP  CPU N/A  GPU N/A", 34.f, 7.5f, {35, 52, 65});
    g_bismuthAdvancedLabel = addLabel("P95 N/A  |  1% low N/A", 19.f, 7.f, {45, 65, 78});
    g_bismuthGraphLabel = addLabel("", 6.f, 6.f, {37, 94, 105});

    auto makeBar = [&](float y, ccColor4B fillColor, CCLayerColor*& fill) {
        auto* bg = CCLayerColor::create({185, 202, 211, 255}, 194.f, 3.f);
        if (!bg) return;
        bg->setAnchorPoint({0.f, 0.5f});
        bg->setPosition({8.f, y});
        panel->addChild(bg);
        fill = CCLayerColor::create(fillColor, 1.f, 3.f);
        if (fill) {
            fill->setAnchorPoint({0.f, 0.5f});
            bg->addChild(fill);
        }
    };
    makeBar(84.f, {35, 164, 125, 255}, g_bismuthCpuFill);
    makeBar(63.f, {112, 94, 190, 255}, g_bismuthGpuFill);

    g_lastHudText.clear();
    log::info("BISMUTH HUD initialized: compact cool-white top-left metrics panel.");
}

void syncHud(CCNode* controller) {
    const bool enabled = Mod::get()->getSettingValue<bool>("hud-enabled");
    if (!enabled) {
        if (g_bismuthHudPanel) {
            auto* oldPanel = g_bismuthHudPanel;
            g_bismuthHudPanel = nullptr;
            g_bismuthHud = nullptr;
            g_bismuthFpsLabel = nullptr;
            g_bismuthCpuLabel = nullptr;
            g_bismuthGpuLabel = nullptr;
            g_bismuthRamLabel = nullptr;
            g_bismuthThermalLabel = nullptr;
            g_bismuthAdvancedLabel = nullptr;
            g_bismuthGraphLabel = nullptr;
            g_bismuthCpuFill = nullptr;
            g_bismuthGpuFill = nullptr;
            g_lastHudText.clear();
            oldPanel->removeFromParentAndCleanup(true);
        }
        return;
    }
    if (!g_bismuthHudPanel && controller) createHud(controller);
}

void updateHud(float dt, double updateCpuMs) {
    if (!g_bismuthHud || !g_bismuthHudPanel || !g_bismuthFpsLabel) return;

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
    double averageFrameMs = 0.0;
    if (g_frameIntervalCount) {
        for (std::size_t i = 0; i < g_frameIntervalCount; ++i) averageFrameMs += g_frameIntervalsMs[i];
        averageFrameMs /= static_cast<double>(g_frameIntervalCount);
    }
    const double averageFps = averageFrameMs > 0.0 ? 1000.0 / averageFrameMs : 0.0;
    const double targetFps = readEngineTargetFps();
    const double gameCpuPercent = Mod::get()->getSettingValue<bool>("monitor-cpu") ? sampleProcessCpuPercent() : -1.0;
    double gpuPercent = Mod::get()->getSettingValue<bool>("monitor-gpu") ? sampleGpuPercent() : -1.0;
    const bool thermalEnabled = Mod::get()->getSettingValue<bool>("monitor-thermal");
    double cpuTempC = thermalEnabled ? readThermalZoneC("cpu") : -1.0;
    double gpuTempC = thermalEnabled ? readThermalZoneC("gpu") : -1.0;
    if (gpuPercent < 0.0) gpuPercent = g_shizukuSnapshot.gpu;
    if (cpuTempC < 0.0) cpuTempC = g_shizukuSnapshot.cpuTemp;
    if (gpuTempC < 0.0) gpuTempC = g_shizukuSnapshot.gpuTemp;


    const double processRssMb = Mod::get()->getSettingValue<bool>("hud-memory") ? readProcessRssMb() : -1.0;
    const bool advanced = Mod::get()->getSettingValue<bool>("hud-advanced");
    const bool graphEnabled = advanced && Mod::get()->getSettingValue<bool>("hud-graph");
    const bool rendererEnabled = Mod::get()->getSettingValue<bool>("renderer-telemetry");
    const double batchCpuMs = static_cast<double>(g_batchCpuNsLast) / 1000000.0;
    std::ostringstream fpsText;
    fpsText.setf(std::ios::fixed);
    fpsText.precision(0);
    fpsText << "FPS " << loopFps;
    g_bismuthFpsLabel->setString(fpsText.str().c_str());

    std::ostringstream frameOut;
    frameOut.setf(std::ios::fixed);
    frameOut.precision(1);
    frameOut << "FRAME " << frameMs << " ms";
    g_bismuthHud->setString(frameOut.str().c_str());

    const double cpuShown = gameCpuPercent >= 0.0 ? gameCpuPercent : g_shizukuSnapshot.deviceCpu;
    if (g_bismuthCpuLabel) {
        std::ostringstream value;
        value.setf(std::ios::fixed); value.precision(0);
        if (cpuShown >= 0.0) value << "CPU  " << cpuShown << "%";
        else value << "CPU  N/A";
        g_bismuthCpuLabel->setString(value.str().c_str());
    }
    if (g_bismuthGpuLabel) {
        std::ostringstream value;
        value.setf(std::ios::fixed); value.precision(0);
        if (gpuPercent >= 0.0) value << "GPU  " << gpuPercent << "%";
        else value << "GPU  N/A";
        g_bismuthGpuLabel->setString(value.str().c_str());
    }

    const double systemRamUsed = g_shizukuSnapshot.ramUsedMb;
    const double systemRamTotal = g_shizukuSnapshot.ramTotalMb;
    if (g_bismuthRamLabel) {
        std::ostringstream value;
        value.setf(std::ios::fixed); value.precision(1);
        value << "RAM ";
        if (processRssMb >= 0.0) value << "APP " << processRssMb / 1024.0 << "G";
        else value << "APP N/A";
        if (systemRamUsed >= 0.0 && systemRamTotal > 0.0)
            value << "  SYS " << systemRamUsed / 1024.0 << "/" << systemRamTotal / 1024.0 << "G";
        g_bismuthRamLabel->setString(value.str().c_str());
    }

    if (g_bismuthThermalLabel) {
        std::ostringstream value;
        value.setf(std::ios::fixed); value.precision(0);
        value << "TEMP  CPU ";
        if (cpuTempC >= 0.0) value << cpuTempC << "C"; else value << "N/A";
        value << "  GPU ";
        if (gpuTempC >= 0.0) value << gpuTempC << "C"; else value << "N/A";
        g_bismuthThermalLabel->setString(value.str().c_str());
    }

    if (g_bismuthAdvancedLabel) {
        std::ostringstream value;
        value.setf(std::ios::fixed); value.precision(1);
        if (advanced && g_frameIntervalCount) {
            value << "P95 " << g_cachedP95Ms << "ms  |  1% low ";
            if (lowFpsEstimate > 0.0) value << std::fixed << std::setprecision(0) << lowFpsEstimate;
            else value << "N/A";
        } else {
            value << "ADVANCED OFF";
        }
        g_bismuthAdvancedLabel->setString(value.str().c_str());
    }

    if (g_bismuthGraphLabel) {
        const std::string graph = graphEnabled ? makeFrameGraph() : std::string("GRAPH OFF");
        g_bismuthGraphLabel->setString(graph.c_str());
    }

    const auto setFill = [](CCLayerColor* fill, double percent) {
        if (!fill) return;
        const float width = percent >= 0.0 ? static_cast<float>(194.0 * std::clamp(percent, 0.0, 100.0) / 100.0) : 1.f;
        fill->setContentSize({std::max(1.f, width), 3.f});
        fill->setVisible(percent >= 0.0);
    };
    setFill(g_bismuthCpuFill, cpuShown);
    setFill(g_bismuthGpuFill, gpuPercent);
    const std::string frameText = frameOut.str();

}
}

class BismuthHudController final : public CCNode {
public:
    static BismuthHudController* create() {
        auto* ret = new BismuthHudController();
        if (ret && ret->init()) { ret->autorelease(); return ret; }
        delete ret;
        return nullptr;
    }

    bool init() override {
        if (!CCNode::init()) return false;
        scheduleUpdate();
        return true;
    }

    ~BismuthHudController() override {
        g_bismuthHudPanel = nullptr;
        g_bismuthHud = nullptr;
        g_bismuthFpsLabel = nullptr;
        g_bismuthCpuLabel = nullptr;
        g_bismuthGpuLabel = nullptr;
        g_bismuthRamLabel = nullptr;
        g_bismuthThermalLabel = nullptr;
        g_bismuthAdvancedLabel = nullptr;
        g_bismuthGraphLabel = nullptr;
        g_bismuthCpuFill = nullptr;
        g_bismuthGpuFill = nullptr;
        g_lastHudText.clear();
    }

    void update(float dt) override {
        const auto start = std::chrono::steady_clock::now();
        g_batchDrawCallsLast = g_batchDrawCallsCurrent.exchange(0, std::memory_order_acq_rel);
        g_batchChildrenLast = g_batchChildrenCurrent.exchange(0, std::memory_order_acq_rel);
        g_batchCpuNsLast = g_batchCpuNsCurrent.exchange(0, std::memory_order_acq_rel);
        pollShizukuBridge();
        syncHud(this);
        if (g_bismuthHudPanel) updateHud(dt, m_lastWorkMs);
        const auto end = std::chrono::steady_clock::now();
        m_lastWorkMs = std::chrono::duration<double, std::milli>(end - start).count();
    }

private:
    double m_lastWorkMs = 0.0;
};

class $modify(BismuthSceneNode, CCNode) {
    void onEnter() {
        CCNode::onEnter();

        // PlayLayer inherits this lifecycle method from CCNode. Hook the class
        // that actually owns the method, then filter to gameplay layers only.
        auto* playLayer = typeinfo_cast<PlayLayer*>(this);
        if (!playLayer) return;

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
        g_batchDrawCallsCurrent.store(0, std::memory_order_relaxed);
        g_batchChildrenCurrent.store(0, std::memory_order_relaxed);
        g_batchCpuNsCurrent.store(0, std::memory_order_relaxed);
        g_batchDrawCallsLast = g_batchChildrenLast = g_batchCpuNsLast = 0;
        g_loggedFirstPlayUpdate = false;

        auto* director = CCDirector::sharedDirector();
        auto* scene = director ? director->getRunningScene() : nullptr;
        if (scene) {
            auto* controller = BismuthHudController::create();
            if (controller) scene->addChild(controller, 1000000);
            else log::error("Bismuth: failed to create scene HUD controller.");
        } else {
            log::error("Bismuth: no running scene for HUD controller.");
        }
        log::info("Bismuth: PlayLayer entered through CCNode lifecycle hook; scene-owned HUD controller initialized.");
    }

    void onExit() {
        if (typeinfo_cast<PlayLayer*>(this)) {
            closeShizukuSocket();
        }
        CCNode::onExit();
    }
};
class $modify(BismuthSpriteBatchNode, CCSpriteBatchNode) {
    void draw() {
        if (!Mod::get()->getSettingValue<bool>("renderer-telemetry")) {
            CCSpriteBatchNode::draw();
            return;
        }
        const auto start = std::chrono::steady_clock::now();
        CCSpriteBatchNode::draw();
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start).count();
        auto* children = getChildren();
        const auto childCount = children ? static_cast<std::uint64_t>(children->count()) : 0u;
        g_batchDrawCallsCurrent.fetch_add(1, std::memory_order_relaxed);
        g_batchChildrenCurrent.fetch_add(childCount, std::memory_order_relaxed);
        if (elapsed > 0)
            g_batchCpuNsCurrent.fetch_add(static_cast<std::uint64_t>(elapsed), std::memory_order_relaxed);
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
    // Register safe Geode-level telemetry hooks; no draw replacement or GPU culling is enabled.
    log::info("Bismuth: Geode mod loaded; PlayLayer entry lifecycle and CCSpriteBatchNode draw observer are registered.");
    if (Mod::get()->getSettingValue<bool>("diagnostics")) {
        log::info("Bismuth diagnostics: HUD reports PlayLayer cadence, CPU update time, SpriteBatch draw invocations/children and batch CPU submission time. GPU execution time and raw GL draw calls are not measured.");
    }
}
