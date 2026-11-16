# Indexed texture-atlas adapter design

## Status

Design plus CPU reference validation helper. The GLES culler is not connected
to Geometry Dash's renderer and does not yet produce a live Cocos2d element
buffer. Do not install a renderer hook or replace a draw until the runtime
state and lifecycle checks below are complete.

## Verified target atlas format

Static disassembly of the supplied Geometry Dash 2.2081 ARM64
`libcocos2dcpp.so` established the following target-specific facts:

- Each atlas quad occupies 96 bytes: four vertices with a 24-byte stride.
- Vertex attributes are position (3 floats at byte 0), normalized color
  (4 unsigned bytes at byte 12), and texture coordinates (2 floats at byte 16).
- The EBO uses six 16-bit indices per quad:
  `[4*q, 4*q+1, 4*q+2, 4*q+3, 4*q+2, 4*q+1]`.
- The atlas initializes its EBO with `GL_STATIC_DRAW`; its quad VBO uses
  `GL_DYNAMIC_DRAW`.
- The normal `CCSpriteBatchNode` path draws the full prefix
  `[0, quadCount)`, and atlas quad records can be shifted/reordered by
  insertion, removal, move, and capacity-resize operations.

These observations are specific to the inspected target binary. They do not
establish that atlas slot identity is stable: it is not. Any candidate-to-slot
mapping must be refreshed or invalidated after structural atlas mutation.

## CPU reference helper

`include/bismuth/atlas_indices.hpp` now provides a host-side reference builder
for the exact six-index pattern and a five-word indexed indirect command
layout. It validates the 16-bit addressable capacity (16,384 quads), rejects
out-of-range slots, and preserves the caller's slot order. The host tests cover
the exact index sequence, empty input, invalid slots, capacity overflow, and
the 20-byte command layout.

This helper is a correctness oracle for a future compute shader; it is not
used by the Android renderer and does not claim a GPU performance improvement.

## GPU output now carries validated atlas slot IDs

The scatter pass now reads each visible candidate's `payload` and writes that
value to the compacted output buffer, rather than writing the candidate's array
index. The C API documents this as a current atlas quad slot. Upload now requires the live atlas capacity and rejects
any payload outside `[0, atlasCapacity)` before dispatch, preventing wrapped
16-bit indices or reads beyond the current atlas vertex range. This lets the GPU produce the
actual slot sequence needed by an indexed-atlas path without a CPU readback.
The host reference helper and tests verify that payload IDs remain in stable
input order and are omitted for culled candidates.

The GLES state guard now preserves indexed SSBO bindings 0..4 because the
scatter pass consumes the candidate buffer at binding 4. Devices must expose at
least five SSBO binding points. The index-compaction pass reuses bindings 0..2. A fourth compute pass now consumes the compacted visible slot IDs and builds a
packed 16-bit element-index buffer on the GPU. It writes three 32-bit words per
quad, each word containing two 16-bit indices, reproducing the exact six-index
atlas pattern without relying on optional 16-bit SSBO storage. The buffer is
exposed separately and is intended to be bound as GL_ELEMENT_ARRAY_BUFFER
with GL_UNSIGNED_SHORT. A shader-storage barrier precedes this pass and an
element-array barrier follows it.

The offsets pass also writes a separate five-word indexed-atlas command
buffer (count = visibleSlotCount * 6, one instance when non-empty, zero
first-index/base-vertex/reserved word). The original four-word arrays command
remains separate and unchanged. The compacted EBO and command are now prepared
as GPU outputs; this project still does not issue an indexed indirect atlas
draw. GLES 3.1 provides glDrawElementsIndirect as a core command; no optional
indirect-draw extension is required once the context is confirmed as ES 3.1.
The fifth command word is reserved and must remain zero (not baseInstance).
The engine's VAO, EBO, texture, blend, stencil and shader state must still be
integrated safely before using the draw path.

## Why a separate indexed path is required

The current prototype writes a four-word `DrawArraysIndirectCommand`
(`count, instanceCount, first, reservedMustBeZero`). GLES indexed
indirect drawing consumes a separate five-word command:

| Word | Field | Meaning |
|---|---|---|
| 0 | count | Number of element indices |
| 1 | instanceCount | Number of instances |
| 2 | firstIndex | Element-buffer offset in indices |
| 3 | baseVertex | Added to each fetched index |
| 4 | reservedMustBeZero | Must be zero in GLES 3.1 |

Never reinterpret the four-word arrays command as an indexed command.

## Remaining invariants before live renderer integration

1. **Candidate mapping:** prove that every culling candidate identifies the
   current atlas slot throughout dispatch-to-draw. Structural mutations must
   invalidate stale mappings.
2. **Painter order:** candidate order must match the engine's atlas order;
   stable scan alone cannot repair an incorrect input order.
3. **Render state:** preserve texture page, UVs, vertex colors, premultiplied
   alpha, blend function, shader, stencil/scissor state, and node uniforms.
4. **VAO/EBO ownership:** bind the generated EBO to a compatible VAO and
   restore the game's state without corrupting a game-owned VAO.
5. **Synchronization:** use shader-storage and element-array barriers between
   compute writes and indexed drawing. Do not use CPU readback or `glFinish`.
6. **Fallback:** retain the original engine draw path for unsupported batches.
7. **Measurement:** compare CPU traversal/submission time, GPU time, draw
   calls, frame-time percentiles, power, and thermals on the same device.

## CPU-overhead constraint

A GPU-generated EBO alone does not remove the engine's per-child visit,
transform update, sorting, or atlas population work. A second CPU traversal
to generate candidate bounds may erase the intended CPU-side benefit. The
future integration must consume data already produced by the engine or prove
that its tracking cost is lower than the work it replaces.

## Acceptance gate

Before a live renderer adapter, obtain a reproducible runtime trace for atlas
slot changes, candidate mapping, GL state, and draw ordering on the target
device. Verify alpha, clipping, blend modes, animation, and fallback behavior.
Only then compare frame-time on the user's phone. The existing native library
is a bootstrap and standalone culler prototype; it is not yet a renderer hook.

## Atlas cache invalidation bookkeeping

The host-side `AtlasRevisionTracker` and its observation contract are documented
in [atlas-lifecycle-contract.md](atlas-lifecycle-contract.md). It distinguishes
structural slot changes from in-place quad writes so future observers can reject
stale slot maps and stale quad-derived data independently. The tracker is not
wired to the game and does not change the renderer.
