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
- Non-finite or inverted candidate bounds are rejected by both the CPU reference and GPU classify shader; callers should still provide conservative, normalized world-space bounds.
- The host reference test does not validate GPU synchronization or visual correctness.

### Texture-atlas draw compatibility check

The public Cocos2d source used for reconnaissance shows that the atlas draw is indexed:
its quad index pattern is six `GLushort` indices per quad, and `drawNumberOfQuads`
binds the atlas texture and the atlas array/element buffers before calling
`glDrawElements(GL_TRIANGLES, ...)`. The exact uploaded target binary is inspected below;
its draw routine does not show an explicit `glBindVertexArray` call.
GLES 3.1 does provide `glDrawElementsIndirect`, but its command is a five-word
indexed-draw structure, not the current four-word `glDrawArraysIndirect` structure.

A future atlas adapter therefore cannot simply pass the current command buffer to the
engine draw call. It needs a separate, correctly sized indexed command and a GPU-built
element-index buffer (or an equally validated vertex-shader remapping design), plus a
compatible VAO that references the game's atlas vertex buffer without corrupting the
engine's VAO state. The atlas vertex layout, VBO/VAO ownership, and the exact game's
draw configuration must be verified from the 2.2081 ARM64 binary before implementation.

## Conservative clip-space bounds utility

`bismuth/clip_bounds.hpp` now provides a small, host-testable projection helper for
four caller-supplied quad vertices and an OpenGL column-major MVP matrix. It computes
the NDC rectangle from all four projected vertices. If the matrix or positions are
non-finite, or any projected vertex has an unsafe/non-positive clip-space W, it fails
open to the full NDC view rather than risk incorrectly dropping visible geometry.

This is only a mathematical utility. It does **not** extract vertices from
`CCSprite` or `CCTextureAtlas`, does not discover the game's MVP matrix, and is not
called by the live hook path. Its purpose is to make the eventual projection step
independently testable once a verified source of quad vertices and the correct
per-frame transform matrix are available.

The compute upload path also rejects duplicate candidate payloads within one atlas
upload. A candidate list intended to represent atlas quads must map one-to-one to
current atlas slots; duplicate slots could otherwise be emitted twice and change
alpha blending. This check does not establish that the caller's slot mapping is
current—structural mutation tracking and frame-by-frame mapping validation remain
required.

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


## Exact uploaded ARM64 library inspection (2026-11-16)

An uploaded `libcocos2dcpp.so` was inspected directly. ELF metadata:

- ELF64, little-endian, `ET_DYN`, `EM_AARCH64`; 19,077,352 bytes.
- GNU Build ID: `ca621323d54c1d94f5f13a2778ce9c8baa31b9a8`.
- The file is marked stripped, but its dynamic symbol table still exposes the Cocos2d functions below.
- SHA-256: `0926fc6ea6802128f057008b7840708c46765acb8df57d1968c966e41d72089d`.

The following dynamic symbols were found in this exact file. Their ELF values match the candidate values from the public 2.2081 symbol listing above:

| Symbol | ELF value | Size |
|---|---:|---:|
| `cocos2d::CCNode::visit()` | `0x00acce40` | 348 |
| `cocos2d::CCSprite::draw()` | `0x00b15700` | 284 |
| `cocos2d::CCSpriteBatchNode::visit()` | `0x00b1711c` | 172 |
| `cocos2d::CCSpriteBatchNode::draw()` | `0x00b17034` | 232 |
| `cocos2d::CCTextureAtlas::drawNumberOfQuads(unsigned int,unsigned int)` | `0x00b33af0` | 336 |
| `cocos2d::CCTextureAtlas::drawQuads()` | `0x00b33c40` | 12 |

Disassembly observations from this uploaded binary:

