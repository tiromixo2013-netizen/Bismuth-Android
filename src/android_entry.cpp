#include <jni.h>
#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include <vector>
#include "bismuth/android_api.h"
#include "bismuth/gles31_culler.hpp"

namespace {
struct BismuthInstance { bismuth::Gles31Culler culler; };
void writeError(char* out, std::uint32_t capacity, const std::string& message) {
    if (!out || capacity == 0) return;
    const auto n = message.size() < capacity - 1 ? message.size() : capacity - 1;
    message.copy(out, n);
    out[n] = '\0';
}
static_assert(sizeof(BismuthCandidate) == sizeof(bismuth::Candidate),
              "C API candidate layout must match the GLES shader record");
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM*, void*) {
    return JNI_VERSION_1_6;
}
extern "C" const char* bismuth_android_build_id() {
    return "Bismuth-Android 0.3.0-c-api";
}
extern "C" std::uint32_t bismuth_android_capabilities() {
    return BISMUTH_CAP_NATIVE_LIBRARY | BISMUTH_CAP_GLES31_COMPUTE;
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
    // Reject oversized input before allocating/copying user-provided records.
    if (count > BISMUTH_MAX_CANDIDATES) return -2;
    std::vector<bismuth::Candidate> input;
    if (count) {
        input.resize(count);
        std::memcpy(input.data(), candidates, static_cast<std::size_t>(count) * sizeof(BismuthCandidate));
    }
    return static_cast<BismuthInstance*>(handle)->culler.uploadCandidates(input, atlasCapacity) ? 0 : -2;
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
extern "C" void bismuth_destroy(void* handle) {
    // Call while the GLES context that owns this instance is current.
    delete static_cast<BismuthInstance*>(handle);
}
