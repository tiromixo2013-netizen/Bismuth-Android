# Bismuth-Android

Android ARM64 Geode mod for Geometry Dash 2.2081, with a native GLES 3.1 renderer-research backend.

The project explores GPU-assisted sprite classification, stable visible-index compaction, and indirect-draw preparation. It is a clean-room implementation of general rendering concepts; it does not copy Bismuth PC source code.

> **Current Geode status: conservative render telemetry.** The mod keeps the original Cocos drawing implementation and adds a Geode-managed pass-through hook around `CCTextureAtlas::drawNumberOfQuads` to measure atlas draw submissions and CPU wall time. Dobby hooks, GPU timing, and the experimental draw bridge remain disabled; no Android device performance gain is claimed.

## What exists

- Portable CPU reference culler with stable input ordering.
- Optional Android GLES 3.1 compute backend.
- Compute stages for classification, group offsets, stable scatter, and atlas-index generation.
- Embedded GPU shaders generated from `shaders/*.comp` during CMake configuration.
- Host-side unit tests and GitHub Actions CI.
- Geode Android64 pause-menu settings entry for gameplay and editor pause screens.
- Compact live HUD with measured update cadence, PlayLayer CPU time, frame-interval p50/p95/p99, hitch counter, process RSS, and actual Cocos atlas draw submissions plus their CPU wall time.

## Build host reference tests

```bash
cmake -S . -B build -DBISMUTH_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Android build

Use Android NDK + CMake and set `-DBISMUTH_ENABLE_GLES31=ON`. The calling application must create and own an active GLES 3.1 context before initializing `Gles31Culler`.

## Important limitations

- Native Cocos hooks are experimental and dormant in the current Geode package.
- Hook installation needs a verified pre-render lifecycle point and coexistence tests with other mods.
- No automatic sprite-bound extraction or production texture/material batching is integrated.
- The compute prototype supports up to 16,384 candidates per dispatch.
- The GPU compute outputs are not a validated Geometry Dash renderer replacement.
- No FPS or latency improvement is claimed until on-device A/B profiling is complete.

See [docs/renderer-integration.md](docs/renderer-integration.md) for the required integration work, [docs/performance-telemetry.md](docs/performance-telemetry.md) for telemetry design, and [docs/cocos-passive-hooks.md](docs/cocos-passive-hooks.md) for the experimental hook adapter's safety status.

## Geode Android64 package

The installable package lives in [geode-mod](geode-mod/README.md) and is built by
[the Geode Android64 workflow](.github/workflows/geode-android64.yml). It is intended to be installed through Geode without APK patching, resigning, or root access. The current package adds settings access to both gameplay Pause and Editor Pause, plus a top-left gameplay HUD. A narrow Geode-managed atlas-draw hook measures around the original Cocos method without changing draw data or order.

## Inspect the exact target game library

Before attempting native renderer hooks, inspect the `libcocos2dcpp.so` extracted from the exact Geometry Dash 2.2081 ARM64 APK:

```bash
python3 tools/inspect_target_elf.py /path/to/libcocos2dcpp.so
```

Reported ELF addresses are not runtime addresses and do not establish that a function is safe to hook.

## Native integration status

The repository retains a C ABI and experimental GLES backend for a future controlled native integration. The current Geode package activates only the Geode-managed pass-through atlas-draw telemetry hook. The native Dobby hook adapter and experimental GPU draw bridge remain disabled.
