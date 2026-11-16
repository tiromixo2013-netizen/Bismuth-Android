#include "bismuth/cocos_hooks.hpp"

#if defined(__ANDROID__) && defined(BISMUTH_ENABLE_COCOS_HOOKS)

#include <dobby.h>
#include <dlfcn.h>
#include "bismuth/gles31_culler.hpp"
#include <GLES3/gl31.h>
#include <link.h>
#include <elf.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <limits>
#include <new>
#include <string>
#include <vector>
#include <thread>

namespace bismuth {
namespace {

constexpr char kTargetLibrary[] = "libcocos2dcpp.so";
constexpr char kExpectedBuildId[] = "ca621323d54c1d94f5f13a2778ce9c8baa31b9a8";
constexpr char kBatchDrawSymbol[] = "_ZN7cocos2d17CCSpriteBatchNode4drawEv";
constexpr char kUpdateQuadSymbol[] = "_ZN7cocos2d14CCTextureAtlas10updateQuadEPNS_19_ccV3F_C4B_T2F_QuadEj";
constexpr char kDrawNumberOfQuadsSymbol[] = "_ZN7cocos2d14CCTextureAtlas17drawNumberOfQuadsEjj";
constexpr char kRemoveAllQuadsSymbol[] = "_ZN7cocos2d14CCTextureAtlas14removeAllQuadsEv";
constexpr char kResizeCapacitySymbol[] = "_ZN7cocos2d14CCTextureAtlas14resizeCapacityEj";
constexpr char kInsertQuadSymbol[] = "_ZN7cocos2d14CCTextureAtlas10insertQuadEPNS_19_ccV3F_C4B_T2F_QuadEj";
constexpr char kInsertQuadsSymbol[] = "_ZN7cocos2d14CCTextureAtlas11insertQuadsEPNS_19_ccV3F_C4B_T2F_QuadEjj";
constexpr char kInsertQuadFromIndexSymbol[] = "_ZN7cocos2d14CCTextureAtlas19insertQuadFromIndexEjj";
constexpr char kRemoveQuadAtIndexSymbol[] = "_ZN7cocos2d14CCTextureAtlas17removeQuadAtIndexEj";
constexpr char kRemoveQuadsAtIndexSymbol[] = "_ZN7cocos2d14CCTextureAtlas18removeQuadsAtIndexEjj";
constexpr char kMoveQuadsThreeSymbol[] = "_ZN7cocos2d14CCTextureAtlas18moveQuadsFromIndexEjjj";
constexpr char kMoveQuadsTwoSymbol[] = "_ZN7cocos2d14CCTextureAtlas18moveQuadsFromIndexEjj";
constexpr char kIncreaseTotalQuadsSymbol[] = "_ZN7cocos2d14CCTextureAtlas22increaseTotalQuadsWithEj";

using BatchDrawFn = void (*)(void*);
using UpdateQuadFn = void (*)(void*, void*, unsigned int);
using DrawNumberOfQuadsFn = void (*)(void*, unsigned int, unsigned int);
using RemoveAllQuadsFn = void (*)(void*);
using ResizeCapacityFn = bool (*)(void*, unsigned int);
using InsertQuadFn = void (*)(void*, void*, unsigned int);
using InsertQuadsFn = void (*)(void*, void*, unsigned int, unsigned int);
using TwoIndexFn = void (*)(void*, unsigned int, unsigned int);
using OneIndexFn = void (*)(void*, unsigned int);
using ThreeIndexFn = void (*)(void*, unsigned int, unsigned int, unsigned int);
using IncreaseQuadsFn = void (*)(void*, unsigned int);

BatchDrawFn g_originalBatchDraw = nullptr;
UpdateQuadFn g_originalUpdateQuad = nullptr;
DrawNumberOfQuadsFn g_originalDrawNumberOfQuads = nullptr;
RemoveAllQuadsFn g_originalRemoveAllQuads = nullptr;
ResizeCapacityFn g_originalResizeCapacity = nullptr;
InsertQuadFn g_originalInsertQuad = nullptr;
InsertQuadsFn g_originalInsertQuads = nullptr;
TwoIndexFn g_originalInsertQuadFromIndex = nullptr;
OneIndexFn g_originalRemoveQuadAtIndex = nullptr;
TwoIndexFn g_originalRemoveQuadsAtIndex = nullptr;
ThreeIndexFn g_originalMoveQuadsThree = nullptr;
TwoIndexFn g_originalMoveQuadsTwo = nullptr;
IncreaseQuadsFn g_originalIncreaseTotalQuads = nullptr;
std::atomic<bool> g_gpuDrawBridgeEnabled{false};
std::atomic<AtlasPassiveObserver*> g_observer{nullptr};
std::atomic<std::uint32_t> g_activeCallbacks{0};
std::mutex g_installMutex;
bool g_hooksInstalled = false;
bool g_updateQuadHooked = false;
bool g_drawNumberOfQuadsHooked = false;
bool g_batchDrawHooked = false;
bool g_removeAllQuadsHooked = false;
bool g_resizeCapacityHooked = false;
bool g_insertQuadHooked = false;
bool g_insertQuadsHooked = false;
bool g_insertQuadFromIndexHooked = false;
bool g_removeQuadAtIndexHooked = false;
bool g_removeQuadsAtIndexHooked = false;
bool g_moveQuadsThreeHooked = false;
bool g_moveQuadsTwoHooked = false;
bool g_increaseTotalQuadsHooked = false;

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

bool tryGpuIndexedDraw(void* atlas, unsigned int quadCount, unsigned int start) {
    // Safety interlock: the current prototype does not yet extract calibrated
    // per-quad bounds in the game's clip-space. Sending synthetic full-screen
    // bounds through a replacement EBO is not a valid culling implementation
    // and can alter rendering on some drivers. Keep the original Cocos draw
    // path until real bounds, atlas layout, and device runtime tests are proven.
    (void)atlas;
    (void)quadCount;
    (void)start;
    return false;
}

void hookedDrawNumberOfQuads(void* self, unsigned int count, unsigned int start) {
    if (g_originalDrawNumberOfQuads)
        g_originalDrawNumberOfQuads(self, count, start);
    // Avoid reading private CCTextureAtlas offsets. The draw call may have
    // uploaded changed quad data, so invalidate the cached revision
    // conservatively whenever a non-empty atlas range is submitted.
    if (count != 0) {
        AtlasPassiveObserver* observer = acquireObserver();
        if (observer) observer->invalidateQuadData();
        releaseObserver();
    }
}

void hookedRemoveAllQuads(void* self) {
    if (g_originalRemoveAllQuads) g_originalRemoveAllQuads(self);
    AtlasPassiveObserver* observer = acquireObserver();
    if (observer) observer->recordStructuralMutation(true);
    releaseObserver();
}

void recordStructuralMutation() {
    AtlasPassiveObserver* observer = acquireObserver();
    if (observer) observer->recordStructuralMutation(true);
    releaseObserver();
}

void hookedInsertQuad(void* self, void* quad, unsigned int index) {
    if (g_originalInsertQuad) g_originalInsertQuad(self, quad, index);
    recordStructuralMutation();
}
void hookedInsertQuads(void* self, void* quads, unsigned int index, unsigned int amount) {
    if (g_originalInsertQuads) g_originalInsertQuads(self, quads, index, amount);
    recordStructuralMutation();
}
void hookedInsertQuadFromIndex(void* self, unsigned int from, unsigned int to) {
    if (g_originalInsertQuadFromIndex) g_originalInsertQuadFromIndex(self, from, to);
    recordStructuralMutation();
}
void hookedRemoveQuadAtIndex(void* self, unsigned int index) {
    if (g_originalRemoveQuadAtIndex) g_originalRemoveQuadAtIndex(self, index);
    recordStructuralMutation();
}
void hookedRemoveQuadsAtIndex(void* self, unsigned int index, unsigned int amount) {
    if (g_originalRemoveQuadsAtIndex) g_originalRemoveQuadsAtIndex(self, index, amount);
    recordStructuralMutation();
}
void hookedMoveQuadsThree(void* self, unsigned int from, unsigned int amount, unsigned int to) {
    if (g_originalMoveQuadsThree) g_originalMoveQuadsThree(self, from, amount, to);
    recordStructuralMutation();
}
void hookedMoveQuadsTwo(void* self, unsigned int from, unsigned int to) {
    if (g_originalMoveQuadsTwo) g_originalMoveQuadsTwo(self, from, to);
    recordStructuralMutation();
}
void hookedIncreaseTotalQuads(void* self, unsigned int amount) {
    if (g_originalIncreaseTotalQuads) g_originalIncreaseTotalQuads(self, amount);
    recordStructuralMutation();
}

bool hookedResizeCapacity(void* self, unsigned int requestedCapacity) {
    const bool result = g_originalResizeCapacity
        ? g_originalResizeCapacity(self, requestedCapacity) : false;
    // Capacity changes can relocate atlas storage. Avoid depending on private
    // object offsets; invalidate conservatively after every resize attempt.
    (void)requestedCapacity;
    AtlasPassiveObserver* observer = acquireObserver();
    if (observer) observer->recordStructuralMutation(true);
    releaseObserver();
    return result;
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
    void* drawNumberOfQuads = dlsym(RTLD_DEFAULT, kDrawNumberOfQuadsSymbol);
    void* removeAllQuads = dlsym(RTLD_DEFAULT, kRemoveAllQuadsSymbol);
    void* resizeCapacity = dlsym(RTLD_DEFAULT, kResizeCapacitySymbol);
    void* insertQuad = dlsym(RTLD_DEFAULT, kInsertQuadSymbol);
    void* insertQuads = dlsym(RTLD_DEFAULT, kInsertQuadsSymbol);
    void* insertQuadFromIndex = dlsym(RTLD_DEFAULT, kInsertQuadFromIndexSymbol);
    void* removeQuadAtIndex = dlsym(RTLD_DEFAULT, kRemoveQuadAtIndexSymbol);
    void* removeQuadsAtIndex = dlsym(RTLD_DEFAULT, kRemoveQuadsAtIndexSymbol);
    void* moveQuadsThree = dlsym(RTLD_DEFAULT, kMoveQuadsThreeSymbol);
    void* moveQuadsTwo = dlsym(RTLD_DEFAULT, kMoveQuadsTwoSymbol);
    void* increaseTotalQuads = dlsym(RTLD_DEFAULT, kIncreaseTotalQuadsSymbol);
    if (!symbolBelongsToTarget(batchDraw, module.path) ||
        !symbolBelongsToTarget(updateQuad, module.path) ||
        !symbolBelongsToTarget(drawNumberOfQuads, module.path) ||
        !symbolBelongsToTarget(removeAllQuads, module.path) ||
        !symbolBelongsToTarget(resizeCapacity, module.path) ||
        !symbolBelongsToTarget(insertQuad, module.path) ||
        !symbolBelongsToTarget(insertQuads, module.path) ||
        !symbolBelongsToTarget(insertQuadFromIndex, module.path) ||
        !symbolBelongsToTarget(removeQuadAtIndex, module.path) ||
        !symbolBelongsToTarget(removeQuadsAtIndex, module.path) ||
        !symbolBelongsToTarget(moveQuadsThree, module.path) ||
        !symbolBelongsToTarget(moveQuadsTwo, module.path) ||
        !symbolBelongsToTarget(increaseTotalQuads, module.path)) {
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

    if (!g_drawNumberOfQuadsHooked) {
        if (DobbyHook(drawNumberOfQuads, reinterpret_cast<void*>(hookedDrawNumberOfQuads),
                      reinterpret_cast<void**>(&g_originalDrawNumberOfQuads)) != 0 ||
            !g_originalDrawNumberOfQuads) {
            if (error) *error = "Dobby failed to install CCTextureAtlas::drawNumberOfQuads observer";
            return false;
        }
        g_drawNumberOfQuadsHooked = true;
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

    if (!g_removeAllQuadsHooked) {
        if (DobbyHook(removeAllQuads, reinterpret_cast<void*>(hookedRemoveAllQuads),
                      reinterpret_cast<void**>(&g_originalRemoveAllQuads)) != 0 ||
            !g_originalRemoveAllQuads) {
            g_originalRemoveAllQuads = nullptr;
            if (error) *error = "Dobby failed to install the CCTextureAtlas::removeAllQuads observer";
            return false;
        }
        g_removeAllQuadsHooked = true;
    }

    if (!g_resizeCapacityHooked) {
        if (DobbyHook(resizeCapacity, reinterpret_cast<void*>(hookedResizeCapacity),
                      reinterpret_cast<void**>(&g_originalResizeCapacity)) != 0 ||
            !g_originalResizeCapacity) {
            g_originalResizeCapacity = nullptr;
            if (error) *error = "Dobby failed to install the CCTextureAtlas::resizeCapacity observer";
            return false;
        }
        g_resizeCapacityHooked = true;
    }

    if (!g_insertQuadHooked) {
        if (DobbyHook(insertQuad, reinterpret_cast<void*>(hookedInsertQuad),
                      reinterpret_cast<void**>(&g_originalInsertQuad)) != 0 || !g_originalInsertQuad) {
            if (error) *error = "Dobby failed to install CCTextureAtlas::insertQuad observer";
            return false;
        }
        g_insertQuadHooked = true;
    }
    if (!g_insertQuadsHooked) {
        if (DobbyHook(insertQuads, reinterpret_cast<void*>(hookedInsertQuads),
                      reinterpret_cast<void**>(&g_originalInsertQuads)) != 0 || !g_originalInsertQuads) {
            if (error) *error = "Dobby failed to install CCTextureAtlas::insertQuads observer";
            return false;
        }
        g_insertQuadsHooked = true;
    }
    if (!g_insertQuadFromIndexHooked) {
        if (DobbyHook(insertQuadFromIndex, reinterpret_cast<void*>(hookedInsertQuadFromIndex),
                      reinterpret_cast<void**>(&g_originalInsertQuadFromIndex)) != 0 || !g_originalInsertQuadFromIndex) {
            if (error) *error = "Dobby failed to install CCTextureAtlas::insertQuadFromIndex observer";
            return false;
        }
        g_insertQuadFromIndexHooked = true;
    }
    if (!g_removeQuadAtIndexHooked) {
        if (DobbyHook(removeQuadAtIndex, reinterpret_cast<void*>(hookedRemoveQuadAtIndex),
                      reinterpret_cast<void**>(&g_originalRemoveQuadAtIndex)) != 0 || !g_originalRemoveQuadAtIndex) {
            if (error) *error = "Dobby failed to install CCTextureAtlas::removeQuadAtIndex observer";
            return false;
        }
        g_removeQuadAtIndexHooked = true;
    }
    if (!g_removeQuadsAtIndexHooked) {
        if (DobbyHook(removeQuadsAtIndex, reinterpret_cast<void*>(hookedRemoveQuadsAtIndex),
                      reinterpret_cast<void**>(&g_originalRemoveQuadsAtIndex)) != 0 || !g_originalRemoveQuadsAtIndex) {
            if (error) *error = "Dobby failed to install CCTextureAtlas::removeQuadsAtIndex observer";
            return false;
        }
        g_removeQuadsAtIndexHooked = true;
    }
    if (!g_moveQuadsThreeHooked) {
        if (DobbyHook(moveQuadsThree, reinterpret_cast<void*>(hookedMoveQuadsThree),
                      reinterpret_cast<void**>(&g_originalMoveQuadsThree)) != 0 || !g_originalMoveQuadsThree) {
            if (error) *error = "Dobby failed to install three-argument CCTextureAtlas::moveQuadsFromIndex observer";
            return false;
        }
        g_moveQuadsThreeHooked = true;
    }
    if (!g_moveQuadsTwoHooked) {
        if (DobbyHook(moveQuadsTwo, reinterpret_cast<void*>(hookedMoveQuadsTwo),
                      reinterpret_cast<void**>(&g_originalMoveQuadsTwo)) != 0 || !g_originalMoveQuadsTwo) {
            if (error) *error = "Dobby failed to install two-argument CCTextureAtlas::moveQuadsFromIndex observer";
            return false;
        }
        g_moveQuadsTwoHooked = true;
    }
    if (!g_increaseTotalQuadsHooked) {
        if (DobbyHook(increaseTotalQuads, reinterpret_cast<void*>(hookedIncreaseTotalQuads),
                      reinterpret_cast<void**>(&g_originalIncreaseTotalQuads)) != 0 || !g_originalIncreaseTotalQuads) {
            if (error) *error = "Dobby failed to install CCTextureAtlas::increaseTotalQuadsWith observer";
            return false;
        }
        g_increaseTotalQuadsHooked = true;
    }

    g_observer.store(observer, std::memory_order_release);
    g_hooksInstalled = true;
    return true;
}

void setGpuDrawBridgeEnabled(bool enabled) noexcept {
    g_gpuDrawBridgeEnabled.store(enabled, std::memory_order_release);
}

bool isGpuDrawBridgeEnabled() noexcept {
    return g_gpuDrawBridgeEnabled.load(std::memory_order_acquire);
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
void setGpuDrawBridgeEnabled(bool) noexcept {}
bool isGpuDrawBridgeEnabled() noexcept { return false; }
void detachCocosPassiveHooks(AtlasPassiveObserver*) noexcept {}
} // namespace bismuth

#endif
