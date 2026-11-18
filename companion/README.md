# Bismuth Companion (Android 15)

A separate Android application that uses the official Shizuku API to start a small UserService under Shizuku's shell identity. It streams best-effort device telemetry to the Bismuth Geode mod over a loopback-only TCP connection.

## Build

From this directory with JDK 17, Android SDK 35, and Gradle 8.11+:

```sh
gradle :app:assembleDebug
```

The GitHub Actions workflow `.github/workflows/shizuku-companion.yml` builds and uploads the debug APK.

## Use on the test device

1. Start Shizuku using Wireless debugging.
2. Install and open Bismuth Companion.
3. Press **Connect to Shizuku & start bridge**, then approve the Shizuku permission prompt.
4. Launch Geometry Dash with Bismuth installed. The mod connects to `127.0.0.1:38471`.
5. Keep the Companion service running while playing. Stop it with the app's Stop button.

## Data protocol

The UserService emits one CSV line every 500 ms:

`device_cpu_percent,gpu_busy_percent,cpu_temp_c,gpu_temp_c,ram_used_mb,ram_total_mb`

Unavailable readings are `-1`. The bridge binds only to IPv4 loopback; it does not listen on Wi-Fi or mobile interfaces.

## Important limitations

- Shizuku's ADB identity is UID 2000, not root. Read access varies by OEM/kernel and Android build.
- GPU utilization requires readable cumulative `busy_time`/`total_time` counters under `/sys/class/devfreq`; GPU frequency is deliberately not mislabeled as utilization.
- CPU/GPU temperatures depend on Samsung thermal-zone names and permissions. Missing readings stay unavailable.
- System CPU is whole-device load, distinct from the game's process CPU percentage already measured by the mod.
- This initial bridge is a development build. The loopback socket transport and sensor availability must be verified on the Galaxy A05 before considering it production-ready.
