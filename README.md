# Bismuth-Android

Experimental Android-native GPU visibility-culling prototype for Geometry Dash 2.2081 ARM64.

The project explores a GPU-assisted path for classifying sprite bounds, compacting visible indices in stable submission order, and preparing an indirect-draw command using OpenGL ES 3.1 compute shaders. It is a clean-room implementation of general rendering concepts; it does not copy Bismuth PC source code.

> **Status: prototype only.** This is not an installable Geometry Dash mod and is not yet integrated with the game's renderer. No Android device performance claim is made.

## What exists

- Portable CPU reference culler with stable input ordering.
- Optional Android GLES 3.1 compute backend.
- Three compute stages: classify + per-workgroup scan, group offsets, stable scatter.
- Embedded GPU shaders are generated directly from `shaders/*.comp` during CMake configuration.
- No CPU readback or `glFinish` in the compute dispatch path.
- Host-side unit tests and GitHub Actions CI.

## Build host reference tests

```bash
cmake -S . -B build -DBISMUTH_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Android build

Use Android NDK + CMake and set `-DBISMUTH_ENABLE_GLES31=ON`. The calling application must create and own an active GLES 3.1 context before initializing `Gles31Culler`.

## Important limitations

- No Geometry Dash hooks, sprite-bound extraction, texture/material batching, or game integration yet.
- The current compute implementation supports up to 16,384 candidates per dispatch.
- The generic indirect command is not yet wired into Geometry Dash's draw path.
- GPU shaders have not yet been validated across real Android drivers.
- No FPS or latency improvement is claimed until on-device A/B profiling is complete.

See [docs/renderer-integration.md](docs/renderer-integration.md) for the required integration work. See [docs/performance-telemetry.md](docs/performance-telemetry.md) for the first CPU timing counters and their limitations.

## Native shared-library API (bootstrap)

The Android build now exports a small C ABI from `libbismuth.so`: `bismuth_create`,
`bismuth_initialize`, `bismuth_upload`, `bismuth_dispatch`, buffer-handle getters,
and `bismuth_destroy`. Create and call the instance only on a thread with the owning
GLES 3.1 context current. The caller supplies conservative candidate bounds in the
renderer submission order. The visible-index and indirect-command buffer handles are
for integration code to consume; this library does not yet issue Geometry Dash draw calls.

This API makes the culling backend callable from native integration code, but it is
still not a Geometry Dash hook or installable mod package.


## Android C integration header

Native consumers can include `bismuth/android_api.h` without depending on C++ headers.
`BismuthCandidate` is a fixed 32-byte C struct matching the compute shader's std430 record. `BISMUTH_MAX_CANDIDATES` exposes the current 16,384-record per-dispatch ceiling; oversized C uploads are rejected before temporary allocation.
The capability bits describe what was compiled into the library; they do not mean that a
GLES context exists or that the culler has initialized. Call `bismuth_initialize` only
on the render thread while the intended GLES 3.1 context is current, then check
`bismuth_is_initialized`. The owning context must also be current when destroying an instance.

The CI suite compiles this header as C as well as C++, so ABI declarations remain usable
from a future Android loader or JNI bridge.


## Android APK loader helper

See [docs/android-loader.md](docs/android-loader.md) for the ARM64 APK loading preparation
and the limitations of the current bootstrap. The helper script validates and copies
`libbismuth.so` and patches the Geometry Dash Smali library-load call; it does not rebuild
or sign the APK and it does not activate renderer hooks.

## Inspect the exact target game library

Before attempting renderer hooks, inspect the `libcocos2dcpp.so` extracted from
the user's exact Geometry Dash 2.2081 ARM64 APK:

```bash
python3 tools/inspect_target_elf.py /path/to/libcocos2dcpp.so
```

The tool validates ELF64 little-endian AArch64 shared-library format and lists exported
dynamic symbols whose names mention the Cocos2d node, sprite, batch-node, or texture-atlas
classes. A stripped library may not expose these names. Reported values are ELF symbol
values, not runtime hook addresses, and do not establish that a function is safe to hook.
