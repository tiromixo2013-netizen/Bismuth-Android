# Indexed texture-atlas adapter design

## Status

Design only. The current GLES culler is not connected to Geometry Dash's
renderer and does not produce a Cocos2d-compatible element buffer. Do not
enable this path or install a renderer hook until the exact Geometry Dash
2.2081 ARM64 binary and its runtime GL state have been inspected.

## Why a separate indexed path is required

The current prototype writes a four-word `DrawArraysIndirectCommand`
(`vertexCount, instanceCount, firstVertex, baseInstance`). A conventional
Cocos2d texture atlas instead submits indexed quads. GLES indexed indirect
drawing consumes a five-word command:

| Word | Field | Meaning |
|---|---|---|
| 0 | count | Number of element indices |
| 1 | instanceCount | Number of instances |
| 2 | firstIndex | Element-buffer offset in indices |
| 3 | baseVertex | Added to each fetched index |
| 4 | baseInstance | First instance |

The command must be a distinct buffer with the correct five-word layout.
The existing arrays command must never be reinterpreted as an indexed command.

## Candidate mapping required by an adapter

The current culler returns candidate IDs in stable input order. An atlas
adapter would need a verified mapping from each candidate ID to the first
vertex of its corresponding quad. A common four-vertices-per-quad layout
would map quad slot `q` to vertices `4*q .. 4*q+3`, but this is only a
hypothesis until the target binary's atlas VBO layout and update behavior are
verified.

For each visible quad, a compute pass could emit six element indices in the
same winding/order used by the engine's own atlas index buffer. It could also
write an indexed indirect command whose element count is six times the
visible-quad count. The element type (16-bit or 32-bit), winding, base vertex,
and command offset must match the actual draw path. Do not assume the public
Cocos2d source exactly matches the shipped Geometry Dash binary.

## Required invariants before implementation

1. **Vertex layout:** establish the exact atlas vertex stride, attribute
   offsets, data types, VBO binding, and whether vertices are rewritten or
   reordered each frame.
2. **Quad mapping:** prove that candidate IDs map to stable atlas quad slots
   for the whole dispatch-to-draw interval. If the atlas compacts or reorders
   quads, the mapping must be updated without a CPU traversal that erases the
   intended CPU savings.
3. **Index safety:** prove that every generated index is within the currently
   populated vertex range and that the selected element type cannot overflow.
4. **Painter order:** preserve the engine's submission order exactly. Do not
   sort visible IDs by texture or depth unless the engine already groups them
   compatibly and no ordering dependency is crossed.
5. **Render state:** preserve texture page, UVs, vertex colors, premultiplied
   alpha, blend function, shader, stencil/scissor state, and any per-node
   uniforms.
6. **VAO/EBO ownership:** bind the intended element buffer to a compatible
   VAO and restore all engine state after compute and draw. Do not mutate a
   VAO owned by the game without a proven restoration strategy.
7. **Synchronization:** use the required shader-storage and element-array
   barriers between compute writes and indexed drawing. Do not use CPU
   readback, `glFinish`, or a per-frame GPU wait as a substitute.
8. **Fallback:** keep the original engine atlas draw path available and
   select the GPU path only for a fully validated compatible batch.
9. **Measurement:** compare CPU traversal/submission time, GPU time, draw
   calls, frame-time percentiles, power, and thermals on the same device.

## CPU-overhead constraint

A GPU-generated element buffer alone does not remove the engine's per-child
visit, transform update, sorting, or atlas population work. The adapter must
identify a way to avoid or reduce that work using data already available to
the renderer, while retaining correct transforms and ordering. If it still
requires a second CPU loop over every sprite to create candidate bounds, the
claimed CPU-side benefit may disappear.

## Acceptance gate

Implementation should begin only after obtaining and inspecting the exact
2.2081 ARM64 `libcocos2dcpp.so` (or a reproducible disassembly/symbol map
for that exact build), validating the relevant atlas structures and call
sequence, and confirming behavior with a real GLES 3.1 context. Public Cocos2d
headers and third-party symbol lists are reconnaissance references, not
sufficient proof for offsets or structure layouts.
