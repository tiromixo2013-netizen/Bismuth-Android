#include "bismuth/culling_reference.hpp"
#include "bismuth/atlas_indices.hpp"
#include "bismuth/atlas_lifecycle.hpp"
#include "bismuth/atlas_observation.hpp"
#include "bismuth/capacity_growth.hpp"
#include "bismuth/target_layout.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>
using namespace bismuth;
int main() {
    // Geometric SSBO growth: preserve capacity, double gradually, and clamp.
    assert(nextWorkgroupCapacity(0u, 1u, 128u) == 1u);
    assert(nextWorkgroupCapacity(1u, 2u, 128u) == 2u);
    assert(nextWorkgroupCapacity(2u, 3u, 128u) == 4u);
    assert(nextWorkgroupCapacity(4u, 4u, 128u) == 4u);
    assert(nextWorkgroupCapacity(64u, 65u, 128u) == 128u);
    assert(nextWorkgroupCapacity(128u, 129u, 128u) == 0u);
    assert(nextWorkgroupCapacity(0u, 0u, 128u) == 0u);

    const Rect view{0, 0, 100, 100};
    std::vector<Candidate> candidates = {
        {{-10, 10, 10, 20}, 0}, {{110, 10, 120, 20}, 1},
        {{20, 20, 30, 30}, 2}, {{100, 100, 110, 110}, 3},
        {{40, 40, 40, 40}, 4}
    };
    auto result = cullStable(candidates, view);
    assert((result.visibleIndices == std::vector<std::uint32_t>{0, 2, 3, 4}));
    auto empty = cullStable({}, view);
    assert(empty.visibleIndices.empty());
    const Rect inverted{10, 10, 0, 0};
    auto none = cullStable(candidates, inverted);
    assert(none.visibleIndices.empty());
    const Rect touching{10, 10, 10, 20};
    auto edge = cullStable(candidates, touching);
    assert((edge.visibleIndices == std::vector<std::uint32_t>{0}));
    std::vector<Candidate> ordered = {
        {{0, 0, 1, 1}, 9}, {{2, 2, 3, 3}, 8}, {{4, 4, 5, 5}, 7}
    };
    auto stable = cullStable(ordered, Rect{-1, -1, 10, 10});
    assert((stable.visibleIndices == std::vector<std::uint32_t>{0, 1, 2}));
    std::vector<Candidate> atlasCandidates = {
        {{-5, 0, 5, 5}, 17}, {{200, 0, 210, 5}, 3},
        {{10, 10, 20, 20}, 42}, {{0, 0, 1, 1}, 9}
    };
    assert((cullStablePayloads(atlasCandidates, view) == std::vector<std::uint32_t>{17, 42, 9}));
    assert(cullStablePayloads(atlasCandidates, Rect{50, 50, 60, 60}).empty());
    const float nan = std::numeric_limits<float>::quiet_NaN();
    std::vector<Candidate> invalidBounds = {
        {{5, 5, 1, 1}, 10}, {{nan, 0, 10, 10}, 11},
        {{0, 0, 10, 10}, 12}
    };
    auto validOnly = cullStable(invalidBounds, view);
    assert((validOnly.visibleIndices == std::vector<std::uint32_t>{2}));
    auto invalidView = cullStable(candidates, Rect{nan, 0, 100, 100});
    assert(invalidView.visibleIndices.empty());

    // Target-verified 2.2081 atlas EBO pattern, preserving slot order.
    std::vector<std::uint16_t> elements;
    assert(buildAtlasElementIndices16({2u, 0u, 1u}, 3u, elements));
    assert((elements == std::vector<std::uint16_t>{
        8u, 9u, 10u, 11u, 10u, 9u,
        0u, 1u, 2u, 3u, 2u, 1u,
        4u, 5u, 6u, 7u, 6u, 5u
    }));
    assert(buildAtlasElementIndices16({}, 0u, elements));
    assert(elements.empty());
    assert(isValidAtlasSlot(2u, 3u));
    assert(!isValidAtlasSlot(3u, 3u));
    assert(!isValidAtlasSlot(0u, kAtlasMaxQuads16 + 1u));
    assert(!buildAtlasElementIndices16({3u}, 3u, elements));
    assert(elements.empty());
    assert(!buildAtlasElementIndices16({0u}, kAtlasMaxQuads16 + 1u, elements));
    std::vector<std::uint32_t> packedElements;
    assert(buildAtlasElementIndices16Packed({2u, 0u}, 3u, packedElements));
    assert((packedElements == std::vector<std::uint32_t>{
        8u | (9u << 16u), 10u | (11u << 16u), 10u | (9u << 16u),
        0u | (1u << 16u), 2u | (3u << 16u), 2u | (1u << 16u)
    }));
    assert(!buildAtlasElementIndices16Packed({3u}, 3u, packedElements));
    assert(packedElements.empty());
    IndexedDrawCommand command{};
    assert(makeIndexedDrawCommand(7u, command));
    assert(command.count == 42u && command.instanceCount == 1u &&
           command.firstIndex == 0u && command.baseVertex == 0 &&
           command.reservedMustBeZero == 0u);
    assert(makeIndexedDrawCommand(0u, command));
    assert(command.count == 0u && command.instanceCount == 0u);
    assert(!makeIndexedDrawCommand(kAtlasMaxQuads16 + 1u, command));
    // Atlas revision tracking: structural mutations invalidate slot maps;
    // in-place quad writes invalidate derived quad data but not slot identity.
    AtlasRevisionTracker revisions;
    const AtlasRevision initialRevision = revisions.snapshot();
    assert(revisions.slotMappingCurrent(initialRevision));
    assert(revisions.quadDataCurrent(initialRevision));
    revisions.recordQuadWrite();
    assert(revisions.slotMappingCurrent(initialRevision));
    assert(!revisions.quadDataCurrent(initialRevision));
    const AtlasRevision afterQuadWrite = revisions.snapshot();
    revisions.recordStructuralMutation();
    assert(!revisions.slotMappingCurrent(afterQuadWrite));
    assert(!revisions.quadDataCurrent(afterQuadWrite));
    const AtlasRevision afterStructure = revisions.snapshot();
    assert(revisions.slotMappingCurrent(afterStructure));
    assert(revisions.quadDataCurrent(afterStructure));
    // Verified target layout reader: count metadata is sampled without a
    // per-child traversal, and invalid/null layouts fail closed.
    std::array<std::uint8_t, bismuth::target_layout::kBatchNodeChildrenArrayOffset + sizeof(void*)> fakeBatch{};
    std::array<std::uint8_t, bismuth::target_layout::kArrayCountOffset + sizeof(std::uint32_t)> fakeChildren{};
    const void* fakeChildrenPointer = fakeChildren.data();
    std::memcpy(fakeBatch.data() + bismuth::target_layout::kBatchNodeChildrenArrayOffset,
                &fakeChildrenPointer, sizeof(fakeChildrenPointer));
    const std::uint32_t fakeChildCount = 321u;
    std::memcpy(fakeChildren.data() + bismuth::target_layout::kArrayCountOffset,
                &fakeChildCount, sizeof(fakeChildCount));
    std::uint32_t observedChildren = 0;
    assert(bismuth::target_layout::readBatchChildCount(fakeBatch.data(), observedChildren));
    assert(observedChildren == fakeChildCount);
    assert(!bismuth::target_layout::readBatchChildCount(nullptr, observedChildren));
    assert(observedChildren == 0u);
    const std::uint32_t impossibleChildCount =
        bismuth::target_layout::kMaxObservedBatchChildren + 1u;
    std::memcpy(fakeChildren.data() + bismuth::target_layout::kArrayCountOffset,
                &impossibleChildCount, sizeof(impossibleChildCount));
    assert(!bismuth::target_layout::readBatchChildCount(fakeBatch.data(), observedChildren));

    // Passive per-frame telemetry is allocation-free and resets at frame start.
    AtlasObservationCounters observation;
    observation.beginFrame();
    observation.recordBatchDraw(12u, 1200u);
    observation.recordBatchDraw(20u, 2300u);
    observation.recordSpriteTransform(true);
    observation.recordSpriteTransform(false);
    observation.recordQuadWrite();
    observation.recordStructuralMutation();
    observation.recordDirtyUpload(32u);
    const AtlasObservationSnapshot observed = observation.snapshot();
    assert(observed.batchDrawCalls == 2u);
    assert(observed.batchDrawCpuNsTotal == 3500u);
    assert(observed.batchDrawCpuNsMax == 2300u);
    assert(observed.batchDrawChildrenTotal == 32u);
    assert(observed.spriteTransformCalls == 2u);
    assert(observed.unknownTransformOverrides == 1u);
    assert(observed.quadWrites == 1u);
    assert(observed.structuralMutations == 1u);
    assert(observed.dirtyUploads == 1u);
    assert(observed.uploadedQuads == 32u);
    assert(observed.maxChildrenInBatch == 20u);
    observation.beginFrame();
    const AtlasObservationSnapshot cleared = observation.snapshot();
    assert(cleared.batchDrawCalls == 0u && cleared.uploadedQuads == 0u &&
           cleared.batchDrawCpuNsTotal == 0u && cleared.batchDrawCpuNsMax == 0u &&
           cleared.batchDrawChildrenTotal == 0u);
    // Frame interval window: deterministic monotonic timestamps, bounded rolling
    // samples, nearest-rank percentiles, and rejection of backwards time.
    FrameIntervalWindow frameWindow;
    assert(frameWindow.recordFrameStart(1000000000ull));
    assert(frameWindow.snapshot().sampleCount == 0u);
    assert(frameWindow.recordFrameStart(1016000000ull));
    assert(frameWindow.recordFrameStart(1033000000ull));
    assert(frameWindow.recordFrameStart(1050000000ull));
    assert(!frameWindow.recordFrameStart(1049000000ull));
    const auto frameStats = frameWindow.snapshot();
    assert(frameStats.sampleCount == 3u);
    assert(frameStats.lastIntervalNs == 17000000ull);
    assert(frameStats.meanIntervalNs == 16666666ull);
    assert(frameStats.p50IntervalNs == 17000000ull);
    assert(frameStats.p95IntervalNs == 17000000ull);
    assert(frameStats.p99IntervalNs == 17000000ull);
    frameWindow.reset();
    assert(frameWindow.snapshot().sampleCount == 0u);
    // The rolling window remains bounded and drops the oldest interval.
    assert(frameWindow.recordFrameStart(0u));
    for (std::uint64_t i = 1u; i <= FrameIntervalWindow::kWindowSize + 1u; ++i)
        assert(frameWindow.recordFrameStart(i * 1000000ull));
    const auto rolledStats = frameWindow.snapshot();
    assert(rolledStats.sampleCount == FrameIntervalWindow::kWindowSize);
    assert(rolledStats.lastIntervalNs == 1000000ull);
    assert(rolledStats.meanIntervalNs == 1000000ull);
    assert(rolledStats.p50IntervalNs == 1000000ull);
    assert(rolledStats.p95IntervalNs == 1000000ull);
    assert(rolledStats.p99IntervalNs == 1000000ull);
    AtlasPassiveObserver frameObserver;
    assert(frameObserver.recordFrameStart(500u));
    assert(frameObserver.recordFrameStart(16500500u));
    assert(frameObserver.snapshot().frameIntervals.sampleCount == 1u);
    AtlasPassiveObserver passive;
    const AtlasRevision passiveStart = passive.revision();
    passive.recordQuadWrite(false); // failed original operation: no invalidation
    assert(passive.quadDataCurrent(passiveStart));
    passive.recordQuadWrite(true);
    assert(passive.slotMappingCurrent(passiveStart));
    assert(!passive.quadDataCurrent(passiveStart));
    const AtlasRevision beforeUnknownTransform = passive.revision();
    passive.recordSpriteTransform(false);
    assert(passive.slotMappingCurrent(beforeUnknownTransform));
    assert(!passive.quadDataCurrent(beforeUnknownTransform));
    const AtlasRevision beforeUnknown = passive.revision();
    passive.invalidateUnknownMutation();
    assert(!passive.slotMappingCurrent(beforeUnknown));
    const AtlasRevision beforeDirtyUpload = passive.revision();
    passive.recordDirtyUpload(false, 10u);
    assert(passive.quadDataCurrent(beforeDirtyUpload));
    passive.recordDirtyUpload(true, 10u);
    assert(!passive.quadDataCurrent(beforeDirtyUpload));
    passive.recordStructuralMutation(false);
    const AtlasObservationSnapshot passiveStats = passive.snapshot();
    assert(passiveStats.quadWrites == 1u);
    assert(passiveStats.structuralMutations == 1u);
    std::cout << "Bismuth-Android reference tests passed\n";
}
