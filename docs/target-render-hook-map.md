# Geometry Dash 2.2081 ARM64 render-path map

## Binary identity

Static analysis was performed on the user-supplied `libcocos2dcpp.so`:

- ELF: 64-bit little-endian AArch64 shared object (ET_DYN), stripped.
- Size: 19,077,352 bytes.
- SHA-256: `0926fc6ea6802128f057008b7840708c46765acb8df57d1968c966e41d72089d`.
- GNU Build ID: `ca621323d54c1d94f5f13a2778ce9c8baa31b9a8`.

Addresses below are ELF virtual addresses. At runtime, add the module load bias; do not treat them as absolute process addresses.

## Verified SpriteBatchNode draw flow

### `CCSpriteBatchNode::draw()`

- Symbol: `_ZN7cocos2d17CCSpriteBatchNode4drawEv`
- ELF address: `0x00b17034`, size 232 bytes.
- Reads the texture atlas pointer from `this + 0x148`.
- Reads the child array from `this + 0xd0`; the `_ccArray` is at child-array `+0x38`, with count at `+0x00` and item pointers at `+0x10`.
- Iterates the children and invokes virtual slot `0x308` on each child.
- After the child loop, applies the batch blend function and branches to `CCTextureAtlas::drawQuads()`.

The `CCSprite` vtable relocation at slot `0x308` resolves to `CCSprite::updateTransform()` at `0x00b161d0`. This confirms that the batch draw path performs per-child transform updates before submitting atlas geometry.

### `CCSpriteBatchNode::visit()`

- Symbol: `_ZN7cocos2d17CCSpriteBatchNode5visitEv`
- ELF address: `0x00b1711c`, size 172 bytes.
- Handles visibility, matrix push/pop, parent transform, and grid before/after draw behavior.
- It is not the inner atlas submission point; the per-child update loop is in `draw()`.

## Sprite transform and atlas writes

### `CCSprite::updateTransform()`

- Symbol: `_ZN7cocos2d8CCSprite15updateTransformEv`
- ELF address: `0x00b161d0`, size 1,072 bytes.
- The virtual call at vtable slot `0x498` resolves to `CCSprite::isDirty()` at `0x0056ee50`; that method reads the byte at `sprite + 0x178`.
- The dirty path calculates the sprite quad. If the atlas pointer at `sprite + 0x160` is non-null and the relevant branch permits an atlas write, it passes the quad at `sprite + 0x1cc` and the slot at `sprite + 0x168` to `CCTextureAtlas::updateQuad()`.
- The clean path calls `CCNode::updateTransform()`, which recursively visits child nodes. Any future transform-skip optimization must preserve that behavior for nodes that have children.

The quad argument is the engine-produced 96-byte `_ccV3F_C4B_T2F_Quad` record. Its four vertices have a 24-byte stride. This gives a plausible passive observation point for reading the four transformed vertex positions, but their exact coordinate space relative to camera/projection still needs calibration.

### `CCTextureAtlas::updateQuad()`

- Symbol: `_ZN7cocos2d14CCTextureAtlas10updateQuadEPNS_19_ccV3F_C4B_T2F_QuadEj`
- ELF address: `0x00b332ac`, size 100 bytes.
- Arguments: atlas pointer, quad pointer, atlas slot index.
- Copies 96 bytes into the atlas quad array at `atlas + 0x60 + slot * 96`.
- Updates the used-quad count at `atlas + 0x4c` to at least `slot + 1`.
- Marks the atlas VBO dirty at `atlas + 0x48`.

This hook can observe a successful in-place quad rewrite and refresh cached bounds for that slot. It is not called for every sprite on every frame: unchanged sprites may retain their previous quad data, so a cache must persist across frames and be invalidated on all structural atlas changes.

## Atlas draw and buffer layout

### `CCTextureAtlas::drawQuads()`

- Symbol: `_ZN7cocos2d14CCTextureAtlas9drawQuadsEv`
- ELF address: `0x00b33c40`, size 12 bytes.
- Loads used-quad count from `atlas + 0x4c` and calls `drawNumberOfQuads(count, 0)`.

### `CCTextureAtlas::drawNumberOfQuads(count, start)`

