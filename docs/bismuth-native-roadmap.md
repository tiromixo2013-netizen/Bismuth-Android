# Bismuth Native for Geometry Dash Android — engineering roadmap

## Goal

Turn the current Android research backend into a validated, opt-in rendering
path for Geometry Dash 2.2081 ARM64. The original Cocos2d renderer remains the
fallback at every stage. No optimization is enabled merely because it compiles.

## Current verified foundation

- C++17 CPU reference culler with stable input ordering and fail-open handling
  for invalid bounds.
- Optional GLES 3.1 compute backend: classify, group offsets, stable scatter,
  packed 16-bit atlas EBO generation, and indexed indirect command output.
- Atlas format inspected from the supplied ARM64 library: four vertices per
  quad, 24-byte vertex stride, 96-byte quad record, and six 16-bit indices.
- Host tests cover stable payload compaction, atlas index packing, command
  layout, revision invalidation, target child-count metadata, and conservative
  clip-space projection.
- Build-ID-gated Dobby observer source and a separate full-visible indexed-draw
  probe exist. They are not active in the Geode package.
- The current Geode mod has a safe HUD and optional passive SpriteBatch draw
  timing. It does not replace the renderer.

## Non-negotiable safety rules

1. Never enable a hook on an unverified game build. The current native adapter
   expects GNU Build ID
   `ca621323d54c1d94f5f13a2778ce9c8baa31b9a8`.
2. Never patch a method while it may be executing. A valid pre-render install
   point and hook coexistence strategy are prerequisites.
3. Keep the original draw call as the fallback for every unsupported or
   uncertain case.
4. Preserve atlas slot order, alpha blending, premultiplied-alpha conventions,
   texture coordinates, vertex colors, clipping/stencil, shader state, VAO,
   EBO and indirect-buffer bindings.
5. Treat an unknown sprite subclass, mutation event, layout, or GL state as
   non-cullable. Fail open rather than risk missing geometry.
6. Do not call GPU clock frequency "GPU utilization", and do not claim FPS
   gains without same-device A/B measurements.

## Milestones

### M0 — HUD and measurement stability

- Keep the compact HUD inexpensive and update text at 1 Hz.
- Keep frame-interval sampling allocation-free.
- Schedule companion socket polling and telemetry snapshots at 1 Hz rather
  than doing timer checks and atomic exchanges on every frame.
- Measure HUD off/on and renderer-telemetry off/on independently.

**Exit gate:** successful Android64 build, no regression to the existing
Live Streaming fix or HUD placement, and a same-level A/B test on the target
phone showing whether the HUD itself adds measurable frame-time cost.

### M1 — Passive renderer workload profile

- Establish a safe hook-installation lifecycle before activating native hooks.
- Collect batch count, children per batch, transform-call count, atlas quad
  writes, structural mutations, dirty uploads, and CPU time spent in the
  original batch draw.
- Confirm callback thread ownership and observer lifetime.
- Validate child counts and atlas revisions against runtime scenes.
- Do not alter draw submission or skip transforms.

**Exit gate:** repeatable per-frame snapshots on the exact target binary, with
no visual differences and no measurable regression versus hooks disabled.

### M2 — Coordinate and bounds calibration

- Capture the actual four post-transform positions for a quad through the
  existing update path; do not add a second child traversal.
- Identify the exact active projection/view transform used by the game's
  atlas shader and validate it against known on-screen landmarks.
- Convert all four vertices to conservative clip-space bounds.
- Fail open for perspective eye-plane crossings, non-finite values, unknown
  coordinate spaces, and stale atlas revisions.
- Build a diagnostic visualization or CPU comparison test before GPU culling.

**Exit gate:** CPU reference bounds agree with observed screen coverage across
camera movement, rotation, scaling, clipping, and representative effects.

### M3 — GPU visibility prototype

- Feed validated candidate bounds and current atlas slot IDs to the existing
  GLES 3.1 compute pipeline.
- Preserve stable painter order; never sort alpha-blended sprites by texture
  or visibility.
- Initially run the GPU output in shadow mode and compare its visible-slot
  list with the CPU reference.
- Then test the indexed indirect draw on one compatible atlas while retaining
  the original draw as fallback.
- Measure CPU submission time, GPU time, frame-time p50/p95/p99, power, and
  thermals. GPU timing must remain asynchronous and non-blocking.

**Exit gate:** exact visual equivalence and a repeatable improvement on the
same device/scene. A faster draw call alone is not sufficient if the CPU still
performs all per-child transforms.

### M4 — Reduce CPU transform work

- Only after M1–M3, investigate caching or skipping per-sprite transform
  updates.
- A cache key must include sprite/parent transform revisions, atlas slot
  revision, quad-content revision, camera/projection revision, and relevant
  animation/effect state.
- Any untracked mutation invalidates the cache and invokes the original
  transform path.
- Do not assume a sprite is static because its position was unchanged in one
  frame.

**Exit gate:** transform-call reduction is demonstrated in traces, while
animation, editor/gameplay transitions, particles, and visual effects remain
correct.

### M5 — Controlled device rollout

- Mali and Adreno capability checks, driver-specific validation, thermal
  soak, low-memory behavior, and mod coexistence.
- Feature flags for passive telemetry, GPU culling, and CPU transform caching
  remain independent.
- Default to the original renderer on unsupported devices or any failed check.

## Immediate next engineering task

Complete M0 by keeping all slow-path telemetry work on the existing 1 Hz
controller cadence. Then implement M1's passive workload snapshot and safe
installation gate. Do not enable the experimental GPU draw bridge as an
ordinary gameplay optimization.

## Reporting format

For each milestone report separately:
- source implementation status;
- host test status;
- Android build status;
- target-device runtime status;
- visual correctness status;
- measured performance delta.

Never combine these into one percentage or call a compiled prototype a
validated renderer.
