#include <jni.h>
#include <cstdint>
#include <new>
#include <string>
#include <vector>
#include "bismuth/gles31_culler.hpp"

namespace {
struct BismuthInstance { bismuth::Gles31Culler culler; };
void writeError(char* out, std::uint32_t capacity, const std::string& message) {
    if (!out || capacity == 0) return;
    const auto n = message.size() < capacity - 1 ? message.size() : capacity - 1;
    message.copy(out, n);
    out[n] = '\0';
}
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM*, void*) {
    return JNI_VERSION_1_6;
}
extern "C" __attribute__((visibility("default")))
const char* bismuth_android_build_id() { return "Bismuth-Android 0.2.0-native-api"; }
extern "C" __attribute__((visibility("default")))
std::uint32_t bismuth_android_capabilities() { return 1u; }

extern "C" __attribute__((visibility("default")))
void* bismuth_create() { return new (std::nothrow) BismuthInstance{}; }

extern "C" __attribute__((visibility("default")))
int bismuth_initialize(void* handle, char* error, std::uint32_t errorCapacity) {
    if (!handle) return -1;
    std::string message;
    const bool ok = static_cast<BismuthInstance*>(handle)->culler.initialize(&message);
    if (!ok) writeError(error, errorCapacity, message);
    return ok ? 0 : -2;
}

// Candidate is 32 bytes: bounds(left,bottom,right,top), payload, three padding uints.
extern "C" __attribute__((visibility("default")))
int bismuth_upload(void* handle, const bismuth::Candidate* candidates, std::uint32_t count) {
    if (!handle || (count && !candidates)) return -1;
    std::vector<bismuth::Candidate> input;
    if (count) input.assign(candidates, candidates + count);
    return static_cast<BismuthInstance*>(handle)->culler.uploadCandidates(input) ? 0 : -2;
}

extern "C" __attribute__((visibility("default")))
int bismuth_dispatch(void* handle, float left, float bottom, float right, float top,
                     std::uint32_t count) {
    if (!handle) return -1;
    return static_cast<BismuthInstance*>(handle)->culler.dispatch(
        bismuth::Rect{left, bottom, right, top}, count) ? 0 : -2;
}
extern "C" __attribute__((visibility("default")))
std::uint32_t bismuth_visible_index_buffer(void* handle) {
    return handle ? static_cast<BismuthInstance*>(handle)->culler.visibleIndexBuffer() : 0u;
}
extern "C" __attribute__((visibility("default")))
std::uint32_t bismuth_indirect_command_buffer(void* handle) {
    return handle ? static_cast<BismuthInstance*>(handle)->culler.indirectCommandBuffer() : 0u;
}
extern "C" __attribute__((visibility("default")))
void bismuth_destroy(void* handle) {
    // Call while the GLES context that owns this instance is current.
    delete static_cast<BismuthInstance*>(handle);
}
