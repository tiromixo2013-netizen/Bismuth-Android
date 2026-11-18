# Bismuth Android — Geode Android64

Bismuth Android is packaged as a Geode mod for Geometry Dash 2.2081 ARM64.

## Pause-menu access

- The Bismuth settings button is added to the left side of the regular gameplay Pause layer.
- The same button is added to the left side of Editor Pause.
- It is placed below the upper-left mod-button area to avoid overlapping Better Visuals.
- Pressing it opens the Geode settings popup. Closing that popup returns to the same pause layer.
- The live telemetry HUD appears at the upper-left of gameplay when enabled.
- Advanced Monitor and its rolling frame-cadence graph can be enabled in Bismuth settings.

## Current safety state

- The Geode package runs in safe mode: it does not install Cocos2d/Dobby hooks from the Geode Loaded event.
- This avoids patching renderer methods after the game render path may already be active or while other mods own hooks on those methods.
- The original Cocos renderer remains untouched.
- Native hook and GPU-timing code remains experimental and is not activated by this Geode package.
- The experimental all-visible GPU EBO bridge remains disabled and is not real sprite culling.

## Crash-safety note

The earlier crash report documented a SIGSEGV in `libc memcpy` reached from
`CCSpriteBatchNode::appendChild` while `CCLabelBMFont` was constructing
level-page text. `node-ids` and `QOLMod` appeared in the stack, but that trace
did not prove a unique culprit. The in-game HUD therefore uses a standalone
`CCLabelTTF` instead of creating another BMFont sprite-child batch. Its text
refresh is throttled to 4 Hz to avoid rebuilding label textures every game
update. This is a risk-reduction measure, not proof that the original crash is
fixed; the original stack and level-page reproduction still need verification.

## Build

The repository workflow builds the Android64 .geode artifact using the Geode SDK. Install that artifact through Geode.

The first device test should use one repeatable level and verify:
1. the mod loads without a Geode crash report;
2. the HUD appears and can be disabled;
3. the Advanced Monitor and graph settings work;
4. the icon appears in gameplay Pause and Editor Pause;
5. opening and closing settings returns to the same pause screen;
6. opening level selection and scrolling pages does not crash.

The HUD measures game-loop update cadence, `PlayLayer::update` wall time,
process RSS, process CPU time, best-effort GPU busy counters when the kernel
exposes them, and SpriteBatch draw invocations/child counts/CPU submission time.
It does not measure display-present FPS, whole-frame GPU time, or raw GLES draw
calls. The optional Shizuku Companion bridge adds best-effort whole-device CPU,
GPU busy, thermal-zone, and system RAM readings over localhost. Missing or
inaccessible sensors remain N/A. No performance gain is claimed without
on-device A/B testing.
