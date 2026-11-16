# Bismuth-Android

Android-native GPU atlas visibility-culling prototype for Geometry Dash 2.2081 ARM64.

This branch contains an experimental renderer bridge. It is independent of the Geode/Bismuth-PC source code and uses general GLES 3.1 compute techniques.

> **Status: experimental integration build.** The build-ID-gated Cocos hooks and GPU atlas-culling path are compiled, but real-device visual correctness and FPS gains still require testing. This is a native `libbismuth.so` build, not a `.geode` package.

## Current GPU path

For compatible atlas draws containing at least 256 quads, the bridge:

1. Reads the live atlas VBO position stream directly as an SSBO.
2. Uses the active Cocos MVP matrix to compute conservative clip-space bounds for each quad on the GPU.
3. Classifies visible quads and performs a stable prefix/scatter in original atlas order.
4. Emits packed 16-bit atlas indices in the same scatter dispatch.
5. Submits the compacted index stream with `glDrawElementsIndirect`.

The bridge is enabled after successful installation of the exact-build Cocos hooks. If the expected vertex layout, VBO range, MVP uniform, or GLES 3.1 path is unavailable, the hook falls back to the original Cocos draw. Small atlas batches below 256 quads always use the original path to avoid compute-dispatch overhead.

## Important limits

- The original `CCSpriteBatchNode::draw()` child loop and `CCSprite::updateTransform()` calls still run on the CPU. This branch does not yet bypass or cache those transforms.
- GPU culling can reduce off-screen vertex/index processing, but its net frame-time benefit depends on scene visibility, batch size, and the phone's GPU driver.
- The MVP uniform is detected from known Cocos names; unsupported shaders fall back to the original draw.
- Atlas capacities above 16,384 quads are not handled by the 16-bit index path and fall back.
- Runtime shader execution, GL state behavior, visual correctness, thermals, and CPU/GPU frame-time p50/p95/p99 have not yet been measured on a physical device.
- No FPS percentage or guaranteed performance gain is claimed.

## Build and test

Host reference tests:

```bash
cmake -S . -B build -DBISMUTH_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Android ARM64 GLES 3.1 build:

```bash
cmake -S . -B build-android \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-26 \
  -DBISMUTH_ENABLE_GLES31=ON \
  -DBISMUTH_BUILD_TESTS=OFF
cmake --build build-android --parallel
```

The CI workflow validates all compute shaders, runs host tests, builds the Android ARM64 shared library, and checks the resulting ELF and exported C API.

## Native loader

The output is `libbismuth.so`. See [docs/android-loader.md](docs/android-loader.md) for APK loading preparation. The helper patches the Geometry Dash APK's library-load sequence; it does not rebuild or sign the APK.

This repository does not currently build a Geode package. A `.geode` wrapper would require a Geode mod entry point and package metadata; renaming the native shared library is not sufficient.

## Next performance stage

The main remaining CPU-side cost identified in the target binary is the per-child `updateTransform()` loop inside `CCSpriteBatchNode::draw()`. A safe cache/fast path needs verified dirty-state and parent-transform invalidation rules. Until those are mapped and validated, the original transform calls remain intact.
