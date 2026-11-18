# Transform invalidation audit matrix

Date: 2026-11-18

## Purpose

This document turns the transform-cache safety gate into a concrete audit
checklist. It is a research/test plan, not evidence that these setter hooks
exist in the target binary or are currently installed.

The inspected target ELF is identified by GNU Build ID
`ca621323d54c1d94f5f13a2778ce9c8baa31b9a8`. Its exact exported setter
symbols, addresses, signatures, and write effects must be re-checked against
that ELF before any detour is considered. The uploaded APK/package version
still needs independent confirmation; do not infer it from the library name.

## Mutation families that can invalidate a cached sprite transform

| Mutation family | Examples to locate in target ELF | Required invalidation scope |
|---|---|---|
| Local translation | position, position X/Y, vertex Z | Sprite local transform; descendants if parent transform propagates |
| Local orientation | rotation, rotation X/Y, skew X/Y | Sprite local transform; descendants |
| Local scale | scale, scale X/Y | Sprite local transform; descendants |
| Anchor and dimensions | anchor point, content size, ignore-anchor flag | Sprite transform and quad geometry; descendants where node-space changes |
| Parent/child graph | add/remove child, reparent, reorder, cleanup | Old and new ancestry; slot/order maps when atlas structure changes |
| Visibility and draw state | visible, opacity, color, opacity-modifies-RGB, blend state | Never reuse a cached visual classification without separately tracking state; transform cache may remain valid only if proven independent |
| Sprite frame/texture | texture, texture rect, rotated frame, display frame, flip X/Y | Quad UV/geometry and atlas content; transform-only cache is insufficient |
| Actions and animation | action step/update, animation frame advance, scheduled callbacks | Every property changed by the action, including indirect parent changes |
| Parent propagation | parent transform dirty/update, camera/node transform changes | Entire affected descendant subtree |
| Atlas structure | insert/remove/move/reorder/resize/remove-all | All atlas-slot-derived mappings and quad-derived data |

The names above are search targets and semantic categories, not verified
symbol names or an assertion that each API exists unchanged in this fork.

## Target-specific facts already established

- `CCSpriteBatchNode::draw()` invokes the child vtable slot at byte offset
  `0x308` before atlas submission.
- In the exported base `CCSprite` vtable, that slot resolves to
  `CCSprite::updateTransform()` at ELF value `0x00b161d0`.
- `CCSprite::updateTransform()` checks the dirty state and can rewrite the
  96-byte quad at the sprite's atlas slot.
- The clean path calls `CCNode::updateTransform()`, which recursively updates
  children. A proposed fast path must not bypass this recursion for nodes
  with children.
- Existing Bismuth hooks observe only selected atlas methods and the exported
  base `CCSprite::updateTransform()` symbol. They do not currently observe
  the complete setter/action/parent-propagation families above.
- The native hook adapter is not installed by the Geode package. No runtime
  setter coverage has been measured.

## Required static-analysis pass

For each candidate method in the exact target ELF:

1. Confirm the dynamic symbol or resolve it through a verified call site;
   record ELF virtual address, size, calling convention, and argument types.
2. Disassemble the method and identify every state byte/word it writes.
3. Trace callers that update the same state indirectly (actions, animation,
   parent changes, frame changes, and callbacks).
4. Connect those writes to the dirty checks and output fields consumed by
   `CCSprite::updateTransform()`.
5. Distinguish transform-only writes from quad appearance/UV writes and
   atlas-slot structural writes.
6. Reject a cache if a write route cannot be observed or if the method may be
   overridden without coverage.

Do not patch methods based only on familiar Cocos2d API names. This project
uses a game-specific binary and exact signatures/behavior are mandatory.

## Runtime coverage tests required before caching

