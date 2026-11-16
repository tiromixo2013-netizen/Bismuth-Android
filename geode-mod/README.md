# Bismuth Android — Geode observer and GLES compute prototype

Android ARM64 Geode package for Geometry Dash 2.2081, targeting Geode 5.11.2.

## Present in the current build

- Geode settings and diagnostic logging.
- Build-ID-gated passive Cocos2d atlas lifecycle hooks.
- GLES 3.1 compute shaders and a standalone candidate-culling/index-generation backend.
- Stable input-order compaction and 16-bit atlas element-index generation.
- Original Cocos renderer remains the only active draw path.
- The GPU draw bridge is hard-locked OFF through both the Geode setting and native API.

## Important limitations

This is **not yet an active GPU culling optimizer**. The hook layer observes atlas mutations and draw activity, but does not extract calibrated per-quad screen bounds or replace the game's draw submission. The compute backend can process caller-supplied bounds, but Geometry Dash does not yet supply those bounds to it. No CPU offload, FPS improvement, or reduced draw workload has been demonstrated.

The original Cocos renderer is preserved. Real integration still requires a validated mapping from transformed atlas vertices to conservative clip-space bounds, a state-preserving indexed draw replacement, and on-device tests covering alpha, painter order, clipping/stencil, shader effects, lifecycle and device drivers.

Build with the official Geode SDK using `geode build -p android64`.
