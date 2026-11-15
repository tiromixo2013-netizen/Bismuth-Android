#include <jni.h>
#include <cstdint>

// Load-time entry point only. Renderer hooks are intentionally not installed here:
// they must be added only after the exact 2.2081 ARM64 call sites are verified.
extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM*, void*) {
    return JNI_VERSION_1_6;
}

extern "C" __attribute__((visibility("default")))
const char* bismuth_android_build_id() {
    return "Bismuth-Android 0.1.0-prototype";
}

extern "C" __attribute__((visibility("default")))
std::uint32_t bismuth_android_capabilities() {
#if defined(__ANDROID__)
    return 1u; // Native Android library loaded; GLES context not checked yet.
#else
    return 0u;
#endif
}
