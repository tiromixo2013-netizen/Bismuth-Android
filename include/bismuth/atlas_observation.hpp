#pragma once

#include <cstdint>

namespace bismuth {

// Allocation-free, render-thread-owned telemetry for a future passive observer.
// The observer must call these after observing the corresponding original
// engine operation; this class does not install hooks or touch GL/game memory.
struct AtlasObservationSnapshot {
    std::uint64_t batchDrawCalls = 0;
    std::uint64_t spriteTransformCalls = 0;
    std::uint64_t unknownTransformOverrides = 0;
    std::uint64_t quadWrites = 0;
    std::uint64_t structuralMutations = 0;
    std::uint64_t dirtyUploads = 0;
    std::uint64_t uploadedQuads = 0;
    std::uint32_t maxChildrenInBatch = 0;
};

class AtlasObservationCounters {
public:
    void beginFrame() noexcept { current_ = {}; }

    void recordBatchDraw(std::uint32_t childCount) noexcept {
        ++current_.batchDrawCalls;
        if (childCount > current_.maxChildrenInBatch)
            current_.maxChildrenInBatch = childCount;
    }

    void recordSpriteTransform(bool knownBaseImplementation) noexcept {
        ++current_.spriteTransformCalls;
        if (!knownBaseImplementation)
            ++current_.unknownTransformOverrides;
    }

    void recordQuadWrite() noexcept { ++current_.quadWrites; }
    void recordStructuralMutation() noexcept {
        ++current_.structuralMutations;
    }

    void recordDirtyUpload(std::uint32_t quadCount) noexcept {
        ++current_.dirtyUploads;
        current_.uploadedQuads += quadCount;
    }

    AtlasObservationSnapshot snapshot() const noexcept { return current_; }

private:
    AtlasObservationSnapshot current_{};
};

} // namespace bismuth
