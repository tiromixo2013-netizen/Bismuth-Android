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

The revision-only methods `invalidateQuadData()` and
`invalidateStructuralMutation()` deliberately bypass telemetry counters.
The live atlas mutation wrappers use them whenever passive telemetry is
disabled, so a future cache cannot become stale merely because the user turned
off the HUD/measurement counters. Telemetry gating controls measurement work;
it must never gate correctness-critical cache invalidation.

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


## Hierarchy and SpriteBatch lifecycle trace (2026-11-18)

A second disassembly pass followed the hierarchy entry points and the
SpriteBatchNode-specific add/remove/reorder paths in the same target ELF.

### CCNode parent/child mutations

- `CCNode::setParent(CCNode*)` at `0x00aca94c` is a minimal pointer store:
  it writes the parent pointer at `this+0xd8` and returns. It does not itself
  mark the node's transform dirty. A cache must not treat a parent-pointer
  write as a complete invalidation event unless the surrounding hierarchy
  operation is also observed.
- `CCNode::addChild(child,z,tag)` at `0x00acca7c` ensures the child array
  exists, calls `insertChild`, writes the child's z-order at `child+0x10`,
  and dispatches the child's parent setter through vtable byte offset
  `+0x1e8`. It then handles order-of-arrival and lifecycle/transition
  callbacks. The short public overloads dispatch through the parent's
  vtable at byte offset `+0x1c8`, so subclass overrides are possible.
- `CCNode::insertChild(child,z)` at `0x00acca00` marks the parent's
  reorder-dirty byte at `+0x120`, inserts/appends the child in the child
  array, and dispatches the child's local Z-order setter through vtable
  offset `+0x60`.
- `CCNode::detachChild(child,cleanup)` at `0x00acc920` invokes lifecycle
  callbacks when the parent is running, dispatches the child's parent setter
  with null, and removes the child from the parent's child array. When child
  indexing is enabled, removal uses the child index; otherwise it removes
  by object. Public remove-child entry points dispatch through virtual
  methods, so an observer limited to the base helper can miss overrides.
- `CCNode::reorderChild(child,z)` at `0x00acab1c` sets the parent's
  reorder-dirty byte, updates the global order-of-arrival counter, and
  updates the child's Z order. It is a draw-order mutation even when the
  transform matrix is unchanged.

### CCSpriteBatchNode-specific behavior

- `CCSpriteBatchNode::addChild(child,z,tag)` at `0x00b18610` first invokes
  child-side batch/texture setup, then performs the base `CCNode::addChild`
  operation, and finally calls `appendChild` to integrate the sprite into
  the batch's atlas.
- `CCSpriteBatchNode::appendChild(sprite)` at `0x00b17e2c` marks batch
  reorder state, associates the sprite with the batch, marks it dirty, handles
  nested sprite children recursively, adds the sprite to the batch's sprite
  array, assigns an atlas index at `sprite+0x168`, copies the 96-byte quad,
  and calls `CCTextureAtlas::insertQuad`. This is both hierarchy and atlas
  structure mutation.
- `CCSpriteBatchNode::removeChild(child,cleanup)` at `0x00b187c4` calls
  `removeSpriteFromAtlas` before tail-branching to the base node removal.
  The atlas removal path may swap the last sprite into the removed sprite's
  index and then removes the corresponding atlas quad. Nested sprite children
  are processed recursively.
- `CCSpriteBatchNode::reorderChild(child,z)` at `0x00b18754` conditionally
  checks whether the child belongs to the batch and, when the effective Z
  order changes, delegates to `CCNode::reorderChild`. Thus order changes
  require preserving the original engine sorting and draw-order path; they
  cannot be treated as a transform-only change.

### Observer coverage consequence

The current native adapter observes selected atlas entry points (insert,
remove, move, remove-all, and increase-total-quads), but it does not hook the
hierarchy methods above or the SpriteBatchNode add/remove/reorder methods.
The atlas-level hooks can see many downstream quad mutations, but they do
not provide a complete parent/child graph revision, and they cannot by
themselves establish which ancestor transform revision applies to a sprite.

The adapter also still does not observe `CCTextureAtlas::resizeCapacity`
(`0x00b336d8`). The actual implementation changes capacity, reallocates
quad and index storage, may rebuild indices and remap buffers, and can take
failure paths that free/reset storage. `increaseTotalQuadsWith` only updates
the used-quad count/high-water field; it is not a substitute for observing
capacity allocation. Therefore any slot/storage cache must be invalidated
conservatively on capacity uncertainty, and the current observer cannot
claim complete atlas-storage coverage.

The existing atlas wrappers generally call the original function and then
record a structural mutation without inspecting a success result. This is
conservative for invalidation (it can produce harmless extra revision
increments), but it is not evidence that every operation succeeded. The
resize path is more complicated because its return value and allocation
failure behavior matter; do not add a hook until the trampoline/rollback and
post-call success semantics are reviewed.

### Current decision

No hierarchy or resize hooks are added in this pass. A transform cache remains
ineligible: parent/child graph changes, subclass virtual dispatch, batch
membership, Z-order changes, and capacity reallocations are not yet covered
by a complete runtime observer. The original Cocos2d path remains authoritative.