- `CCSpriteBatchNode::draw()` performs a loop over child pointers and makes one indirect virtual call per child before setting the blend function and branching to `CCTextureAtlas::drawQuads()`. This confirms a per-child CPU operation in the batch draw path. The exact semantic name of that virtual slot still needs vtable/type-layout confirmation; do not assume its identity solely from this loop.
- `CCTextureAtlas::drawNumberOfQuads()` binds the atlas array buffer, configures vertex attributes, binds the element buffer, and calls `glDrawElements` with `GL_TRIANGLES` and `GL_UNSIGNED_SHORT`. The visible non-dirty path configures a 24-byte quad-vertex stride with position (3 floats at offset 0), color (4 unsigned bytes at offset 12), and texture coordinates (2 floats at offset 16). A dirty-data path updates the array buffer with `glBufferSubData` before drawing.
- `CCTextureAtlas::drawQuads()` is a short wrapper that branches to the two-argument draw routine.

This is materially stronger than the earlier public-source-only reconnaissance: the relevant symbols and their values are present in the actual uploaded binary, and the atlas draw behavior above comes from its disassembly. However, the ELF alone does **not** prove the installed game's marketing/version number is 2.2081; the user-provided APK archive name included `2.2.144`, so the APK/package version must be independently checked before labelling this binary an exact 2.2081 build. Matching offsets are strong corroboration, not a substitute for that version check.

These symbol values are ELF virtual addresses, not absolute process addresses. Runtime addresses require the module's actual load bias. They are investigation points only; no hook has been installed or validated. The current culler still cannot replace this indexed atlas draw path, and no GPU-side visibility integration is implemented yet.


## Exact draw-path details confirmed from uploaded target disassembly

The following details are from AArch64 disassembly of the uploaded library, not inferred
from public Cocos2d source:

- `CCSpriteBatchNode::draw()` loads a child-pointer array through the object at
  `this + 0x148`. It reads the child count at array-object offset `+0x38`, the
  pointer to child entries at `+0x10`, then iterates 8-byte child pointers.
- For each non-null child, it loads the child's vtable and calls the function pointer
  at vtable byte offset `0x308`. In the target's exported `CCSprite` vtable, that slot
  relocates to `cocos2d::CCSprite::updateTransform()` at ELF value `0x00b161d0`.
  This confirms a per-child CPU transform update for ordinary CCSprite objects.
  Subclasses may override the virtual slot, so a runtime adapter must account for
  dynamic child types. The initial virtual call at vtable offset `0x50` gates the
  draw path; its semantic identity is not yet established.
- After the child loop, it applies the batch node's blend function and branches to
  `CCTextureAtlas::drawQuads()`.
- In `CCTextureAtlas::drawNumberOfQuads(start, count)`, the normal non-dirty path
  binds the texture, binds the atlas VBO to `GL_ARRAY_BUFFER`, and configures:
  position = 3 x `GL_FLOAT`, offset 0;
  color = 4 x `GL_UNSIGNED_BYTE`, normalized, offset 12;
  texture coordinates = 2 x `GL_FLOAT`, offset 16;
  stride = 24 bytes for all three attributes.
- It binds the atlas element buffer to `GL_ELEMENT_ARRAY_BUFFER` and calls
  `glDrawElements(GL_TRIANGLES, count * 6, GL_UNSIGNED_SHORT,
  start * 6 * sizeof(GLushort))`. It then unbinds both array and element buffers.
- When the atlas data is dirty, the function first calls `glBufferSubData` to
  update the requested vertex range, clears the dirty flag, then follows the same
  attribute/indexed-draw path.
- No explicit `glBindVertexArray` appears in this function's disassembly. The
  adapter must therefore not assume the public-source VAO behavior; it must inspect
  the actual GLES context/version path and preserve the game's attribute/buffer state.

### Updated integration consequence

The target binary confirms the atlas uses a 24-byte interleaved vertex record and
16-bit indexed quads, but this still does not prove a stable mapping from candidate
sprite IDs to atlas quad slots across sorting, atlas updates, or child transform work.
The child loop is an explicit CPU-side per-child virtual-call loop, and the default
CCSprite vtable proves that the slot calls `CCSprite::updateTransform()`. The next safe
investigation is to determine which child subclasses occur in live batches and whether
atlas quad slots remain stable after transform updates, sorting, and dirty-buffer uploads. Do not bypass this loop or synthesize replacement indices until those facts
are established.

### Vtable resolution: per-child transform update confirmed