- Symbol: `_ZN7cocos2d14CCTextureAtlas17drawNumberOfQuadsEjj`
- ELF address: `0x00b33af0`, size 336 bytes.
- Confirmed argument order is **count first, start second**.
- Atlas fields used by this routine:
  - VBO name: `+0x40`
  - EBO name: `+0x44`
  - dirty byte: `+0x48`
  - used quad count: `+0x4c`
  - capacity: `+0x50`
  - texture: `+0x58`
  - quad CPU data: `+0x60`
- When dirty, uploads `count * 96` bytes from `quadData + start * 96` to the VBO at byte offset `start * 96`, then clears the dirty byte.
- Vertex attributes use a 24-byte stride: position (3 floats) at offset 0, color (4 normalized unsigned bytes) at offset 12, texture coordinates (2 floats) at offset 16.
- Binds the atlas EBO and calls `glDrawElements(GL_TRIANGLES, count * 6, GL_UNSIGNED_SHORT, start * 12)`, then unbinds the array and element buffers.
- The `glDrawElements` call instruction is at `0x00b33bd0`. It occurs after the VBO upload and vertex-attribute setup, so a future draw substitution must preserve those preceding operations and the cleanup that follows.

The class-specific tail branch in `CCSpriteBatchNode::draw()` is at `0x00b17118`. At that point the child-transform loop has completed and `x0` contains the atlas pointer; the instruction branches to `CCTextureAtlas::drawQuads()`. This is a candidate seam for a SpriteBatchNode-only replacement while retaining `drawQuads()` as fallback. It is a static patch point, not an installed hook.

The existing EBO is generated by `CCTextureAtlas::setupIndices()` at `0x00b32ee0`. Its per-quad pattern is `[4q, 4q+1, 4q+2, 4q+3, 4q+2, 4q+1]`. Atlas initialization allocates 96 bytes per quad record and 12 bytes per quad for 16-bit indices. The 16-bit vertex-index ceiling is 16,384 quads.

## Structural atlas mutation entry points

The binary exports these relevant methods:

| Operation | ELF address |
|---|---:|
| `insertQuad` | `0x00b33310` |
| `insertQuads` | `0x00b333e0` |
| `insertQuadFromIndex` | `0x00b334f4` |
| `removeQuadAtIndex` | `0x00b335f0` |
| `removeQuadsAtIndex` | `0x00b33664` |
| `removeAllQuads` | `0x00b336d0` |
| `resizeCapacity` | `0x00b336d8` |
| `moveQuadsFromIndex` (2 args) | `0x00b339f4` |
| `moveQuadsFromIndex` (3 args) | `0x00b338e4` |

These are candidate passive-observation sites. Hook coverage and post-call success semantics still need runtime validation. A slot-map cache must be invalidated after insert/remove/move/reorder/resize/remove-all.

## Implications for Bismuth integration

1. A passive observer can begin at `CCTextureAtlas::updateQuad()` to record slot and quad data, and at the structural mutation methods to invalidate slot mappings.
2. `CCSpriteBatchNode::draw()` is the verified path that updates child transforms before calling atlas draw. Its child count can be sampled from the array layout above.
3. The current Bismuth compute pipeline accepts CPU-supplied candidate bounds and atlas-slot payloads. The binary now provides a likely source for those bounds, but the camera/view coordinate conversion is not yet proven. Do not enable culling until the candidate bounds and view rectangle are confirmed to share a coordinate space under camera movement, zoom, rotation, and effects.
4. The current batch draw still walks every child and invokes its transform method before atlas submission. Therefore the current GPU culler cannot, by itself, remove that CPU traversal cost. It may reduce submitted geometry only after the real draw integration is implemented and validated.
5. A future replacement draw must preserve the engine's texture, shader, blend, vertex attribute, clipping/stencil, and painter-order state. Stable candidate order is required because alpha blending is order-dependent.
6. Keep the original `glDrawElements` path as the fail-closed fallback. Do not patch or skip the original path during the passive-observation phase.

## Status and limitations

This is static analysis of the identified binary only. No hooks are installed by this document, no live game memory has been inspected, and no device runtime test has been performed. The Bismuth GLES culler and EBO generation remain a prototype until the observer, camera mapping, real draw substitution, visual correctness, and device performance are all validated.
