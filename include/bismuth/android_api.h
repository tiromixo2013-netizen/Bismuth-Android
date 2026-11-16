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
    BISMUTH_CAP_COCOS_PASSIVE_HOOKS = 1u << 4
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

/* Per-frame counters exported by the passive observer event API. */
typedef struct BismuthAtlasTelemetry {
    uint64_t batch_draw_calls;
    uint64_t sprite_transform_calls;
    uint64_t unknown_transform_overrides;
    uint64_t quad_writes;
    uint64_t structural_mutations;
    uint64_t dirty_uploads;
    uint64_t uploaded_quads;
    uint32_t max_children_in_batch;
} BismuthAtlasTelemetry;

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

/*
 * Passive observer event API. These functions record telemetry; the Cocos
 * hook adapter calls the relevant events after observing original engine calls.
 */
BISMUTH_API void bismuth_observer_begin_frame(void* handle);
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
