# Build-verified Cocos2d passive hooks

## Scope

The Android ARM64 build can compile a passive hook adapter for the exact
Geometry Dash 2.2081 Cocos2d binary identified by GNU Build ID
`ca621323d54c1d94f5f13a2778ce9c8baa31b9a8`.

The adapter uses Dobby at pinned commit
`a418c6a7c493e6c599713a097ff823c84a40ba96` (Apache-2.0) for ARM64 inline
trampolines. It does not patch unknown builds: it checks the loaded module's
GNU Build ID and verifies both resolved symbols belong to
`libcocos2dcpp.so` before installing either hook.

## Installed passive hooks

- `CCSpriteBatchNode::draw()`: records a batch draw event, then calls the
  original function unchanged.
- `CCTextureAtlas::updateQuad(quad, index)`: calls the original function
  first, then records a successful quad-content write.
- `CCTextureAtlas::removeAllQuads()`: calls the original function first, then
  invalidates the atlas slot mapping.
- `CCTextureAtlas::resizeCapacity(capacity)`: preserves the original boolean
  result and invalidates the slot mapping whenever a different capacity was
  requested. The target routine can partially tear down storage on allocation
  failure, so invalidation is conservative even when it returns false.
- `CCTextureAtlas::insertQuad`, `insertQuads`, and `insertQuadFromIndex`:
  forward the original void-returning operation, then invalidate the slot map.
- `CCTextureAtlas::removeQuadAtIndex` and `removeQuadsAtIndex`: forward the
  original void-returning operation, then invalidate the slot map.
- Both verified overloads of `CCTextureAtlas::moveQuadsFromIndex` (two and
  three unsigned-integer arguments after `this`): forward the original
  operation, then invalidate the slot map.
- `CCTextureAtlas::increaseTotalQuadsWith`: forwards the original
  void-returning operation, then invalidates the slot map because the live
  quad count changes.
- `CCTextureAtlas::drawNumberOfQuads(count, start)`: observes the dirty byte
  before forwarding the original draw. If a non-empty draw enters with dirty
  data, it conservatively invalidates cached quad data after the original
  call. The engine issues `glBufferSubData` and clears its dirty byte, but GL
  success is not observable without consuming the game's GL error state, so
  this hook does not claim or count a confirmed successful upload.

The batch child count is intentionally reported as zero until its runtime
object layout is independently validated. The dirty-draw observer only invalidates cached quad data on an upload
attempt; it deliberately does not count the operation as a confirmed
successful GL upload. Do not use these counters as proof that GPU culling is
safe to enable.

## API contract

Create and initialize a Bismuth instance as usual, then call
`bismuth_install_cocos_hooks(handle, error, error_capacity)` only after
`libcocos2dcpp.so` has loaded and before the hooked render path starts
executing. Installation is opt-in; loading `libbismuth.so` alone does not
install hooks.

When loaded through the repository APK injector, `JNI_OnLoad` attempts to install
the two hooks automatically because the injector places Bismuth immediately
after `System.loadLibrary("cocos2dcpp")`. Query `bismuth_auto_hook_status()`
and `bismuth_auto_hook_error()` to see whether exact-binary validation and
installation succeeded. A version mismatch is non-fatal to game startup.

All installed hooks are pass-through observers only. They do not skip transforms,
change GL state, replace draw calls, alter blend state, or submit Bismuth's
generated EBO. The original engine functions remain in the call chain.

`bismuth_destroy` detaches telemetry safely. The pass-through trampolines
remain installed for process lifetime rather than unpatching executable code
while game threads may be running.

## Remaining validation

- Build/CI must pass with the pinned Dobby dependency.
- Confirm dirty VBO upload behavior and callback execution on-device.
- Install in the actual game process and verify the expected build-ID check.
- Confirm callbacks execute on the render thread and that counters increase.
- The insert/remove/move/count mutation signatures were checked against the
  target ARM64 disassembly; runtime callback execution still requires testing.
- Calibrate sprite bounds against the actual camera/view coordinate space.
- Preserve the original draw path until alpha, clipping, blend, painter order,
  and device performance are validated.

No FPS improvement or in-game runtime success is claimed by this adapter.
