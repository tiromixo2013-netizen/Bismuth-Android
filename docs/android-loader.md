# Android ARM64 loader preparation

Bismuth is built as `libbismuth.so`. A plain shared library is not loaded by Geometry Dash
automatically: Android must call `System.loadLibrary("bismuth")` from the app's startup path.
A public native-mod template specifically targeting GD 2.2081 Android ARM64 documents adding
the load immediately after `cocos2dcpp` in
`com/robtopx/geometryjump/GeometryJump`. This project follows that loading order without
copying its hook implementation or binary code.

## Prepare a decoded APK tree

1. Use your own Geometry Dash 2.2081 APK and decode it with apktool.
2. Build the ARM64 library from this repository's `android-arm64` GitHub Actions artifact.
3. Run:

   ```sh
   python3 tools/inject_android_library.py decoded-apk/ libbismuth.so
   ```

4. Rebuild the APK with apktool, sign it with your own signing key, and install it.
5. Keep the original APK and signing key safe. An APK signed with a different key cannot
   update the store-installed package in place; it may need to be installed separately or
   the original app uninstalled first.

The script validates ELF64 AArch64, finds exactly one `GeometryJump.smali` call loading
`cocos2dcpp`, adds the Bismuth load immediately afterward, and copies the library into
`lib/arm64-v8a/`. It refuses ambiguous input and does not delete other ABIs. It does not
rebuild or sign the APK.

## Important current limitation

This only makes Android attempt to load the native library. It does **not** yet hook the
Geometry Dash render loop, initialize GLES on the game's render thread, replace sprite draw
submission, or activate GPU culling in gameplay. Those require verified 2.2081 ARM64 renderer
integration and in-game validation. A successful APK launch alone does not prove that the
renderer optimization is active.
