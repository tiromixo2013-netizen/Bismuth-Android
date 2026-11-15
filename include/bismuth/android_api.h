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
    BISMUTH_CAP_GLES31_COMPUTE = 1u << 1
};

/* Matches the 32-byte std430 Candidate record used by the compute shaders. */
typedef struct BismuthCandidate {
    float left;
    float bottom;
    float right;
    float top;
    uint32_t payload;
    uint32_t pad0;
    uint32_t pad1;
    uint32_t pad2;
} BismuthCandidate;

BISMUTH_API const char* bismuth_android_build_id(void);
BISMUTH_API uint32_t bismuth_android_capabilities(void);
BISMUTH_API void* bismuth_create(void);
BISMUTH_API int bismuth_initialize(void* handle, char* error, uint32_t error_capacity);
BISMUTH_API int bismuth_is_initialized(void* handle);
BISMUTH_API int bismuth_upload(void* handle, const BismuthCandidate* candidates, uint32_t count);
BISMUTH_API int bismuth_dispatch(void* handle, float left, float bottom, float right, float top, uint32_t count);
BISMUTH_API uint32_t bismuth_visible_index_buffer(void* handle);
BISMUTH_API uint32_t bismuth_indirect_command_buffer(void* handle);
/* Destroy while the GLES context that created this instance is current. */
BISMUTH_API void bismuth_destroy(void* handle);

#ifdef __cplusplus
}
static_assert(sizeof(BismuthCandidate) == 32, "BismuthCandidate must match the std430 record");
#endif
