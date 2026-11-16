#pragma once

#include <cstdint>
#include "bismuth/atlas_lifecycle.hpp"

namespace bismuth {

// Allocation-free, render-thread-owned telemetry for a future passive observer.
// The observer must call these after observing the corresponding original
// engine operation; this class does not install hooks or touch GL/game memory.
struct AtlasObservationSnapshot {
    std::uint64_t batchDrawCalls = 0;
    // CPU wall time spent inside the original batch-draw call; not GPU execution time.
    std::uint64_t batchDrawCpuNsTotal = 0;
    std::uint64_t batchDrawCpuNsMax = 0;
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

    void recordBatchDraw(std::uint32_t childCount, std::uint64_t cpuDurationNs = 0) noexcept {
        ++current_.batchDrawCalls;
        current_.batchDrawCpuNsTotal += cpuDurationNs;
        if (cpuDurationNs > current_.batchDrawCpuNsMax)
            current_.batchDrawCpuNsMax = cpuDurationNs;
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

/*
 * Passive hook event facade. Call mutation methods only after the original
 * engine operation has completed successfully. Unknown mutations invalidate
 * slot mappings conservatively. This class does not install hooks.
 */
class AtlasPassiveObserver {
public:
    void beginFrame() noexcept { counters_.beginFrame(); }
    void recordBatchDraw(std::uint32_t childCount, std::uint64_t cpuDurationNs = 0) noexcept {
        counters_.recordBatchDraw(childCount, cpuDurationNs);
    }
    void recordSpriteTransform(bool knownBaseImplementation) noexcept {
        counters_.recordSpriteTransform(knownBaseImplementation);
        if (!knownBaseImplementation) revisions_.recordQuadWrite();
    }
    void recordQuadWrite(bool originalCallSucceeded) noexcept {
        if (!originalCallSucceeded) return;
        revisions_.recordQuadWrite();
        counters_.recordQuadWrite();
    }
    void recordStructuralMutation(bool originalCallSucceeded) noexcept {
        if (!originalCallSucceeded) return;
        revisions_.recordStructuralMutation();
        counters_.recordStructuralMutation();
    }
    void recordDirtyUpload(bool originalUploadSucceeded, std::uint32_t quadCount) noexcept {
        if (!originalUploadSucceeded) return;
        revisions_.recordQuadWrite();
        counters_.recordDirtyUpload(quadCount);
    }
    // Conservative invalidation for an upload attempt whose GL success cannot
    // be observed without consuming or changing the game's GL error state.
    void invalidateQuadData() noexcept { revisions_.recordQuadWrite(); }
    void invalidateUnknownMutation() noexcept {
        revisions_.recordStructuralMutation();
        counters_.recordStructuralMutation();
    }
    AtlasObservationSnapshot snapshot() const noexcept { return counters_.snapshot(); }
    AtlasRevision revision() const noexcept { return revisions_.snapshot(); }
    bool slotMappingCurrent(AtlasRevision saved) const noexcept {
        return revisions_.slotMappingCurrent(saved);
    }
    bool quadDataCurrent(AtlasRevision saved) const noexcept {
        return revisions_.quadDataCurrent(saved);
    }
private:
    AtlasRevisionTracker revisions_{};
    AtlasObservationCounters counters_{};
};

} // namespace bismuth