The `CCSprite` vtable symbol is at ELF value `0x001218510`; its address point is
`0x001218520`. The relocation at vtable slot `+0x308` (address
`0x001218828`) resolves to `0x00b161d0`, the exported
`cocos2d::CCSprite::updateTransform()` symbol. This resolves the indirect call
inside `CCSpriteBatchNode::draw()` for ordinary CCSprite objects. The function itself
contains further transform/visibility-related state checks and transform work.

This confirms that the batch draw path invokes `updateTransform()` once per child
before issuing the atlas draw for the base CCSprite vtable. It does not prove that
skipping this call is safe: the method may update vertex positions, dirty flags,
visibility state, or subclass-specific behavior. A replacement must preserve those
effects or prove that cached atlas data is already valid for the frame.
### Transform-to-atlas dependency found in CCSprite::updateTransform

Further disassembly of `CCSprite::updateTransform()` shows why bypassing the child loop
is unsafe. The routine checks per-sprite state, obtains a transform through a virtual
call, composes affine transforms, writes transformed quad-related fields in the sprite
object, and in the dirty/update path calls
`CCTextureAtlas::updateQuad(cocos2d::_ccV3F_C4B_T2F_Quad*, unsigned int)` at ELF
value `0x00b332ac`. The call is prepared with a quad pointer at `this + 0x1cc`,
an atlas pointer loaded from `this + 0x160`, and an atlas index loaded from
`this + 0x168`.

This establishes a concrete dependency chain:

`CCSpriteBatchNode::draw()` -> per-child virtual `CCSprite::updateTransform()` ->
transform/quad data update -> `CCTextureAtlas::updateQuad()` when the relevant
sprite state requires it -> indexed atlas draw.

Therefore, a safe optimization cannot simply skip `updateTransform()` for every
child. A viable direction to investigate is a conservative static/unchanged-sprite
fast path that reuses already-valid atlas quad data, while retaining the original
update path for dirty, animated, transformed, subclassed, or otherwise unsupported
sprites. The dirty-state semantics and invalidation triggers must be proven before
implementing such a fast path. This is not implemented yet.
### Detailed updateTransform control flow (target disassembly)

Additional instruction-level inspection of the full 1,072-byte
`CCSprite::updateTransform()` body refines the earlier call-chain finding:

- It begins with a virtual call through the object's vtable at byte offset
  `0x498`. If that call returns false, it bypasses the main sprite transform
  branch and calls `CCNode::updateTransform()`.
- The main branch reads sprite state at offsets including `0x11e`, `0x17b`,
  `0x22f`, and `0x1b8`, and obtains parent-related transform state through
  the pointer at `this + 0xd8`. It may call another virtual function at
  vtable byte offset `0x310`, then composes affine transforms.
- It writes multiple derived transform/quad-related fields in the sprite
  object, including regions around offsets `0x17c`, `0x1cc`, `0x1e4`,
  `0x1fc`, and `0x214`.
- The call to `CCTextureAtlas::updateQuad()` is conditional, not unconditional.
  In the observed base-class path, the code checks the byte at `this + 0x22f`;
  when it is zero and the atlas pointer at `this + 0x160` is non-null, it
  passes the quad at `this + 0x1cc` and atlas index at `this + 0x168` to
  `updateQuad()`.
- The routine clears a byte at `this + 0x179` before returning through
  `CCNode::updateTransform()` in the main path.

These are object offsets and branch observations, not yet named C++ fields.
Their semantics must be mapped using constructors, setters, related methods,
and runtime observations before any code reads or writes them.

### Safe optimization decision

The disassembly does not justify skipping `updateTransform()` merely because a
sprite appears visually stationary. The routine has virtual dispatch, parent
transform dependencies, multiple state flags, affine composition, and a
conditional atlas write. The safe next step is to map every invalidation source
that can change the derived quad: position/rotation/scale/anchor/content-size,
parent transform, texture/rect/flip/vertex-color/opacity changes, visibility,
animation, and subclass overrides. Only after that map is validated can an
unchanged-sprite cache be designed. The first implementation should be
observation-only (count calls, dirty transitions, and atlas writes) with the
original engine path left intact; a later fast path can be gated behind a
runtime opt-in and fall back per unsupported sprite.

