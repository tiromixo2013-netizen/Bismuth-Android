# Bismuth-Android

Experimental Android ARM64 Geode mod and GLES 3.1 compute-culling research project for Geometry Dash 2.2081.

> **Current status: safe observer/prototype build.** The Geode package compiles, but the GPU draw bridge is hard-locked OFF. The original Cocos renderer remains active. No GPU culling integration, CPU offload, FPS gain, or stable release is claimed.

## Implemented

- Geode Android64 package and native C ABI.
- Build-ID-gated passive Cocos2d atlas lifecycle hooks for the inspected target binary.
- CPU reference culler with stable input ordering.
- GLES 3.1 compute backend: classify, workgroup scan, group offsets, stable scatter, and atlas element-index generation.
- 16-bit atlas EBO pattern matching the inspected 2.2081 target binary.
- Fail-closed command handling, SSBO binding-range preservation, and host-side tests.
- Conservative quad-to-NDC bounds helper for caller-supplied transformed vertices and a verified column-major MVP matrix. Unsafe projection fails open to the full view.
- Duplicate atlas-slot candidate rejection before compute upload.
- CI checks for host tests, shader validation, Android ARM64 native compilation, and Geode packaging.

## What is deliberately not active

- The live game renderer does not yet supply calibrated per-quad positions/bounds to the compute backend.
- No sprite transform calls are skipped.
- No original atlas draw call is replaced and no generated EBO is submitted by Geometry Dash.
- The GPU draw bridge cannot be enabled through the native API or Geode setting.
- GLES compute execution and visual correctness have not been validated on the target phone/GPU.

## Build and test

Host reference tests:

```bash
cmake -S . -B build -DBISMUTH_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Android backend: build with Android NDK and `BISMUTH_ENABLE_GLES31=ON`. The owning GLES 3.1 context must be current for initialization, dispatch, reset, and destruction.

Geode Android64 packaging is performed by the repository's GitHub Actions workflow. A successful package build verifies compilation and packaging only; it does not prove runtime safety or performance.

## Remaining work before a stable GPU-optimized release

1. Establish a safe, measured source of the exact four transformed vertices for each eligible quad.
2. Validate the actual per-frame MVP/viewport mapping and conservative clip-space bounds on the target device.
3. Map all transform invalidation, atlas slot reordering, subclass behavior, and unsupported sprite cases.
4. Prototype a gated draw adapter that preserves the original VBO, vertex attributes, VAO, texture, blend, alpha, stencil/scissor, shader, and painter order; retain CPU fallback.
5. Run visual regression and crash testing on the target phone, then profile CPU/GPU frame times, p50/p95/p99, power, and thermals against the unmodified renderer.

See [docs/renderer-integration.md](docs/renderer-integration.md) for the binary analysis and integration blockers, and [geode-mod/README.md](geode-mod/README.md) for the package-specific status.
