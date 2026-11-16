# Bismuth Android — Geode package shell

This is the first Geode-native Android package scaffold for Geometry Dash 2.2081 ARM64, targeting Geode v5.11.2.

## Current scope

- Installable Geode mod structure and metadata.
- Geode settings page with an ON/OFF preference and diagnostic logging toggle.
- Android64 build workflow.

## Important limitation

The ON/OFF setting currently stores and reports the user's preference only. The experimental Bismuth GLES/Cocos GPU draw bridge from the standalone native-library project is **not linked to this Geode module** and is not activated by the setting. No rendering optimization or FPS gain is claimed. This package is a settings/loader port scaffold, not the completed renderer mod.

Build with the official Geode SDK using `geode build -p android64`.
