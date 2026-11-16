#include "bismuth/cocos_hooks.hpp"

#if defined(__ANDROID__) && defined(BISMUTH_ENABLE_COCOS_HOOKS)

#include <dobby.h>
#include <dlfcn.h>
#include <link.h>
#include <elf.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

namespace bismuth {
namespace {

constexpr char kTargetLibrary[] = "libcocos2dcpp.so";
constexpr char kExpectedBuildId[] = "ca621323d54c1d94f5f13a2778ce9c8baa31b9a8";
constexpr char kBatchDrawSymbol[] = "_ZN7cocos2d17CCSpriteBatchNode4drawEv";
constexpr char kUpdateQuadSymbol[] = "_ZN7cocos2d14CCTextureAtlas10updateQuadEPNS_19_ccV3F_C4B_T2F_QuadEj";

using BatchDrawFn = void (*)(void*);
using UpdateQuadFn = void (*)(void*, void*, unsigned int);

BatchDrawFn g_originalBatchDraw = nullptr;
UpdateQuadFn g_originalUpdateQuad = nullptr;
std::atomic<AtlasPassiveObserver*> g_observer{nullptr};
std::atomic<std::uint32_t> g_activeCallbacks{0};
std::mutex g_installMutex;
bool g_hooksInstalled = false;
bool g_updateQuadHooked = false;
bool g_batchDrawHooked = false;

const char* basenameOf(const char* path) {
    if (!path) return "";
    const char* slash = std::strrchr(path, '/');
    return slash ? slash + 1 : path;
}

std::string readBuildId(const dl_phdr_info* info) {
    for (ElfW(Half) i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr)& ph = info->dlpi_phdr[i];
        if (ph.p_type != PT_NOTE || ph.p_memsz < sizeof(ElfW(Nhdr))) continue;
        const auto* cursor = reinterpret_cast<const std::uint8_t*>(info->dlpi_addr + ph.p_vaddr);
        const auto* end = cursor + ph.p_memsz;
        while (static_cast<std::size_t>(end - cursor) >= sizeof(ElfW(Nhdr))) {
            const auto* note = reinterpret_cast<const ElfW(Nhdr)*>(cursor);
            cursor += sizeof(ElfW(Nhdr));
            const std::size_t nameSize = (note->n_namesz + 3u) & ~std::size_t(3u);
            const std::size_t descSize = (note->n_descsz + 3u) & ~std::size_t(3u);
            if (nameSize > static_cast<std::size_t>(end - cursor)) break;
            const auto* name = cursor;
            cursor += nameSize;
            if (descSize > static_cast<std::size_t>(end - cursor)) break;
            const auto* desc = cursor;
            cursor += descSize;
            if (note->n_type != NT_GNU_BUILD_ID || note->n_namesz < 3 ||
                std::memcmp(name, "GNU", 3) != 0) continue;
            static constexpr char hex[] = "0123456789abcdef";
            std::string result;
            result.reserve(note->n_descsz * 2u);
            for (std::uint32_t n = 0; n < note->n_descsz; ++n) {
                result.push_back(hex[desc[n] >> 4]);
                result.push_back(hex[desc[n] & 0x0f]);
            }
            return result;
        }
    }
    return {};
}

struct ModuleSearch {
    std::string path;
    std::string buildId;
    bool found = false;
};

int findTargetModule(dl_phdr_info* info, std::size_t, void* opaque) {
    auto* result = static_cast<ModuleSearch*>(opaque);
    if (std::strcmp(basenameOf(info->dlpi_name), kTargetLibrary) != 0) return 0;
    if (result->found) {
        result->found = false; // Reject ambiguous duplicate mappings.
        result->path.clear();
        result->buildId.clear();
        return 1;
    }
    result->found = true;
    result->path = info->dlpi_name ? info->dlpi_name : "";
    result->buildId = readBuildId(info);
    return 0;
}

bool symbolBelongsToTarget(void* symbol, const std::string& targetPath) {
    if (!symbol) return false;
    Dl_info info{};
    if (!dladdr(symbol, &info) || !info.dli_fname) return false;
    return std::strcmp(basenameOf(info.dli_fname), basenameOf(targetPath.c_str())) == 0;
}

AtlasPassiveObserver* acquireObserver() noexcept {
    g_activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
    return g_observer.load(std::memory_order_acquire);
}

void releaseObserver() noexcept {
    g_activeCallbacks.fetch_sub(1, std::memory_order_release);
}

void hookedBatchDraw(void* self) {
    AtlasPassiveObserver* observer = acquireObserver();
    if (observer) observer->recordBatchDraw(0);
    releaseObserver();
    if (g_originalBatchDraw) g_originalBatchDraw(self);
}

void hookedUpdateQuad(void* self, void* quad, unsigned int index) {
    if (g_originalUpdateQuad) g_originalUpdateQuad(self, quad, index);
    AtlasPassiveObserver* observer = acquireObserver();
    if (observer) observer->recordQuadWrite(true);
    releaseObserver();
}

} // namespace

