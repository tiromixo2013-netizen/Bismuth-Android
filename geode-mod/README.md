# Bismuth Android — Geode Android64

Bismuth Android is packaged as a Geode mod for Geometry Dash 2.2081 ARM64.

## Pause-menu access

- The Bismuth settings button is added to the left side of the regular gameplay Pause layer.
- The same button is added to the left side of Editor Pause.
- It is placed below the upper-left mod-button area to avoid overlapping Better Visuals.
- Pressing it opens the Geode settings popup. Closing that popup returns to the same pause layer.
- No floating in-game overlay is created.

## Current safety state

- Cocos hooks are build-ID gated to the inspected Geometry Dash 2.2081 ARM64 library.
- Passive telemetry can be enabled or disabled from the settings popup.
- Asynchronous GPU timing is optional and only runs when the driver exposes the required timer-query extension.
- The experimental all-visible GPU EBO bridge is hard-disabled. It is not real sprite culling and is not exposed as a user toggle.
- The original Cocos draw path remains active.

## Build

The repository workflow builds the Android64 .geode artifact using the Geode SDK. Install that artifact through Geode and restart Geometry Dash.

The first device test should use one repeatable level and verify:
1. the mod loads without a Geode crash report;
2. the icon appears below Better Visuals in gameplay Pause;
3. the icon appears in Editor Pause;
4. opening and closing settings returns to the same pause screen;
5. telemetry toggles do not affect gameplay behavior.

No FPS gain or full GPU-culling activation is claimed before these checks.