## Capacity-resize call graph and failure semantics (2026-11-18)

The exact target ELF (`libcocos2dcpp.so`, Build ID
`ca621323d54c1d94f5f13a2778ce9c8baa31b9a8`) was disassembled around
`CCTextureAtlas::resizeCapacity(unsigned)` at `0x00b336d8`.

### Confirmed callers

A whole-text disassembly search found these direct call/tail-call sites:

- `CCLabelAtlas::setString(char const*)` at `0x00ae1830` calls
  `resizeCapacity` at `0x00ae188c` when the requested string length
  exceeds the atlas capacity returned by the virtual capacity getter.
- `CCParticleBatchNode::increaseAtlasCapacityTo(unsigned)` at
  `0x00b024bc` loads its atlas pointer from `this+0x148` and tail-branches
  directly to `resizeCapacity`.
- `CCSpriteBatchNode::increaseAtlasCapacity(unsigned)` at
  `0x00b176c8` compares the requested capacity with the current atlas
  capacity and tail-branches to `resizeCapacity` only when growth is needed.
- `CCSpriteBatchNode::increaseAtlasCapacity()` at `0x00b17718` computes a
  larger capacity and delegates to the preceding helper.

This means a hook on the central exported `resizeCapacity` symbol would
observe these three client families (label atlas, particle batch, and sprite
batch) without separately hooking each wrapper. It still would not cover
other binaries, private copies, or runtime paths that bypass this exported
implementation.

### Exact resize behavior

The target implementation compares the requested capacity with the current
capacity at atlas offset `+0x50`. If equal, it returns true without changing
storage. Otherwise it writes the new capacity and clamps the used-quad count
at offset `+0x4c`, then reallocates the quad buffer at `+0x60` (96 bytes
per quad) and the index buffer at `+0x38` (12 bytes per quad).

On a successful allocation it rebuilds the index data through
`setupIndices()`, remaps buffers through `mapBuffers()`, marks atlas dirty
at `+0x48`, and returns true. When growing, the new quad/index regions are
zero-initialized. On allocation failure, the implementation frees the
remaining atlas allocations as applicable, clears their pointers, resets
used-quad count and capacity to zero, and returns false. Thus **false does
not mean the atlas remained unchanged**; it can mean the atlas was reset
after allocation failure.

The boolean return is therefore an operation-result signal, not a safe
invalidation predicate. A future observer must conservatively invalidate
slot and quad-derived state after every nontrivial resize attempt, including
a failed one. A same-capacity no-op may be over-invalidated harmlessly unless
the observer has a separately validated before/after capacity check.

### Important correction to event semantics

The current source wrappers for insert/remove/move operations call the
original void-returning function and then pass `true` to
`recordStructuralMutation`. They establish that the original call returned,
not that the requested index/count changed the atlas. The current revision
tracker is therefore a conservative mutation-attempt tracker for those
void APIs, not a proof of successful state change. This over-invalidation is
safe for correctness but may increment revisions on no-op or rejected
requests. Documentation and telemetry must not describe these wrappers as
independently verifying engine success.

### Hook decision

The central resize symbol is a better observation point than separate
client-family hooks, but it remains intentionally unhooked in this pass.
Before adding it, review Dobby trampoline coexistence, partial-install
rollback, recursive entry behavior, and the exact bool ABI on the target
ARM64 build. In particular, do not use the false return to skip invalidation:
the failure path can clear the atlas storage itself. No cache may be enabled
until resize events are observed reliably on-device.


## Native hook-install state machine (2026-11-19)

The installer now uses an explicit process-lifetime state machine shared from
`include/bismuth/hook_install_state.hpp`:

- `Clean`: module Build ID and required symbol ownership can be checked
  without changing executable code.
- `Installing`: entered immediately before the first Dobby detour attempt.
- `Partial`: any early return after entering Installing transitions here
  through an RAII guard. The observer pointer remains unpublished, so already
  installed wrappers remain pass-through. A second install attempt is refused
  until process restart.
- `Installed`: reached only after every required detour succeeds and the
  observer pointer is published. Detach removes the observer but does not
  reset this state because the process-lifetime trampolines remain installed.

The previous one-off rollback of the update-quad hook when batch-draw
installation failed has been removed. It was unsafe to assume that
`DobbyDestroy` could be called while render threads might execute the target.
No reverse-order unpatching is attempted. The guard is tested on the host for
both early-return-to-Partial and successful-commit-to-Installed transitions;
the Android ARM64 build compiles the actual installer path.

This is a fail-closed partial-install state machine, not an atomic transaction:
a failure can leave earlier pass-through detours installed for the remainder
of the process. It prevents retries and keeps the observer unpublished, but
does not prove concurrent patch safety or recover a process if a detour itself
is left in an unknown state. Physical-device fault injection and thread
quiescence remain outstanding.

**Next gate:** before adding the central `resizeCapacity` observer, validate
the installer state machine and Dobby behavior on the target device, then
observe normal growth, same-capacity no-op, and allocation-failure invalidation.
No transform cache is eligible until hierarchy, subclass dispatch, atlas
capacity, and all transform-affecting mutation paths have complete invalidation
coverage.