bool installCocosPassiveHooks(AtlasPassiveObserver* observer, std::string* error) {
    if (!observer) {
        if (error) *error = "Observer handle is null";
        return false;
    }
    std::lock_guard<std::mutex> lock(g_installMutex);
    if (g_hooksInstalled) {
        AtlasPassiveObserver* current = g_observer.load(std::memory_order_acquire);
        if (current && current != observer) {
            if (error) *error = "Cocos hooks are already attached to another Bismuth instance";
            return false;
        }
        g_observer.store(observer, std::memory_order_release);
        return true;
    }

    ModuleSearch module;
    dl_iterate_phdr(findTargetModule, &module);
    if (!module.found || module.path.empty()) {
        if (error) *error = "libcocos2dcpp.so is not loaded or appears more than once";
        return false;
    }
    if (module.buildId != kExpectedBuildId) {
        if (error) *error = "Cocos2d build ID mismatch; refusing to patch an unknown game binary";
        return false;
    }

    void* batchDraw = dlsym(RTLD_DEFAULT, kBatchDrawSymbol);
    void* updateQuad = dlsym(RTLD_DEFAULT, kUpdateQuadSymbol);
    if (!symbolBelongsToTarget(batchDraw, module.path) ||
        !symbolBelongsToTarget(updateQuad, module.path)) {
        if (error) *error = "Required exported Cocos2d symbols could not be resolved from the verified module";
        return false;
    }

    if (!g_updateQuadHooked) {
        if (DobbyHook(updateQuad, reinterpret_cast<void*>(hookedUpdateQuad),
                      reinterpret_cast<void**>(&g_originalUpdateQuad)) != 0 ||
            !g_originalUpdateQuad) {
            g_originalUpdateQuad = nullptr;
            if (error) *error = "Dobby failed to install the CCTextureAtlas::updateQuad observer";
            return false;
        }
        g_updateQuadHooked = true;
    }

    if (!g_batchDrawHooked) {
        if (DobbyHook(batchDraw, reinterpret_cast<void*>(hookedBatchDraw),
                      reinterpret_cast<void**>(&g_originalBatchDraw)) != 0 ||
            !g_originalBatchDraw) {
            g_originalBatchDraw = nullptr;
            // If rollback fails, keep the updateQuad trampoline intact. Its
            // observer is still null until both hooks are installed.
            if (DobbyDestroy(updateQuad) == 0) {
                g_updateQuadHooked = false;
                g_originalUpdateQuad = nullptr;
            }
            if (error) *error = "Dobby failed to install the CCSpriteBatchNode::draw observer";
            return false;
        }
        g_batchDrawHooked = true;
    }

    g_observer.store(observer, std::memory_order_release);
    g_hooksInstalled = true;
    return true;
}

void detachCocosPassiveHooks(AtlasPassiveObserver* observer) noexcept {
    AtlasPassiveObserver* expected = observer;
    if (!g_observer.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel)) return;
    while (g_activeCallbacks.load(std::memory_order_acquire) != 0) std::this_thread::yield();
}

} // namespace bismuth

#else

namespace bismuth {
bool installCocosPassiveHooks(AtlasPassiveObserver*, std::string* error) {
    if (error) *error = "Cocos hooks are available only in the Android ARM64 build";
    return false;
}
void detachCocosPassiveHooks(AtlasPassiveObserver*) noexcept {}
} // namespace bismuth

#endif
