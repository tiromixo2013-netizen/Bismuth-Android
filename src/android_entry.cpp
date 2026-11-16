#include <jni.h>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <new>
#include <string>
#include <vector>
#include "bismuth/android_api.h"
#include "bismuth/atlas_observation.hpp"
#include "bismuth/cocos_hooks.hpp"
#include "bismuth/gles31_culler.hpp"

namespace {
struct BismuthInstance {
    bismuth::Gles31Culler culler;
    bismuth::AtlasPassiveObserver observer;
    // Reused staging storage: avoids a fresh heap allocation on every upload.
    std::vector<bismuth::Candidate> candidateScratch;
};
BismuthInstance* g_autoHookInstance = nullptr;
int g_autoHookStatus = 0;
std::string g_autoHookError;

void writeError(char* out, std::uint32_t capacity, const std::string& message) {
    if (!out || capacity == 0) return;
    const auto n = message.size() < capacity - 1 ? message.size() : capacity - 1;
    message.copy(out, n);
    out[n] = '\0';
}
static_assert(sizeof(BismuthCandidate) == sizeof(bismuth::Candidate),
              "C API candidate layout must match the GLES shader record");
static_assert(sizeof(BismuthAtlasTelemetry) == sizeof(bismuth::AtlasObservationSnapshot),
              "C API telemetry layout must match the observer snapshot");
static_assert(offsetof(BismuthAtlasTelemetry, max_children_in_batch) ==
              offsetof(bismuth::AtlasObservationSnapshot, maxChildrenInBatch),
              "C API batch child count offset mismatch");
static_assert(offsetof(BismuthAtlasTelemetry, frame_interval_sample_count) ==
              offsetof(bismuth::AtlasObservationSnapshot, frameIntervals) +
              offsetof(bismuth::FrameIntervalSnapshot, sampleCount),
              "C API frame sample count offset mismatch");
static_assert(offsetof(BismuthAtlasTelemetry, frame_interval_last_ns) ==
              offsetof(bismuth::AtlasObservationSnapshot, frameIntervals) +
              offsetof(bismuth::FrameIntervalSnapshot, lastIntervalNs),
              "C API frame interval offset mismatch");
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM*, void*) {
    // APK injection loads Bismuth immediately after cocos2dcpp.
    auto* instance = new (std::nothrow) BismuthInstance{};
    if (!instance) {
        g_autoHookStatus = -1;
        g_autoHookError = "Failed to allocate the automatic passive-hook observer";
        return JNI_VERSION_1_6;
    }
    std::string message;
    if (bismuth::installCocosPassiveHooks(&instance->observer, &message)) {
        g_autoHookInstance = instance;
        g_autoHookStatus = 1;
        g_autoHookError.clear();
    } else {
        g_autoHookStatus = -2;
        g_autoHookError = message;
        delete instance;
    }
    // A failed version check must never prevent the game itself from loading.
    return JNI_VERSION_1_6;
}
extern "C" const char* bismuth_android_build_id() {
    return "Bismuth-Android 0.8.0-passive-telemetry-gate";
}
extern "C" std::uint32_t bismuth_android_capabilities() {
    return BISMUTH_CAP_NATIVE_LIBRARY | BISMUTH_CAP_GLES31_COMPUTE |
           BISMUTH_CAP_ATLAS_INDEX_OUTPUT | BISMUTH_CAP_PASSIVE_OBSERVER_API |
           BISMUTH_CAP_COCOS_PASSIVE_HOOKS | BISMUTH_CAP_GPU_DRAW_BRIDGE;
}
extern "C" void* bismuth_create() {
    return new (std::nothrow) BismuthInstance{};
}
extern "C" int bismuth_initialize(void* handle, char* error, std::uint32_t errorCapacity) {
    if (!handle) return -1;
    std::string message;
    const bool ok = static_cast<BismuthInstance*>(handle)->culler.initialize(&message);
    if (!ok) writeError(error, errorCapacity, message);
    return ok ? 0 : -2;
}
extern "C" int bismuth_is_initialized(void* handle) {
    if (!handle) return 0;
    const auto* instance = static_cast<const BismuthInstance*>(handle);
    return instance->culler.isInitialized() ? 1 : 0;
}
extern "C" int bismuth_upload(void* handle, const BismuthCandidate* candidates,
                               std::uint32_t count, std::uint32_t atlasCapacity) {
    if (!handle || (count && !candidates)) return -1;
    // Reject oversized input before resizing or copying user-provided records.
    if (count > BISMUTH_MAX_CANDIDATES) return -2;
    auto* instance = static_cast<BismuthInstance*>(handle);
    try {
        instance->candidateScratch.resize(count);
    } catch (const std::bad_alloc&) {
        return -2;
    }
    if (count) {
        std::memcpy(instance->candidateScratch.data(), candidates,
                    static_cast<std::size_t>(count) * sizeof(BismuthCandidate));
    }
    // vector::resize reuses its capacity after the first sufficiently large upload.
    return instance->culler.uploadCandidates(instance->candidateScratch, atlasCapacity) ? 0 : -2;
}
extern "C" int bismuth_dispatch(void* handle, float left, float bottom, float right,
                                 float top, std::uint32_t count) {
    if (!handle) return -1;
    return static_cast<BismuthInstance*>(handle)->culler.dispatch(
        bismuth::Rect{left, bottom, right, top}, count) ? 0 : -2;
}
extern "C" std::uint32_t bismuth_visible_index_buffer(void* handle) {
    return handle ? static_cast<BismuthInstance*>(handle)->culler.visibleIndexBuffer() : 0u;
}
extern "C" std::uint32_t bismuth_indirect_command_buffer(void* handle) {
    return handle ? static_cast<BismuthInstance*>(handle)->culler.indirectCommandBuffer() : 0u;
}
extern "C" std::uint32_t bismuth_indexed_command_buffer(void* handle) {
    return handle ? static_cast<BismuthInstance*>(handle)->culler.indexedCommandBuffer() : 0u;
}
extern "C" std::uint32_t bismuth_atlas_element_index_buffer(void* handle) {
    return handle ? static_cast<BismuthInstance*>(handle)->culler.atlasElementIndexBuffer() : 0u;
}

