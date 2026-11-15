# Renderer integration requirements

This repository is an experimental culling component, not yet a Geometry Dash mod.
The compute pipeline produces a stable list of visible candidate indices. It does not
currently hook Geometry Dash, discover sprite bounds, replace its renderer, or submit
its real textures/materials as one batch.

A real integration must:

1. Identify the exact Android 2.2081 ARM64 renderer entry points and establish a safe hook strategy.
2. Extract conservative world-space bounds for each eligible sprite without changing gameplay state.
3. Preserve painter order. The current stable scan preserves candidate input order; the caller must feed candidates in the renderer's original submission order.
4. Preserve alpha, premultiplied-alpha conventions, blend modes, clipping/stencil state, texture sampling, shader effects, and z-order.
5. Batch only compatible sprites (same render state/material/texture page); never merge across an ordering dependency.
6. Keep a CPU fallback for devices without GLES 3.1 compute support and for unsupported render paths.
7. Measure CPU submission time, GPU time, draw calls, frame-time p50/p95/p99, power and thermals with feature enabled/disabled on the same device.

## Current prototype limitations

- One compute dispatch supports at most 16,384 candidates (128 groups of 128).
- Group prefix scan is a single workgroup; larger scenes need a hierarchical scan.
- Candidate upload currently uploads the full array with glBufferSubData.
- The output command is a generic six-vertex instanced command; the game renderer must be explicitly adapted before it can be consumed.
- GLES shader compilation and behavior must be verified on actual Android GPU drivers.
- The host reference test does not validate GPU synchronization or visual correctness.

## GLES backend ownership and lifecycle

- Create and make current a GLES 3.1 context before initialize().
- Call uploadCandidates() before dispatch; the count passed to dispatch() must equal the latest successful upload count.
- A zero-count dispatch explicitly writes an indirect command with zero instances, preventing a stale previous frame's instance count from being reused.
- All methods that issue GL commands, including reset() and destruction, require the owning context to be current on the calling thread.
- Call reset() before destroying or losing the GL context. After context loss, discard the old object state and initialize a fresh backend after a new context is current.
- The backend checks the compute workgroup, SSBO binding, SSBO block-size, and dispatch-count limits it relies on. This does not guarantee shader correctness on every vendor driver.
- The backend is a static library component; an Android app or mod loader still needs to provide lifecycle integration and package/load it.

## Next Android validation

Build the ARM64 backend with the Android NDK CMake toolchain. The CI build checks compilation/linking only; shader execution still requires a real GLES 3.1 context on target hardware.