No game hook, object-field write, skipped virtual call, or renderer replacement
has been added by this analysis. The prototype remains separate from the game's
live renderer.
### Exact `CCTextureAtlas::updateQuad()` behavior

Disassembly of the target function at `0x00b332ac` confirms the atlas update
contract used by `CCSprite::updateTransform()`:

- The atlas stores its quad-data pointer at object offset `+0x60`.
- The destination byte offset is `index * 96` (the code computes
  `(index << 7) - (index << 5)`). The function copies exactly 96 bytes from
  the supplied quad into that slot.
- It updates the 32-bit value at atlas offset `+0x4c` to the maximum of its
  prior value and `index + 1`.
- It sets the byte at atlas offset `+0x48` to `1`, marking atlas data dirty.

This establishes that a transform update can overwrite a full 96-byte quad
slot and mark the atlas dirty for a later GPU-buffer upload. It does not yet
prove the complete meaning of the two atlas counters/flags; names such as
`dirtyMaxIndex` and `dirty` remain working labels, not verified source fields.

### Consequence for a future visibility path

An optimization must not simply compact visible sprite quads into a new draw
list and assume the existing atlas VBO/EBO will follow. The existing engine
updates a fixed atlas slot by sprite atlas index, marks atlas data dirty, and
later uploads that dirty range before drawing. A compatible adapter must
preserve the mapping between sprite, atlas index, 96-byte quad slot, texture,
blend state, and painter order. If it introduces a separate compacted index
stream, it must also prove that the index stream references the correct four
vertices per original quad and that all required GL state is restored.

### Current blocker list (revalidated)

1. Map all sprite and parent setters that invalidate transform/quad state.
2. Identify subclasses overriding the virtual update-transform slot and
   determine which can be supported without bypassing custom behavior.
3. Trace atlas dirty-range upload and buffer/index setup end-to-end.
4. Design an observation-only hook that records calls and dirty ranges while
   leaving original functions and draw order untouched.
5. Only then prototype a gated fast path and compare rendered output, alpha,
   clipping, blend modes, and frame-time on the target phone.

Until those steps are complete, no transform call is skipped and no atlas
draw command is replaced.

### Dirty-range upload and indexed draw: complete target path

Further disassembly of `CCTextureAtlas::drawNumberOfQuads(start, count)` (target address `0x00b33af0`) resolves the dirty flag behavior:

- `count == 0` returns immediately without issuing a draw.
- The atlas binds its vertex buffer (`this + 0x40`) to `GL_ARRAY_BUFFER` and tests the dirty byte at `this + 0x48`.
- When dirty, it uploads `count * 96` bytes from the CPU quad array at `this + 0x60 + start * 96` using `glBufferSubData`, then clears the dirty byte at `this + 0x48`.
- It configures three vertex attributes with 24-byte stride: position (3 floats, offset 0), color (4 normalized unsigned bytes, offset 12), and texture coordinates (2 floats, offset 16).
- It binds the element buffer (`this + 0x44`) and calls `glDrawElements(GL_TRIANGLES, count * 6, GL_UNSIGNED_SHORT, start * 12)`.
- It then unbinds both array and element buffers. This function contains no explicit VAO bind in the inspected code path.

The byte arithmetic is consistent with four 24-byte vertices per quad: 96 bytes per quad and six 16-bit indices per quad. The draw path uploads the requested contiguous quad range only when the atlas dirty flag is set.

### Important implication

The current dirty mechanism is atlas-wide (one observed dirty byte), and the draw call's upload range is the exact `(start, count)` requested by its caller. Before changing it, we must trace every caller and determine whether dirty updates outside a requested range can remain pending or whether the engine always draws the relevant full range. A new GPU-generated visibility list cannot safely change `start/count` until this interaction is understood. Preserving the original contiguous index order is also essential for alpha blending and painter order.

### Next investigation