extern "C" void bismuth_set_gpu_draw_bridge_enabled(int enabled) {
    bismuth::setGpuDrawBridgeEnabled(enabled != 0);
}
extern "C" int bismuth_is_gpu_draw_bridge_enabled() {
    return bismuth::isGpuDrawBridgeEnabled() ? 1 : 0;
}
extern "C" void bismuth_set_passive_telemetry_enabled(int enabled) {
    bismuth::setPassiveTelemetryEnabled(enabled != 0);
}
extern "C" int bismuth_is_passive_telemetry_enabled() {
    return bismuth::isPassiveTelemetryEnabled() ? 1 : 0;
}

extern "C" int bismuth_auto_hook_status() {
    return g_autoHookStatus;
}
extern "C" const char* bismuth_auto_hook_error() {
    return g_autoHookError.c_str();
}

extern "C" int bismuth_install_cocos_hooks(void* handle, char* error, std::uint32_t errorCapacity) {
    if (!handle) return -1;
    std::string message;
    const bool ok = bismuth::installCocosPassiveHooks(
        &static_cast<BismuthInstance*>(handle)->observer, &message);
    if (!ok) writeError(error, errorCapacity, message);
    return ok ? 0 : -2;
}

extern "C" void bismuth_observer_begin_frame(void* handle) {
    if (handle) static_cast<BismuthInstance*>(handle)->observer.beginFrame();
}
extern "C" int bismuth_observer_record_frame_start(void* handle, std::uint64_t monotonicNs) {
    if (!handle) return -1;
    return static_cast<BismuthInstance*>(handle)->observer.recordFrameStart(monotonicNs) ? 0 : -2;
}
extern "C" void bismuth_observer_record_batch_draw(void* handle, std::uint32_t childCount) {
    if (handle) static_cast<BismuthInstance*>(handle)->observer.recordBatchDraw(childCount);
}
extern "C" void bismuth_observer_record_sprite_transform(void* handle, int knownBaseImplementation) {
    if (handle) static_cast<BismuthInstance*>(handle)->observer.recordSpriteTransform(knownBaseImplementation != 0);
}
extern "C" void bismuth_observer_record_quad_write(void* handle, int originalCallSucceeded) {
    if (handle) static_cast<BismuthInstance*>(handle)->observer.recordQuadWrite(originalCallSucceeded != 0);
}
extern "C" void bismuth_observer_record_structural_mutation(void* handle, int originalCallSucceeded) {
    if (handle) static_cast<BismuthInstance*>(handle)->observer.recordStructuralMutation(originalCallSucceeded != 0);
}
extern "C" void bismuth_observer_record_dirty_upload(void* handle, int originalUploadSucceeded,
                                                       std::uint32_t quadCount) {
    if (handle) static_cast<BismuthInstance*>(handle)->observer.recordDirtyUpload(
        originalUploadSucceeded != 0, quadCount);
}
extern "C" void bismuth_observer_invalidate_unknown_mutation(void* handle) {
    if (handle) static_cast<BismuthInstance*>(handle)->observer.invalidateUnknownMutation();
}
extern "C" int bismuth_observer_snapshot(void* handle, BismuthAtlasTelemetry* telemetry,
                                          std::uint64_t* slotsRevision,
                                          std::uint64_t* contentRevision) {
    if (!handle || !telemetry) return -1;
    const auto* instance = static_cast<const BismuthInstance*>(handle);
    const auto counters = instance->observer.snapshot();
    telemetry->batch_draw_calls = counters.batchDrawCalls;
    telemetry->batch_draw_cpu_ns_total = counters.batchDrawCpuNsTotal;
    telemetry->batch_draw_cpu_ns_max = counters.batchDrawCpuNsMax;
    telemetry->batch_draw_children_total = counters.batchDrawChildrenTotal;
    telemetry->sprite_transform_calls = counters.spriteTransformCalls;
    telemetry->unknown_transform_overrides = counters.unknownTransformOverrides;
    telemetry->quad_writes = counters.quadWrites;
    telemetry->structural_mutations = counters.structuralMutations;
    telemetry->dirty_uploads = counters.dirtyUploads;
    telemetry->uploaded_quads = counters.uploadedQuads;
    telemetry->max_children_in_batch = counters.maxChildrenInBatch;
    telemetry->reserved0 = 0;
    telemetry->frame_interval_sample_count = counters.frameIntervals.sampleCount;
    telemetry->frame_interval_last_ns = counters.frameIntervals.lastIntervalNs;
    telemetry->frame_interval_mean_ns = counters.frameIntervals.meanIntervalNs;
    telemetry->frame_interval_p50_ns = counters.frameIntervals.p50IntervalNs;
    telemetry->frame_interval_p95_ns = counters.frameIntervals.p95IntervalNs;
    telemetry->frame_interval_p99_ns = counters.frameIntervals.p99IntervalNs;
    const auto revision = instance->observer.revision();
    if (slotsRevision) *slotsRevision = revision.slots;
    if (contentRevision) *contentRevision = revision.content;
    return 0;
}

extern "C" void bismuth_destroy(void* handle) {
    if (!handle) return;
    auto* instance = static_cast<BismuthInstance*>(handle);
    bismuth::detachCocosPassiveHooks(&instance->observer);
    // Call while the GLES context that owns this instance is current.
    delete instance;
}
