# Visual-safe optimization plan

## Objective

Improve CPU and GPU frame cost without changing any visible or gameplay behavior.
The first target is redundant CPU work around Cocos2d sprite transforms. The
second target is render submission overhead. No optimization may hide, reorder,
or approximate game content.

## Current verified state

- The current Cocos hook forwards `CCSprite::updateTransform()` to the original
  implementation before recording a passive call counter.
- The hook adapter is experimental and is not installed by the current Geode
  package. Its installation point and coexistence with other mods remain
  unvalidated on a device.
- The existing GPU indexed-draw bridge is experimental, disabled by default,
  and is not an optimization candidate for normal gameplay yet.
- The existing atlas revision tracker can invalidate cached quad data after
  observed writes and structural mutations, but it does not establish that a
  sprite's transform inputs are unchanged.

## Why transform caching is not enabled yet

Skipping `CCSprite::updateTransform()` based only on an unchanged position,
rotation, or scale would be unsafe. Correctness also depends on inherited
parent transforms, anchor and content size, vertex/z state, visibility and
dirty flags, animation/actions, atlas state, and custom subclass overrides.
The current hook has a `void*\` receiver and does not prove the runtime class
or all of those invalidation conditions.

Therefore this branch must not bypass the original transform implementation
until all of the following are established for the exact target binary:

1. The exact class layout and method implementation are verified against the
   Geometry Dash 2.2081 ARM64 build ID already enforced by the hook adapter.
2. Every transform-affecting mutation and parent invalidation path is covered.
3. Unknown subclasses and unrecognized states always take the original path.
4. A cache miss, allocation failure, revision mismatch, or uncertain state
   always fails open to the original implementation.
5. Host tests cover invalidation; on-device visual regression tests cover
   representative levels, transitions, particles, triggers, and mod
   interoperability.

## Staged implementation

### Stage A — evidence and passive baseline

- Keep the original engine call unchanged.
- Establish a safe, measured baseline for transform-call frequency and CPU
  cost before proposing a cache.
- Keep profiling opt-in and outside the default Geode runtime until a safe
  pre-render installation point is proven.
- Separate CPU wall time from GPU execution time in all reports.

### Stage B — conservative CPU optimization

- Implement caching only for a proven, explicitly identified safe class/state.
- Preserve original behavior for dynamic sprites, unknown subclasses, and
  any object with uncertain parent or transform revisions.
- Do not change atlas ordering, quad contents, blend state, clipping, or draw
  submission.
- Require tests for every invalidation route before enabling the path.

### Stage C — GPU/render submission investigation

- Profile actual GPU time and draw submission cost independently.
- Prefer eliminating redundant state work only when equivalence is proven.
- Preserve painter order, alpha/blend semantics, texture sampling, clipping,
  and every submitted visible quad.
- Keep the original draw call as the fallback; do not enable the current
  experimental EBO/indirect bridge in production without device validation.

## Acceptance criteria

A change is eligible for release only if:

- Host tests pass and Android ARM64 build succeeds.
- The exact target binary is accepted by the existing Build ID gate.
- Side-by-side captures show no missing, reordered, recolored, or altered
  objects/effects in representative gameplay.
- Gameplay behavior and trigger timing remain unchanged.
- CPU/GPU measurements show a repeatable improvement beyond measurement
  noise, with no frame-time regression in worst-case scenes.
- The feature remains disabled by default until on-device validation is
  complete.

No FPS gain, successful Android build, or in-game validation is claimed by
this planning commit.
