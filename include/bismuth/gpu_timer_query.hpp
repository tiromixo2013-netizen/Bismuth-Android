#pragma once

#include <GLES3/gl31.h>
#include <atomic>
#include <cstdint>

namespace bismuth {

struct GpuTimingSnapshot {
    bool supported = false;
    std::uint32_t sampleCount = 0;
    std::uint64_t lastGpuNs = 0;
    std::uint64_t meanGpuNs = 0;
    std::uint64_t p95GpuNs = 0;
    std::uint64_t p99GpuNs = 0;
    std::uint64_t disjointEvents = 0;
    std::uint64_t skippedSamples = 0;
};

// Non-blocking EXT_disjoint_timer_query ring. All GL methods must be called
// on the render thread with the game's GLES context current.
class GlesGpuTimerQuery final {
public:
    bool initialize() noexcept;
    bool begin() noexcept;
    void end() noexcept;
    void poll() noexcept;
    GpuTimingSnapshot snapshot() const noexcept;

private:
    using GenQueriesFn = void (*)(GLsizei, GLuint*);
    using DeleteQueriesFn = void (*)(GLsizei, const GLuint*);
    using BeginQueryFn = void (*)(GLenum, GLuint);
    using EndQueryFn = void (*)(GLenum);
    using GetQueryObjectuivFn = void (*)(GLuint, GLenum, GLuint*);
    using GetQueryObjectui64vFn = void (*)(GLuint, GLenum, GLuint64*);

    static constexpr std::uint32_t kQueryCount = 4;
    static constexpr std::uint32_t kWindowSize = 120;
    bool loadFunctions() noexcept;
    void recordSample(std::uint64_t ns) noexcept;

    GenQueriesFn genQueries_ = nullptr;
    DeleteQueriesFn deleteQueries_ = nullptr;
    BeginQueryFn beginQuery_ = nullptr;
    EndQueryFn endQuery_ = nullptr;
    GetQueryObjectuivFn getQueryObjectuiv_ = nullptr;
    GetQueryObjectui64vFn getQueryObjectui64v_ = nullptr;
    GLuint queries_[kQueryCount]{};
    bool pending_[kQueryCount]{};
    bool initialized_ = false;
    bool active_ = false;
    std::uint32_t activeSlot_ = 0;
    std::uint32_t nextSlot_ = 0;
    std::uint64_t samples_[kWindowSize]{};
    std::uint32_t sampleCount_ = 0;
    std::uint32_t sampleCursor_ = 0;
    std::uint64_t sampleTotal_ = 0;
    std::atomic<bool> supported_{false};
    std::atomic<std::uint32_t> publishedSampleCount_{0};
    std::atomic<std::uint64_t> lastGpuNs_{0};
    std::atomic<std::uint64_t> meanGpuNs_{0};
    std::atomic<std::uint64_t> p95GpuNs_{0};
    std::atomic<std::uint64_t> p99GpuNs_{0};
    std::atomic<std::uint64_t> disjointEvents_{0};
    std::atomic<std::uint64_t> skippedSamples_{0};
};

} // namespace bismuth
