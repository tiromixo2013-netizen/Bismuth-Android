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

- One compute dispatch supports at most 16,384 candidates (128 groups of 128). The public C API exposes this as `BISMUTH_MAX_CANDIDATES` and rejects oversized uploads before allocating a temporary copy.
- Dispatch rejects non-finite or inverted view rectangles; zero-area rectangles are permitted.
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


## Important Cocos2d batching finding

The public OpenGD 1.71 Cocos2d source is useful architectural evidence, but it is not
proof of the exact Geometry Dash 2.2081 binary implementation. In that source,
`CCSpriteBatchNode::draw()` calls `updateTransform()` for its children and then calls
`CCTextureAtlas::drawQuads()`; its visit path also sorts children before draw. The
batch node is already designed to submit compatible same-texture sprites as one draw
call. Therefore, replacing only the final atlas draw with a GPU culler would not
eliminate the per-child CPU transform/update work. A real CPU-overhead reduction must
also avoid or reduce that per-sprite CPU update path, or prove that another GD 2.2081
renderer path dominates the cost.

The next adapter investigation is consequently split into two separate questions:

1. Can conservative visibility be determined from already-available atlas/world data
   without adding another per-sprite CPU traversal?
2. Can the atlas quad stream be consumed by a GPU-visible-index draw path while
   preserving the exact quad vertex format, texture coordinates, vertex colors,
   premultiplied-alpha blend state, painter order, and any shader/stencil effects?

Until both are answered against the exact 2.2081 ARM64 binary and a real GLES context,
the culler remains a standalone backend and must not be presented as an active game
optimization.

## Public 2.2081 ARM64 symbol reconnaissance (candidate hooks only)

A public Android native-mod template explicitly targets Geometry Dash 2.2081 ARM64 and
includes an `Addr.txt` dynamic-symbol listing. Its listed Cocos2d symbols provide useful
candidate investigation points (values below are ELF symbol values from that listing):

| Symbol | Listed value | Why inspect it |
|---|---:|---|
| `cocos2d::CCNode::visit()` | `0x00acce40` | Scene traversal boundary; likely too high-level for safe batching. |
| `cocos2d::CCSprite::draw()` | `0x00b15700` | Per-sprite submission path; hooking alone would not remove CPU traversal. |
| `cocos2d::CCSpriteBatchNode::visit()` | `0x00b1711c` | Batch-node traversal boundary. |
| `cocos2d::CCSpriteBatchNode::draw()` | `0x00b17034` | Candidate batch submission boundary. |
| `cocos2d::CCTextureAtlas::drawNumberOfQuads(unsigned int,unsigned int)` | `0x00b33af0` | Candidate atlas draw boundary where quad count/order and VBO ownership must be understood. |

Reference: [ItzZyann/gd-native-mod-android](https://github.com/ItzZyann/gd-native-mod-android)
(the repository README states its target is GD 2.2081 Android arm64-v8a; its `Addr.txt`
contains the symbol table).

**These are research leads, not approved hook offsets.** The address values must be checked
against the exact target `libcocos2dcpp.so` build and its ELF load layout before use.
A symbol being present does not establish that the function is safe to replace. In particular,
the existing Culler output is not compatible with Cocos2d's texture-atlas VBO/index layout.
The next implementation task is to inspect the batch/atlas data structures and draw state,
then design a state-preserving backend for a single compatible material batch. Do not hook
`CCSprite::draw()` as a supposed CPU-overhead fix: that would still leave the engine's
per-sprite traversal and call overhead in place.
