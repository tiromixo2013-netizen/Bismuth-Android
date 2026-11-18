# Atlas lifecycle and cache invalidation contract

## Purpose and status

This document defines the revision tracker and passive event bridge for a
future runtime observation adapter. The tracker and C ABI event bridge are
implemented and testable, but neither is connected to Geometry Dash yet:
there is no installed hook, no game-memory reader, and no live telemetry.
Their purpose is to make cache invalidation rules explicit before runtime
instrumentation is written.

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

## Per-frame passive telemetry

`include/bismuth/atlas_observation.hpp` adds allocation-free counters for a
future observer: batch draw calls, child transform calls, unknown transform
overrides, quad writes, structural mutations, dirty uploads, uploaded quad
counts, and maximum children in one batch. `beginFrame()` resets the current
snapshot. The counters are render-thread-owned and intentionally do not use
atomics or allocate memory; a future hook must confirm all callbacks execute
on the same render thread before using this class.

## Passive event facade

`include/bismuth/atlas_observation.hpp` also provides `AtlasPassiveObserver`,
a render-thread-owned facade that combines the revision tracker with telemetry.
Mutation callbacks accept an explicit success flag: failed original operations
do not advance revisions. Unknown transform overrides and successful dirty
uploads conservatively advance the content revision. An unclassified or
ambiguous mutation calls `invalidateUnknownMutation()`, which advances the
structural revision and invalidates cached slot maps.

## Native C ABI event bridge

The Android shared library now exports a small C ABI bridge for an external
hook adapter:

- `bismuth_observer_begin_frame` resets per-frame counters.
- `bismuth_observer_record_batch_draw` records a batch and its child count.
- `bismuth_observer_record_sprite_transform` counts known and unknown
  transform implementations; unknown implementations invalidate content.
- `bismuth_observer_record_quad_write` and
  `bismuth_observer_record_dirty_upload` update content revision only when
  the original operation succeeded.
- `bismuth_observer_record_structural_mutation` advances both revisions only
  after success.
- `bismuth_observer_invalidate_unknown_mutation` conservatively invalidates
  slot mappings when an event cannot be classified.
- `bismuth_observer_snapshot` copies telemetry and revision numbers to a C
  structure.

The C API uses render-thread ownership just like the underlying observer:
calls must be serialized on the game's render thread. The snapshot function
does not add locks or make concurrent reads safe. These exports are event
sinks only. They do not locate functions, install trampolines, intercept calls,
or read Cocos2d objects. CI checks the C declarations and exported symbols.

## Limitations

The revision tracker and event bridge are host/source-level components only.
They do not prove that all mutation sites have been found, that callbacks
execute after successful mutations, or that a runtime cache will improve
performance. The live renderer remains untouched. Runtime hook installation,
object-layout validation, and device testing are still required.


## Coverage audit: important observer blind spots (2026-11-18)

A source audit of `src/cocos_hooks.cpp` found that the current passive
`CCSprite::updateTransform()` detour attaches only to the exported base-class
symbol. The batch draw loop dispatches through each child's vtable slot
`+0x308`. A subclass that overrides that slot without calling the base
implementation will bypass this detour entirely. Consequently:

- The current transform counter is a count of calls that enter the base
  `CCSprite::updateTransform()` symbol, **not** a complete count of all
  per-child virtual transform calls.
- The existing `unknownTransformOverrides` counter cannot be treated as a
  complete census of overrides from this hook alone. The current callback
  records the base implementation as known; it has no independent callback
  for an override that replaces the slot.
- A future override census needs safe vtable-slot observation or a proven
  runtime class/method inventory. It must not patch arbitrary vtable targets
  without validating executable ownership, signatures, and coexistence.

The same audit found that this observer does not hook the transform-affecting
`CCNode` / `CCSprite` setters (position, rotation, scale, anchor point,
content size, vertex/color/opacity, texture/rect, flip, visibility, actions,
and parent-child transform propagation). Therefore the current atlas revision
tracker is an atlas-content/slot tracker, **not** a complete sprite-transform
invalidation tracker.

Capacity changes also need a distinct audit: the former `resizeCapacity`
hook is intentionally absent after a prior recursive-hook crash. The
`increaseTotalQuadsWith` observer is not proof that every reallocation path
is covered. Until the capacity-growth call graph is traced and observed,
capacity changes must invalidate all candidate-to-slot and quad-derived caches.

### Required gate before any transform cache

1. Obtain the exact target ELF again and map constructor/setter writes to the
   state consumed by `CCSprite::updateTransform()`.
2. Trace parent transform invalidation and action/animation updates, not just
   direct sprite setters.
3. Establish how subclass overrides are represented and counted in actual
   batches; unknown implementations always use the original engine path.
4. Trace all atlas capacity/reallocation paths, including the unhooked
   `resizeCapacity` route.
5. Keep the feature observation-only until the above coverage is demonstrated
   in a runtime trace. A missing event, ambiguous callback, or unknown class
   invalidates the entire relevant cache.

No transform bypass or renderer change is authorized by this coverage audit.
