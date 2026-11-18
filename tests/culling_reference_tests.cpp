#include "bismuth/culling_reference.hpp"
#include "bismuth/clip_bounds.hpp"
#include "bismuth/atlas_indices.hpp"
#include "bismuth/atlas_lifecycle.hpp"
#include "bismuth/atlas_observation.hpp"
#include "bismuth/capacity_growth.hpp"
#include "bismuth/spatial_hash.hpp"
#include "bismuth/target_layout.hpp"
#include "bismuth/telemetry_gate.hpp"
#include "bismuth/observer_callback_guard.hpp"
#include <array>
#include <atomic>
#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>
using namespace bismuth;
int main() {
    std::atomic<std::uint32_t> activeObserverCallbacks{0u};
    releaseObserverCallback(activeObserverCallbacks, nullptr);
    assert(activeObserverCallbacks.load(std::memory_order_relaxed) == 0u);
    activeObserverCallbacks.fetch_add(1u, std::memory_order_relaxed);
    releaseObserverCallback(activeObserverCallbacks, reinterpret_cast<void*>(1));
    assert(activeObserverCallbacks.load(std::memory_order_relaxed) == 0u);

    TelemetryGate telemetryGate;
    assert(telemetryGate.enabled());
    telemetryGate.setEnabled(false);
    assert(!telemetryGate.enabled());
    telemetryGate.setEnabled(true);
    assert(telemetryGate.enabled());

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
    auto invertedViewFailOpen = cullStable(candidates, inverted);
    assert((invertedViewFailOpen.visibleIndices == std::vector<std::uint32_t>{0, 1, 2, 3, 4}));
    const Rect touching{10, 10, 10, 20};
    auto edge = cullStable(candidates, touching);
    assert((edge.visibleIndices == std::vector<std::uint32_t>{0}));
    std::vector<Candidate> ordered = {
        {{0, 0, 1, 1}, 9}, {{2, 2, 3, 3}, 8}, {{4, 4, 5, 5}, 7}
    };
    auto stable = cullStable(ordered, Rect{-1, -1, 10, 10});
    assert((stable.visibleIndices == std::vector<std::uint32_t>{0, 1, 2}));
    std::vector<std::uint32_t> reusedIndices;
    cullStableInto(ordered, Rect{-1, -1, 10, 10}, reusedIndices);
    const auto warmedCapacity = reusedIndices.capacity();
    assert((reusedIndices == std::vector<std::uint32_t>{0, 1, 2}));
    cullStableInto(ordered, Rect{0, 0, 2, 2}, reusedIndices);
    assert(reusedIndices.capacity() == warmedCapacity);
    assert((reusedIndices == std::vector<std::uint32_t>{0, 1}));
    std::vector<std::uint32_t> reusedPayloads;
    cullStablePayloadsInto(ordered, Rect{-1, -1, 10, 10}, reusedPayloads);
    const auto warmedPayloadCapacity = reusedPayloads.capacity();
    cullStablePayloadsInto(ordered, Rect{0, 0, 2, 2}, reusedPayloads);
    assert(reusedPayloads.capacity() == warmedPayloadCapacity);
    assert((reusedPayloads == std::vector<std::uint32_t>{9, 8}));
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
    auto failOpenBounds = cullStable(invalidBounds, view);
    assert((failOpenBounds.visibleIndices == std::vector<std::uint32_t>{0, 1, 2}));
    auto invalidView = cullStable(candidates, Rect{nan, 0, 100, 100});
    assert((invalidView.visibleIndices == std::vector<std::uint32_t>{0, 1, 2, 3, 4}));
    assert(intersects(Bounds{nan, 0, 10, 10}, view));
    assert(intersects(Bounds{0, 0, 1, 1}, Rect{10, 10, 0, 0}));
    assert((cullStablePayloads(atlasCandidates, Rect{nan, 0, 1, 1}) ==
            std::vector<std::uint32_t>{17, 3, 42, 9}));

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

    // Target 2.2081 layout: node +0xd0 -> CCArray object, +0x38 -> _ccArray,
    // then _ccArray +0x00 contains the element count.
    std::array<std::uint8_t,
        bismuth::target_layout::kBatchNodeChildrenArrayObjectOffset + sizeof(void*)> fakeBatch{};
    std::array<std::uint8_t,
        bismuth::target_layout::kCCArrayDataOffset + sizeof(void*)> fakeArrayObject{};
    std::array<std::uint8_t,
        bismuth::target_layout::kArrayCountOffset + sizeof(std::uint32_t)> fakeArrayData{};
    const void* fakeArrayObjectPointer = fakeArrayObject.data();
    const void* fakeArrayDataPointer = fakeArrayData.data();
    std::memcpy(fakeBatch.data() + bismuth::target_layout::kBatchNodeChildrenArrayObjectOffset,
                &fakeArrayObjectPointer, sizeof(fakeArrayObjectPointer));
    std::memcpy(fakeArrayObject.data() + bismuth::target_layout::kCCArrayDataOffset,
                &fakeArrayDataPointer, sizeof(fakeArrayDataPointer));
    const std::uint32_t fakeChildCount = 321u;
    std::memcpy(fakeArrayData.data() + bismuth::target_layout::kArrayCountOffset,
                &fakeChildCount, sizeof(fakeChildCount));
    std::uint32_t observedChildren = 0;
    assert(bismuth::target_layout::readBatchChildCount(fakeBatch.data(), observedChildren));
    assert(observedChildren == fakeChildCount);
    assert(!bismuth::target_layout::readBatchChildCount(nullptr, observedChildren));
    assert(observedChildren == 0u);

    const void* nullArrayData = nullptr;
    std::memcpy(fakeArrayObject.data() + bismuth::target_layout::kCCArrayDataOffset,
                &nullArrayData, sizeof(nullArrayData));
    assert(!bismuth::target_layout::readBatchChildCount(fakeBatch.data(), observedChildren));
    std::memcpy(fakeArrayObject.data() + bismuth::target_layout::kCCArrayDataOffset,
                &fakeArrayDataPointer, sizeof(fakeArrayDataPointer));
    const std::uint32_t impossibleChildCount =
        bismuth::target_layout::kMaxObservedBatchChildren + 1u;
    std::memcpy(fakeArrayData.data() + bismuth::target_layout::kArrayCountOffset,
                &impossibleChildCount, sizeof(impossibleChildCount));
    assert(!bismuth::target_layout::readBatchChildCount(fakeBatch.data(), observedChildren));

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
    passive.recordQuadWrite(false);
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
    // Projection helper: identity MVP keeps clip-space x/y unchanged.
    const ClipMatrix4 identityMvp{
        1.f, 0.f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        0.f, 0.f, 0.f, 1.f
    };
    const std::array<QuadPosition3, 4> identityQuad{{
        {-0.75f, -0.5f, 0.f}, {0.25f, -0.5f, 0.f},
        {-0.75f, 0.5f, 0.f}, {0.25f, 0.5f, 0.f}
    }};
    const Bounds projected = conservativeQuadClipBounds(identityQuad, identityMvp);
    assert(projected.left == -0.75f && projected.right == 0.25f);
    assert(projected.bottom == -0.5f && projected.top == 0.5f);
    assert(intersects(projected, Rect{-1.f, -1.f, 0.f, 0.f}));
    assert(!intersects(projected, Rect{0.5f, 0.5f, 1.f, 1.f}));

    // Perspective divide is applied to every vertex, not just the first one.
    ClipMatrix4 perspectiveMvp = identityMvp;
    perspectiveMvp[3] = 0.5f;
    const Bounds perspectiveBounds = conservativeQuadClipBounds(identityQuad, perspectiveMvp);
    assert(perspectiveBounds.left < -0.75f);
    assert(perspectiveBounds.right < 0.25f);
    assert(isValidBounds(perspectiveBounds));

    // Unsafe matrices, vertices, and eye-plane crossings fail open to full NDC.
    ClipMatrix4 invalidMvp = identityMvp;
    invalidMvp[0] = std::numeric_limits<float>::quiet_NaN();
    const Bounds invalidMatrixBounds = conservativeQuadClipBounds(identityQuad, invalidMvp);
    assert(invalidMatrixBounds.left == -1.f && invalidMatrixBounds.bottom == -1.f);
    assert(invalidMatrixBounds.right == 1.f && invalidMatrixBounds.top == 1.f);
    auto behindEyeQuad = identityQuad;
    behindEyeQuad[0].z = -2.f;
    ClipMatrix4 eyePlaneMvp = identityMvp;
    eyePlaneMvp[11] = 1.f;
    eyePlaneMvp[15] = 0.f;
    const Bounds eyePlaneBounds = conservativeQuadClipBounds(behindEyeQuad, eyePlaneMvp);
    assert(eyePlaneBounds.left == -1.f && eyePlaneBounds.bottom == -1.f);
    assert(eyePlaneBounds.right == 1.f && eyePlaneBounds.top == 1.f);
    auto invalidVertexQuad = identityQuad;
    invalidVertexQuad[2].x = std::numeric_limits<float>::infinity();
    const Bounds invalidVertexBounds = conservativeQuadClipBounds(invalidVertexQuad, identityMvp);
    assert(invalidVertexBounds.left == -1.f && invalidVertexBounds.bottom == -1.f);
    assert(invalidVertexBounds.right == 1.f && invalidVertexBounds.top == 1.f);


    // Dynamic spatial hash: updates move an object between cells without
    // changing its stable painter-order position.
    SpatialHash spatial(10.0f, 16u, 64u);
    assert(spatial.insert(100u, Bounds{0, 0, 4, 4}));
    assert(spatial.insert(200u, Bounds{25, 0, 29, 4}));
    assert(spatial.insert(300u, Bounds{50, 0, 54, 4}));
    std::vector<std::uint32_t> spatialHits;
    spatial.query(Rect{-1, -1, 30, 10}, spatialHits);
    assert((spatialHits == std::vector<std::uint32_t>{100u, 200u}));
    assert(spatial.update(300u, Bounds{8, 0, 12, 4}));
    spatial.query(Rect{0, 0, 15, 10}, spatialHits);
    assert((spatialHits == std::vector<std::uint32_t>{100u, 300u}));
    assert(spatial.remove(100u));
    spatial.query(Rect{0, 0, 15, 10}, spatialHits);
    assert((spatialHits == std::vector<std::uint32_t>{300u}));
    assert(!spatial.remove(999u));

    // Large bounds use the overflow path rather than expanding to thousands
    // of cells; invalid bounds fail open and remain query-visible.
    assert(spatial.insert(400u, Bounds{-1000, -1000, 1000, 1000}));
    assert(spatial.insert(500u, Bounds{nan, 0, 10, 10}));
    spatial.query(Rect{2000, 2000, 2010, 2010}, spatialHits);
    assert((spatialHits == std::vector<std::uint32_t>{400u, 500u}));
    // A huge query safely falls back to scanning all entries.
    spatial.query(Rect{-1.0e20f, -1.0e20f, 1.0e20f, 1.0e20f}, spatialHits);
    assert(spatialHits.size() == 4u);

    std::cout << "Bismuth-Android reference tests passed\n";
}
