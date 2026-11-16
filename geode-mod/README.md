# Bismuth Android — Geode GPU bridge

Android ARM64 Geode mod for Geometry Dash 2.2081, targeting Geode 5.11.2.

## Integrated in v0.2.0

- Geode settings page with an OFF-by-default GPU draw bridge toggle.
- Build-ID-gated Cocos2d hooks for the verified Geometry Dash 2.2081 binary.
- GLES 3.1 compute shaders linked directly into the Geode module.
- Compute-generated 16-bit atlas element-index buffer and indirect indexed draw.
- Original Cocos renderer remains the fallback whenever validation, initialization, upload, or dispatch fails.
- Diagnostic logging for hook installation and bridge state.

## Important limitations

This is an experimental GPU draw-path integration, **not yet a completed GPU culling optimizer**. The current bridge deliberately marks all atlas quads visible because camera-space bounds and safe per-quad visibility extraction have not been calibrated. CPU still prepares and uploads the candidate records. Therefore this build demonstrates real GLES compute execution on supported draw calls, but it does not establish CPU offload savings or FPS improvement; it may be slower due to compute dispatch and buffer work.

The bridge is disabled by default. It only attempts to hook the exact Cocos2d build ID embedded in src/cocos_hooks.cpp; unknown or mismatched game binaries are left untouched. Runtime stability and performance must be tested on the target phone before enabling it for normal play.

Build with the official Geode SDK using geode build -p android64.
