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

## Why a separate indexed path is required

The current prototype writes a four-word `DrawArraysIndirectCommand`
(`vertexCount, instanceCount, firstVertex, baseInstance`). GLES indexed
indirect drawing consumes a separate five-word command:

| Word | Field | Meaning |
|---|---|---|
| 0 | count | Number of element indices |
| 1 | instanceCount | Number of instances |
| 2 | firstIndex | Element-buffer offset in indices |
| 3 | baseVertex | Added to each fetched index |
| 4 | baseInstance | First instance |

Never reinterpret the four-word arrays command as an indexed command.

## Remaining invariants before GPU implementation

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