- Move, rotate, scale, skew, and resize a sprite between consecutive frames.
- Change its anchor point and vertex Z.
- Change the parent transform while the child remains locally unchanged.
- Reparent a child and add/remove/reorder siblings.
- Run actions that animate position, scale, rotation, opacity, and color.
- Change sprite frame, texture rect, flip flags, and texture.
- Toggle visibility and opacity/color while transforms remain unchanged.
- Exercise batch capacity growth, insertion, deletion, movement, and full clear.
- Include custom sprite subclasses overriding the transform vtable slot.
- Repeat under camera movement/zoom, clipping, and representative GD effects.

For every test, compare the cached path against the original engine path at the
quad-byte level and in side-by-side frame captures. A missing observer event,
unknown override, unexpected quad write, or atlas structural change must force
the original path for the affected object/batch.

## Cache admission policy

A sprite is not eligible merely because its quad did not change last frame.
Eligibility requires all of the following:

- Its concrete transform implementation is known and covered.
- Its parent/ancestor transform revision is known and unchanged.
- All local transform inputs are known and unchanged.
- It has no child subtree whose update would be skipped.
- Its atlas slot and quad storage are valid and unchanged.
- No action, animation, callback, or external mod can mutate it through an
  unobserved route.

Until each condition is demonstrated, call the original Cocos2d method.
The initial implementation target remains observation-only; no transform
bypass is enabled by this audit.


## Binary inspection results (uploaded target ELF)

Inspected the uploaded `libcocos2dcpp.so` directly with ELF metadata,
dynamic-symbol enumeration, and AArch64 disassembly. The file is a stripped
ELF64 AArch64 shared object (19,077,352 bytes) with GNU Build ID
`ca621323d54c1d94f5f13a2778ce9c8baa31b9a8`, matching the target ID recorded
above. The presence of symbols does not establish the exact APK version.

### Confirmed rendering and transform flow

- `CCSpriteBatchNode::draw()`: `0x00b17034`, size 232 bytes. It walks the
  child array held through the node's child-container pointer at `this+0xd0`
  (container count at `+0x38`, element pointer at `+0x10`), calls each
  non-null child's vtable slot `+0x308`, then submits the texture atlas via
  `CCTextureAtlas::drawQuads()` at `0x00b33c40`.
- `CCNode::updateTransform()`: `0x00acabf8`, size 100 bytes. It also walks
  the child container at `this+0xd0` and dispatches the same vtable slot
  `+0x308` for each non-null child. This confirms recursive child processing
  on this path; a shortcut must preserve it or prove the subtree is safe.
- `CCSprite::updateTransform()`: `0x00b161d0`, size 1,072 bytes. It first
  calls a virtual predicate at vtable byte offset `+0x498`; the main path
  checks dirty state at `this+0x11e`, consults parent pointer `this+0xd8`
  and parent dirty byte `parent+0x17b`, obtains parent transform through
  vtable slot `+0x310`, composes affine data, and writes sprite transform/
  quad-related fields. It calls `CCTextureAtlas::updateQuad()` at
  `0x00b332ac` only when the atlas pointer at `this+0x160` is non-null and
  the checked byte at `this+0x22f` is clear. The function also clears/updates
  dirty-state bytes including `this+0x179` and `this+0x17b`.
- The base `CCSprite` vtable slot `+0x308` resolves to this exported
  `CCSprite::updateTransform()`; a subclass that replaces the slot without
  calling the base still bypasses a hook placed only on this symbol.
- `CCTextureAtlas::updateQuad()`: `0x00b332ac`, size 100 bytes. It copies
  the 96-byte quad record into atlas storage at a 96-byte stride and marks the
  atlas dirty.
- `CCTextureAtlas::resizeCapacity()`: `0x00b336d8`, size 492 bytes. It
  reallocates quad/index storage, may rebuild indices and remap buffers, and
  marks the atlas dirty on the successful resize path. This is a confirmed
  structural invalidation point and must invalidate any slot/storage cache.
- `CCSpriteBatchNode::visit()`: `0x00b1711c`, size 172 bytes. It performs
  visibility/transform handling around the batch draw path; it is not a
  replacement for per-child transform processing.

### Confirmed exported mutation entry points

The following symbols are present in this exact ELF. These are audit targets,
not a claim that Bismuth currently hooks them:

