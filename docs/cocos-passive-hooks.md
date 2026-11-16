# Build-verified Cocos2d passive hooks

## Scope

The Android ARM64 build can compile a passive hook adapter for the exact
Geometry Dash 2.2081 Cocos2d binary identified by GNU Build ID
`ca621323d54c1d94f5f13a2778ce9c8baa31b9a8`.

The adapter uses Dobby at pinned commit
`5dfc8546954ce3b3198132ab13fddb89ee92cdd7` (Apache-2.0) for ARM64 inline
trampolines. It does not patch unknown builds: it checks the loaded module's
GNU Build ID and verifies both resolved symbols belong to
`libcocos2dcpp.so` before installing either hook.

## Installed passive hooks

- `CCSpriteBatchNode::draw()`: records a batch draw event, then calls the
  original function unchanged.
- `CCTextureAtlas::updateQuad(quad, index)`: calls the original function
  first, then records a successful quad-content write.

The batch child count is intentionally reported as zero until its runtime
object layout is independently validated. Structural atlas mutations and
dirty VBO uploads are not hooked yet. Do not use these counters as proof that
the slot map is stable or that GPU culling is safe to enable.

## API contract

Create and initialize a Bismuth instance as usual, then call
`bismuth_install_cocos_hooks(handle, error, error_capacity)` only after
`libcocos2dcpp.so` has loaded and before the hooked render path starts
executing. Installation is opt-in; loading `libbismuth.so` alone does not
install hooks.

The two hooks are pass-through observers only. They do not skip transforms,
change GL state, replace draw calls, alter blend state, or submit Bismuth's
generated EBO. The original engine functions remain in the call chain.

`bismuth_destroy` detaches telemetry safely. The pass-through trampolines
remain installed for process lifetime rather than unpatching executable code
while game threads may be running.

## Remaining validation

- Build/CI must pass with the pinned Dobby dependency.
- Install in the actual game process and verify the expected build-ID check.
- Confirm callbacks execute on the render thread and that counters increase.
- Add structural mutation hooks only after every signature and success
  convention is validated against the binary.
- Calibrate sprite bounds against the actual camera/view coordinate space.
- Preserve the original draw path until alpha, clipping, blend, painter order,
  and device performance are validated.

No FPS improvement or in-game runtime success is claimed by this adapter.