Trace callers of `drawNumberOfQuads()` / `drawQuads()`, the atlas creation and index-buffer initialization path, and all writes to the atlas dirty byte and dirty-range counter. This is now the highest-value static-analysis task before writing a hook or adapter.


### Caller trace: atlas draw ranges in the uploaded target

A full `.text` disassembly of the same uploaded library was searched for direct branches/calls to the atlas draw symbols. The observed call sites are:

- `CCSpriteBatchNode::draw()` (`0x00b17034`) branches to `CCTextureAtlas::drawQuads()` (`0x00b33c40`). The wrapper loads the 32-bit quad count from atlas offset `+0x4c`, sets `start = 0`, and tail-branches to `drawNumberOfQuads(start, count)`.
- `CCParticleBatchNode::draw()` (`0x00b01eb8`) also branches to `drawQuads()`, so it shares the full-prefix atlas draw behavior, but it is a separate renderer path with its own particle update/population rules.
- `CCAtlasNode::draw()` (`0xaca0c4`) directly calls `drawNumberOfQuads(count, 0)` with its node-owned count loaded from object offset `+0x18c`; it uses the atlas object at `+0x178`. This is a distinct atlas consumer and must not be treated as a CCSpriteBatchNode.
- The one other direct `drawNumberOfQuads()` symbol entry is the one-argument wrapper, which sets `start = 0` and forwards the supplied count.

For the normal `CCSpriteBatchNode` path, the draw range is therefore the contiguous prefix `[0, atlas.quadCount)`. If the dirty flag is set, `drawNumberOfQuads(0, quadCount)` uploads that entire prefix (`quadCount * 96` bytes) before drawing. This removes the earlier uncertainty about dirty data outside a partial requested range for this specific path. It does **not** establish that all other atlas consumers have the same lifecycle or that a visibility-compacted range can be substituted safely.

### Updated adapter implication

The CCSpriteBatchNode path draws the atlas's entire contiguous quad prefix in atlas order. A GPU visibility adapter would need to preserve that order while selecting quads; it cannot simply shorten the draw count because invisible quads may be interspersed with visible ones. A separate generated element-index stream remains a plausible experiment, but only after proving the quad-to-vertex/index pattern and how atlas reordering/insert/remove operations update slots. ParticleBatchNode and CCAtlasNode need separate compatibility decisions and should initially remain on the original path.


### Exact atlas EBO construction and quad index pattern

The uploaded target's `CCTextureAtlas::initWithTexture(texture, capacity)`
(`0x00b33004`) allocates and zero-initializes the CPU-side quad array at
`this + 0x60` and the index array at `this + 0x38`. The allocation sizes
are respectively `capacity * 96` bytes and `capacity * 12` bytes.
Initialization then calls `setupIndices()` followed by `setupVBO()`.

The target's `CCTextureAtlas::setupIndices()` (`0x00b32ee0`) writes six
16-bit indices for each quad. The loop's values for quad `i` are:

```text
[4*i + 0, 4*i + 1, 4*i + 2, 4*i + 3, 4*i + 2, 4*i + 1]
```

This exact pattern is established by the six `strh` stores in the loop; it is
not an assumption based on a public Cocos2d implementation. Each quad therefore
uses four consecutive vertices and two triangles with the index order
(0,1,2) and (3,2,1), relative to that quad's first vertex. The EBO is allocated
with `capacity * 12` bytes and uploaded using `GL_ELEMENT_ARRAY_BUFFER`
with `GL_STATIC_DRAW` (`0x88e4`). The VBO is allocated with
`capacity * 96` bytes and `GL_DYNAMIC_DRAW` (`0x88e8`).

The index-generation loop advances the base vertex by four and narrows the
values to 16 bits. Consequently, this representation can address at most
16,384 quads (65,536 vertices, indices 0 through 65,535) without 16-bit
index wraparound. This is a format limit of this exact EBO scheme, not a
validated maximum capacity for every game atlas; callers and capacity growth
must be inspected before assuming all runtime atlases stay within it.

### What this enables, and what it does not