| API | ELF value |
|---|---:|
| `CCNode::setPosition(CCPoint const&)` | `0x00aca8c8` |
| `CCNode::setScale(float)` | `0x00aca858` |
| `CCNode::setRotation(float)` | `0x00aca7e8` |
| `CCNode::setVisible(bool)` | `0x00aca91c` |
| `CCNode::setAnchorPoint(CCPoint const&)` | `0x00acb158` |
| `CCNode::setContentSize(CCSize const&)` | `0x00acb20c` |
| `CCSprite::setPosition(CCPoint const&)` | `0x00b16880` |
| `CCSprite::setRotation(float)` | `0x00b1690c` |
| `CCSprite::setScale(float)` | `0x00b16c40` |
| `CCSprite::setAnchorPoint(CCPoint const&)` | `0x00b16d38` |
| `CCSprite::setVisible(bool)` | `0x00b16db0` |
| `CCSprite::setTextureRect(CCRect const&, bool, CCSize const&)` | `0x00b14db4` |
| `CCSprite::setTexture(CCTexture2D*)` | `0x00b154a4` |
| `CCSprite::setFlipX(bool)` / `setFlipY(bool)` | `0x00b15e78` / `0x00b15ebc` |
| `CCTextureAtlas::insertQuad` | `0x00b33310` |
| `CCTextureAtlas::removeQuadAtIndex` | `0x00b335f0` |
| `CCTextureAtlas::removeQuadsAtIndex` | `0x00b33664` |
| `CCTextureAtlas::removeAllQuads` | `0x00b336d0` |
| `CCTextureAtlas::resizeCapacity` | `0x00b336d8` |
| `CCTextureAtlas::moveQuadsFromIndex` | `0x00b338e4` / `0x00b339f4` |

Disassembly confirms, for example, that `CCNode::setPosition` marks transform
state bytes at `+0x11c` and `+0x112` before storing the two position floats.
The `CCSprite::setPosition` override compares the old and new coordinates,
delegates to the base setter when changed, and may notify its batch node through
virtual calls. `CCSprite::setVisible` delegates to the base setter and can
also notify the batch node. These paths reinforce that a single passive
`updateTransform` counter cannot provide complete invalidation coverage.

### Consequence for implementation

We now have exact, matching-binary entry points to audit instead of generic
API guesses. The next static pass should trace all relevant setter and action
writes into the dirty flags and determine the virtual implementations behind
slots `+0x308`, `+0x310`, and `+0x498`, including custom subclasses. Only
after that can we design an opt-in cache with original-path fallback.

No rendering bypass or transform cache has been implemented or enabled by
this binary inspection. No visual-equivalence, runtime, or FPS test has been
performed yet.


### Exported sprite-vtable slot census

A relocation-table pass checked the vtable entry corresponding to virtual byte
offset `+0x308` (vtable object address plus the two-word Itanium ABI header
and `0x308`). In the exported sprite-named vtables present in this ELF, the
entry resolved either to `CCSprite::updateTransform()` (`0x00b161d0`) or
`CCNode::updateTransform()` (`0x00acabf8`). Examples resolving to the
CCSprite implementation include `CCSprite`, `CCSpritePlus`,
`CCSpriteCOpacity`, `CCSpriteWithHue`, `CCSpriteGrayscale`,
`CCSpriteExtra`, `CCAnimatedSprite`, `CCPartAnimSprite`,
`GJRobotSprite`, `GJSpiderSprite`, `GJChestSprite`, and
`GravityEffectSprite`. Examples resolving to the CCNode implementation
include `GJSmartBlockPreviewSprite`, `GauntletSprite`,
`CCScale9Sprite`, `CCMenuItemSprite`, and `GJBigSprite`.

This is a useful binary-level census of exported vtables, not proof that every
runtime object or dynamically generated class is represented. It also does
not cover vtables whose RTTI/symbols are absent, objects whose vptr changes at
runtime, or third-party mods adding subclasses. Therefore it narrows the
known override question but does not remove the need for a conservative
unknown-class fallback and runtime validation.
