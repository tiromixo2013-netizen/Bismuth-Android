# Atlas lifecycle and cache invalidation contract

## Purpose and status

This document defines a small host-side revision tracker for a future
observation-only adapter. It is not connected to Geometry Dash, does not read
game memory, and does not install hooks. Its job is to make cache invalidation
rules explicit and testable before a runtime observer is written.

## Why two revisions are needed

The inspected target binary shows that atlas quad records occupy mutable slots.
Insertion, removal, range movement, reordering, and capacity resize can shift
records or rebuild storage. A cached sprite-to-slot mapping must therefore be
invalidated after every structural atlas mutation.

Separately, `CCSprite::updateTransform()` can call
`CCTextureAtlas::updateQuad()` to overwrite the 96-byte record at an existing
slot and mark the atlas dirty. That write does not by itself prove that the
slot identity changed, but it does mean any cached bounds, quad bytes, or
derived visibility data may be stale.

The adapter therefore tracks two monotonic revisions:

- **slots** changes after structural operations: insert, remove, move,
  reorder, resize, or remove-all.
- **content** changes after any quad write. Structural changes advance both
  revisions because they also invalidate content derived from old slot layout.

## API

`include/bismuth/atlas_lifecycle.hpp` provides
`AtlasRevisionTracker`:

- `recordStructuralMutation()` advances slot and content revisions.
- `recordQuadWrite()` advances only the content revision.
- `snapshot()` captures both values.
- `slotMappingCurrent(snapshot)` checks whether a cached slot map is valid.
- `quadDataCurrent(snapshot)` checks whether data derived from the atlas
  contents is still valid.

A future observer must call these methods only after confirming the
corresponding target operation succeeded. It must not infer success from a
function entry alone.

## Required observation points

The binary analysis has identified the relevant operation families, but the
live hook addresses, call coverage, and success/failure semantics still need
runtime validation:

| Operation family | Required invalidation |
|---|---|
| insert quad(s) | structural |
| insert from index | structural |
| remove quad(s) / remove all | structural |
| move quad range | structural |
| resize capacity | structural |
| update one quad in-place | content only |
| sprite transform/quad rewrite | content only, unless slot identity also changes |

The observer should initially be passive: count calls and record before/after
revision values while allowing the original function to execute unchanged.
It must not skip transforms, write object fields, replace draw calls, or alter
atlas buffers in this phase.

## Correctness rules for any future cache

1. A slot map is usable only when its saved `slots` revision matches the
   current tracker.
2. Cached quad-derived data is usable only when both revisions match.
3. Any missed or ambiguous mutation event invalidates the entire cache.
4. Unknown subclasses and unobserved atlas consumers stay on the original path.
5. Revision tracking must be validated against actual insert/remove/move/resize
   behavior on the target device before enabling a fast path.

## Limitations

The tracker is a host-tested bookkeeping primitive only. It does not prove
that all mutation sites have been found, that callbacks execute after
successful mutations, or that a runtime cache will improve performance.
The live renderer remains untouched.
