#include "bismuth/gpu_timer_query.hpp"

#if defined(__ANDROID__)
#include <GLES3/gl3.h>
#include <EGL/egl.h>
#include <algorithm>
#include <cstring>
#include <iterator>

namespace bismuth {
namespace {
constexpr std::uint32_t kTimeElapsedExt = 0x88BF;
constexpr std::uint32_t kQueryResultExt = 0x8866;
constexpr std::uint32_t kCurrentQueryExt = 0x8865;
constexpr std::uint32_t kQueryResultAvailableExt = 0x8867;
constexpr std::uint32_t kGpuDisjointExt = 0x8FBB;

bool hasExtension(const char* list, const char* wanted) noexcept {
    if (!list || !wanted || !*wanted) return false;
    const std::size_t wantedLength = std::strlen(wanted);
    const char* cursor = list;
    while ((cursor = std::strstr(cursor, wanted)) != nullptr) {
        const bool left = cursor == list || cursor[-1] == ' ';
        const char rightChar = cursor[wantedLength];
        const bool right = rightChar == '\0' || rightChar == ' ';
        if (left && right) return true;
        cursor += wantedLength;
    }
    return false;
}

template <typename T>
T loadProc(const char* name) noexcept {
    return reinterpret_cast<T>(eglGetProcAddress(name));
}
} // namespace

bool GlesGpuTimerQuery::loadFunctions() noexcept {
    genQueries_ = loadProc<GenQueriesFn>("glGenQueriesEXT");
    deleteQueries_ = loadProc<DeleteQueriesFn>("glDeleteQueriesEXT");
    beginQuery_ = loadProc<BeginQueryFn>("glBeginQueryEXT");
    getQueryiv_ = loadProc<GetQueryivFn>("glGetQueryivEXT");
    endQuery_ = loadProc<EndQueryFn>("glEndQueryEXT");
    getQueryObjectuiv_ = loadProc<GetQueryObjectuivFn>("glGetQueryObjectuivEXT");
    getQueryObjectui64v_ = loadProc<GetQueryObjectui64vFn>("glGetQueryObjectui64vEXT");
    return genQueries_ && deleteQueries_ && beginQuery_ && getQueryiv_ && endQuery_ &&
           getQueryObjectuiv_ && getQueryObjectui64v_;
}

bool GlesGpuTimerQuery::initialize() noexcept {
    if (initialized_) return supported_.load(std::memory_order_acquire);
    initialized_ = true;
    const char* extensions = reinterpret_cast<const char*>(glGetString(0x1F03u /* GL_EXTENSIONS */));
    if (!hasExtension(extensions, "GL_EXT_disjoint_timer_query") || !loadFunctions())
        return false;
    genQueries_(static_cast<std::int32_t>(kQueryCount), queries_);
    for (std::uint32_t query : queries_) {
        if (!query) {
            if (deleteQueries_) deleteQueries_(static_cast<std::int32_t>(kQueryCount), queries_);
            std::fill(std::begin(queries_), std::end(queries_), 0u);
            return false;
        }
    }
    supported_.store(true, std::memory_order_release);
    return true;
}

bool GlesGpuTimerQuery::begin() noexcept {
    if (!initialize() || active_) return false;
    std::int32_t currentQuery = 0;
    getQueryiv_(kTimeElapsedExt, kCurrentQueryExt, &currentQuery);
    if (currentQuery != 0) {
        skippedSamples_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    for (std::uint32_t attempt = 0; attempt < kQueryCount; ++attempt) {
        const std::uint32_t slot = (nextSlot_ + attempt) % kQueryCount;
        if (pending_[slot]) continue;
        beginQuery_(kTimeElapsedExt, queries_[slot]);
        activeSlot_ = slot;
        active_ = true;
        nextSlot_ = (slot + 1u) % kQueryCount;
        return true;
    }
    skippedSamples_.fetch_add(1, std::memory_order_relaxed);
    return false;
}

void GlesGpuTimerQuery::end() noexcept {
    if (!active_) return;
    endQuery_(kTimeElapsedExt);
    pending_[activeSlot_] = true;
    active_ = false;
}

void GlesGpuTimerQuery::recordSample(std::uint64_t ns) noexcept {
    if (sampleCount_ == kWindowSize)
        sampleTotal_ -= samples_[sampleCursor_];
    else
        ++sampleCount_;
    samples_[sampleCursor_] = ns;
    sampleTotal_ += ns;
    sampleCursor_ = (sampleCursor_ + 1u) % kWindowSize;

    std::uint64_t sorted[kWindowSize]{};
    std::copy(samples_, samples_ + sampleCount_, sorted);
    std::sort(sorted, sorted + sampleCount_);
    const auto percentile = [this, &sorted](std::uint32_t p) noexcept -> std::uint64_t {
        if (!sampleCount_) return 0;
        const std::uint32_t index = ((sampleCount_ - 1u) * p + 99u) / 100u;
        return sorted[index];
    };
    lastGpuNs_.store(ns, std::memory_order_relaxed);
    meanGpuNs_.store(sampleTotal_ / sampleCount_, std::memory_order_relaxed);
    p95GpuNs_.store(percentile(95), std::memory_order_relaxed);
    p99GpuNs_.store(percentile(99), std::memory_order_relaxed);
    publishedSampleCount_.store(sampleCount_, std::memory_order_release);
}

void GlesGpuTimerQuery::poll() noexcept {
    if (!initialize()) return;
    std::int32_t disjoint = 0;
    glGetIntegerv(kGpuDisjointExt, &disjoint);
    if (disjoint != 0) {
        for (std::uint32_t slot = 0; slot < kQueryCount; ++slot)
            if (pending_[slot]) discardPending_[slot] = true;
        disjointEvents_.fetch_add(1, std::memory_order_relaxed);
    }
    for (std::uint32_t slot = 0; slot < kQueryCount; ++slot) {
        if (!pending_[slot]) continue;
        std::uint32_t available = 0;
        getQueryObjectuiv_(queries_[slot], kQueryResultAvailableExt, &available);
        if (!available) continue; // Never wait for the GPU.
        std::uint64_t elapsed = 0;
        if (!discardPending_[slot]) {
            getQueryObjectui64v_(queries_[slot], kQueryResultExt, &elapsed);
            if (elapsed != 0) recordSample(static_cast<std::uint64_t>(elapsed));
        }
        pending_[slot] = false;
        discardPending_[slot] = false;
    }
}

GpuTimingSnapshot GlesGpuTimerQuery::snapshot() const noexcept {
    GpuTimingSnapshot result{};
    result.supported = supported_.load(std::memory_order_acquire);
    result.sampleCount = publishedSampleCount_.load(std::memory_order_acquire);
    result.lastGpuNs = lastGpuNs_.load(std::memory_order_relaxed);
    result.meanGpuNs = meanGpuNs_.load(std::memory_order_relaxed);
    result.p95GpuNs = p95GpuNs_.load(std::memory_order_relaxed);
    result.p99GpuNs = p99GpuNs_.load(std::memory_order_relaxed);
    result.disjointEvents = disjointEvents_.load(std::memory_order_relaxed);
    result.skippedSamples = skippedSamples_.load(std::memory_order_relaxed);
    return result;
}
} // namespace bismuth
#else
namespace bismuth {
bool GlesGpuTimerQuery::initialize() noexcept { return false; }
bool GlesGpuTimerQuery::begin() noexcept { return false; }
void GlesGpuTimerQuery::end() noexcept {}
void GlesGpuTimerQuery::poll() noexcept {}
GpuTimingSnapshot GlesGpuTimerQuery::snapshot() const noexcept { return {}; }
} // namespace bismuth
#endif
