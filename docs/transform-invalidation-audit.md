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
