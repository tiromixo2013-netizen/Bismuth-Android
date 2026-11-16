#pragma once
#include <stdint.h>

#if defined(__GNUC__)
#define BISMUTH_API __attribute__((visibility("default")))
#else
#define BISMUTH_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Compile-time capabilities of this shared library. */
#define BISMUTH_MAX_CANDIDATES 16384u

enum BismuthCapability {
    BISMUTH_CAP_NATIVE_LIBRARY = 1u << 0,
    BISMUTH_CAP_GLES31_COMPUTE = 1u << 1,
    BISMUTH_CAP_ATLAS_INDEX_OUTPUT = 1u << 2,
    BISMUTH_CAP_PASSIVE_OBSERVER_API = 1u << 3,
    BISMUTH_CAP_COCOS_PASSIVE_HOOKS = 1u << 4,
    BISMUTH_CAP_GPU_DRAW_BRIDGE = 1u << 5,
    BISMUTH_CAP_PASSIVE_TELEMETRY_CONTROL = 1u << 6,
    BISMUTH_CAP_GPU_TIMER_QUERY = 1u << 7
};

/* Bounds must be finite and normalized (left <= right, bottom <= top). Invalid bounds are culled. */
/* Matches the 32-byte std430 Candidate record used by the compute shaders. */
typedef struct BismuthCandidate {
    float left;
    float bottom;
    float right;
    float top;
    uint32_t payload; /* Current atlas quad slot; must be less than atlas_capacity passed to bismuth_upload. */
    uint32_t pad0;
    uint32_t pad1;
    uint32_t pad2;
} BismuthCandidate;

/* Observer-window counters. They are per-frame only when the caller resets them at a verified frame boundary. */
typedef struct BismuthAtlasTelemetry {
    uint64_t batch_draw_calls;
    uint64_t batch_draw_cpu_ns_total;
    uint64_t batch_draw_cpu_ns_max;
    uint64_t batch_draw_children_total;
    uint64_t sprite_transform_calls;
    uint64_t unknown_transform_overrides;
    uint64_t quad_writes;
    uint64_t structural_mutations;
    uint64_t dirty_uploads;
    uint64_t uploaded_quads;
    uint32_t max_children_in_batch;
    uint32_t reserved0; /* ABI padding matching AtlasObservationSnapshot alignment. */
    uint32_t frame_interval_sample_count;
    uint64_t frame_interval_last_ns;
    uint64_t frame_interval_mean_ns;
    uint64_t frame_interval_p50_ns;
    uint64_t frame_interval_p95_ns;
    uint64_t frame_interval_p99_ns;
} BismuthAtlasTelemetry;

/* Asynchronous GPU execution timing for the original atlas draw, when the
 * driver exposes GL_EXT_disjoint_timer_query. Values are nanoseconds. */
typedef struct BismuthGpuTiming {
    uint32_t supported;
    uint32_t sample_count;
    uint64_t last_gpu_ns;
    uint64_t mean_gpu_ns;
    uint64_t p95_gpu_ns;
    uint64_t p99_gpu_ns;
    uint64_t disjoint_events;
    uint64_t skipped_samples;
} BismuthGpuTiming;

BISMUTH_API const char* bismuth_android_build_id(void);
BISMUTH_API uint32_t bismuth_android_capabilities(void);
BISMUTH_API void* bismuth_create(void);
BISMUTH_API int bismuth_initialize(void* handle, char* error, uint32_t error_capacity);
BISMUTH_API int bismuth_is_initialized(void* handle);
BISMUTH_API int bismuth_upload(void* handle, const BismuthCandidate* candidates, uint32_t count, uint32_t atlas_capacity);
BISMUTH_API int bismuth_dispatch(void* handle, float left, float bottom, float right, float top, uint32_t count);
/* SSBO of uint32 atlas quad slots for visible candidates, stable input order. */
BISMUTH_API uint32_t bismuth_visible_index_buffer(void* handle);
/* Four-word DrawArraysIndirect command (6 vertices, visible candidate count). */
BISMUTH_API uint32_t bismuth_indirect_command_buffer(void* handle);
/* Five-word GLES 3.1 DrawElementsIndirect command: count=visible slots*6, one instance when non-empty; reserved word is zero. */
BISMUTH_API uint32_t bismuth_indexed_command_buffer(void* handle);
/* Packed 16-bit atlas EBO indices in stable visible-slot order; bind as GL_ELEMENT_ARRAY_BUFFER. */
BISMUTH_API uint32_t bismuth_atlas_element_index_buffer(void* handle);

