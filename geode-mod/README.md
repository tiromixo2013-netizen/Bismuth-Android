# Bismuth Android — Geode Android64

Bismuth Android is packaged as a Geode mod for Geometry Dash 2.2081 ARM64.

## Pause-menu access

- The Bismuth settings button is added to the left side of the regular gameplay Pause layer.
- The same button is added to the left side of Editor Pause.
- It is placed below the upper-left mod-button area to avoid overlapping Better Visuals.
- Pressing it opens the Geode settings popup. Closing that popup returns to the same pause layer.
- No floating in-game overlay is created.

## Current safety state

- The Geode package runs in safe mode: it does not install Cocos2d/Dobby hooks from the Geode Loaded event.
- This avoids patching renderer methods after the game render path may already be active or while other mods own hooks on those methods.
- The original Cocos renderer remains untouched.
- Native hook and GPU-timing code remains experimental and is not activated by this Geode package.
- The experimental all-visible GPU EBO bridge remains disabled and is not real sprite culling.

## Build

The repository workflow builds the Android64 .geode artifact using the Geode SDK. Install that artifact through Geode.

The first device test should use one repeatable level and verify:
1. the mod loads without a Geode crash report;
2. the icon appears in gameplay Pause;
3. the icon appears in Editor Pause;
4. opening and closing settings returns to the same pause screen;
5. opening level selection and scrolling pages does not crash.

No FPS gain, live telemetry, or GPU-culling activation is claimed in safe mode.