The static per-quad index pattern means a visibility experiment does not need
to regenerate the game's original EBO when the atlas slots remain unchanged.
A separate compacted EBO could instead emit the six original indices for each
visible quad in original atlas order. That would preserve painter order if
(and only if) the candidate-to-atlas-slot mapping is correct and the compacted
index buffer is drawn with the same VBO, texture, vertex attributes, blend
state, shader, and clipping/stencil state.

This still does not remove the CPU child loop or its `updateTransform()`
virtual calls. It also does not establish that quad slots remain stable during
child sorting, insertion/removal, atlas capacity growth, or animation. The
first practical prototype should remain observation-only and should measure
these lifecycle events before attempting a compacted EBO or changing a draw.


### Atlas slot stability check: insert, move, remove, and resize

The target's atlas mutation routines were inspected after the EBO pattern was
resolved. They show that atlas slot numbers are **not stable** across structural
updates:

- `insertQuad()` and `insertQuads()` shift existing 96-byte quad records with
  `memmove` when inserting before the current end, then copy the new quad data
  into the insertion slot and set the atlas dirty byte.
- `insertQuadFromIndex()` moves a quad record from one slot to another by
  copying the source record, shifting the intervening records, and writing the
  saved record at the destination. It also sets the dirty byte.
- `removeQuadAtIndex()` and `removeQuadsAtIndex()` compact the remaining
  quad records after removal; `removeAllQuads()` resets the atlas quad count.
- `moveQuadsFromIndex()` uses a temporary copy plus `memmove` to reorder a
  range of 96-byte records and marks the atlas dirty.
- `resizeCapacity()` reallocates the CPU quad and index arrays. On successful
  resize it calls `setupIndices()` again and remaps/reuploads the GL buffers,
  then marks the atlas dirty.

The static EBO pattern remains a regular sequence based on each slot's current
position, so the original EBO itself does not encode sprite identity. A compacted
visibility EBO can only be correct if its source indices are regenerated or
updated after every slot insertion, removal, move, and capacity change—or if
the adapter reads the current slot mapping each frame. A cache keyed only by a
sprite's former atlas index would become stale after these operations.

### Revised safe integration direction

The static analysis now confirms both halves of the atlas contract:
(1) each slot uses four consecutive vertices and a fixed six-index pattern;
(2) the quad records occupying those slots can move during ordinary atlas
mutation operations. This makes a separate visible-index stream technically
plausible, but it also makes slot-map invalidation mandatory. The next work is
to trace the corresponding `CCSpriteBatchNode` child/atlas-index updates and
sort paths, then define an observation-only event log for slot changes and
per-child transform calls. No draw replacement or transform bypass is justified
yet.


## Fail-closed dispatch behavior

The standalone GLES culler now clears its four-word indirect command to
`{vertexCount=6, instanceCount=0, firstVertex=0, baseInstance=0}` when
a dispatch is rejected because the view rectangle is invalid, the uploaded
candidate count does not match, or a compute dispatch reports a GLES error.
It also invalidates the remembered upload count. This prevents a consumer from
accidentally reusing a previous successful frame's draw count after a failed
dispatch. Clearing is best-effort and deliberately does not call `glGetError()` to
verify the reset: GLES exposes a context-wide sticky error flag, so reading it
inside this helper could consume an unrelated error raised by the game. The
caller must not consume the command after a rejected dispatch, regardless of
whether the reset itself can be verified.

This is a defensive property of the standalone prototype only. It does not
replace renderer-side validation, and it has not yet been exercised on a
physical Android GLES driver.


## Compute backend GL-state preservation detail

The GLES backend's scoped state guard preserves the current program, generic
shader-storage-buffer binding, and indexed SSBO bindings 0 through 3. For each
indexed binding it captures the buffer object plus the bound range start and
size, then restores the range with `glBindBufferRange` (or an unbound/base
binding with `glBindBufferBase`). This matters when the host renderer uses
SSBO subranges: restoring only the buffer object with `glBindBufferBase`
would silently widen the binding and could corrupt unrelated renderer state.

This guard still does not claim to preserve every GLES state category. The
current compute backend only changes the program and SSBO binding state; any
future additions that touch textures, image units, VAOs, indirect draw
bindings, or other state must extend the guard and receive dedicated tests.