/*
 * Install passive Cocos2d hooks for the exact supported Geometry Dash binary.
 * Call after libcocos2dcpp.so is loaded and before its render path starts.
 * Returns 0 on success, -1 for invalid handle, or -2 on validation/install failure.
 */
BISMUTH_API int bismuth_install_cocos_hooks(void* handle, char* error, uint32_t error_capacity);
/* Enable/disable the experimental all-visible GPU EBO bridge. Disabled by default. */
BISMUTH_API void bismuth_set_gpu_draw_bridge_enabled(int enabled);
BISMUTH_API int bismuth_is_gpu_draw_bridge_enabled(void);
/* Disable/enable passive hook counters and timers for low-overhead A/B runs.
 * This does not unpatch hooks; the original game functions always remain active. */
BISMUTH_API void bismuth_set_passive_telemetry_enabled(int enabled);
BISMUTH_API int bismuth_is_passive_telemetry_enabled(void);
/* GPU timer queries are opt-in. This toggle is thread-safe; GL query work is deferred to the render thread. */
BISMUTH_API void bismuth_set_gpu_timing_enabled(int enabled);
BISMUTH_API int bismuth_is_gpu_timing_enabled(void);
BISMUTH_API int bismuth_is_gpu_timing_supported(void);
BISMUTH_API int bismuth_gpu_timing_snapshot(BismuthGpuTiming* timing);
/* Automatic JNI_OnLoad hook status: 1=installed, -1=allocation failure, -2=validation/install failure, 0=not attempted. */
BISMUTH_API int bismuth_auto_hook_status(void);
BISMUTH_API const char* bismuth_auto_hook_error(void);

/*
 * Passive observer event API. These functions record telemetry; the Cocos
 * hook adapter calls the relevant events after observing original engine calls.
 */
/* Reset batch/atlas counters; call only at a verified frame boundary. */
BISMUTH_API void bismuth_observer_begin_frame(void* handle);
/* Feed one monotonic frame-start timestamp (nanoseconds); first call primes the window. */
BISMUTH_API int bismuth_observer_record_frame_start(void* handle, uint64_t monotonic_ns);
BISMUTH_API void bismuth_observer_record_batch_draw(void* handle, uint32_t child_count);
BISMUTH_API void bismuth_observer_record_sprite_transform(void* handle, int known_base_implementation);
BISMUTH_API void bismuth_observer_record_quad_write(void* handle, int original_call_succeeded);
BISMUTH_API void bismuth_observer_record_structural_mutation(void* handle, int original_call_succeeded);
BISMUTH_API void bismuth_observer_record_dirty_upload(void* handle, int original_upload_succeeded, uint32_t quad_count);
BISMUTH_API void bismuth_observer_invalidate_unknown_mutation(void* handle);
BISMUTH_API int bismuth_observer_snapshot(void* handle, BismuthAtlasTelemetry* telemetry,
                                          uint64_t* slots_revision, uint64_t* content_revision);

/* Destroy while the GLES context that created this instance is current. */
BISMUTH_API void bismuth_destroy(void* handle);

#ifdef __cplusplus
}
static_assert(sizeof(BismuthCandidate) == 32, "BismuthCandidate must match the std430 record");
#endif
