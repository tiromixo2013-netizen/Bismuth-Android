#!/usr/bin/env python3
"""Inject libbismuth.so into an apktool-decoded Geometry Dash APK tree.

This patches only the ARM64 native-library load. It does not rebuild or sign the APK.
"""
from __future__ import annotations
import argparse
import shutil
import struct
from pathlib import Path

SYSTEM_LOAD = "Ljava/lang/System;->loadLibrary(Ljava/lang/String;)V"

def is_arm64_elf(path: Path) -> bool:
    data = path.read_bytes()
    return (len(data) >= 20 and data[:4] == b"\x7fELF" and data[4] == 2
            and data[5] == 1 and struct.unpack_from("<H", data, 18)[0] == 183)

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("decoded_apk", type=Path, help="apktool decoded APK directory")
    parser.add_argument("libbismuth", type=Path, help="built ARM64 libbismuth.so")
    args = parser.parse_args()
    root, library = args.decoded_apk.resolve(), args.libbismuth.resolve()
    if not root.is_dir():
        parser.error(f"decoded APK directory does not exist: {root}")
    if not library.is_file() or not is_arm64_elf(library):
        parser.error("libbismuth.so must be a valid little-endian ELF64 AArch64 library")

    matches = []
    for smali in root.rglob("GeometryJump.smali"):
        try:
            lines = smali.read_text(encoding="utf-8").splitlines()
        except UnicodeDecodeError:
            continue
        for index, line in enumerate(lines):
            if '"cocos2dcpp"' not in line or "const-string" not in line:
                continue
            parts = line.strip().split()
            if len(parts) < 2:
                continue
            register = parts[1].rstrip(",")
            call_index = next((j for j in range(index + 1, min(index + 5, len(lines)))
                               if SYSTEM_LOAD in lines[j] and f"{{{register}}}" in lines[j]), None)
            if call_index is not None:
                matches.append((smali, lines, call_index, register))

    if len(matches) != 1:
        parser.error(f"expected exactly one GeometryJump loadLibrary(cocos2dcpp) call; "
                     f"found {len(matches)}. No files were changed.")
    smali, lines, call_index, register = matches[0]
    if any('"bismuth"' in line and "const-string" in line for line in lines):
        print("Bismuth load call already appears in GeometryJump.smali; refusing duplicate patch.")
        return 0

    lines[call_index + 1:call_index + 1] = [
        f'    const-string {register}, "bismuth"',
        f'    invoke-static {{{register}}}, Ljava/lang/System;->loadLibrary(Ljava/lang/String;)V',
    ]
    destination = root / "lib" / "arm64-v8a"
    destination.mkdir(parents=True, exist_ok=True)
    shutil.copy2(library, destination / "libbismuth.so")
    smali.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Patched {smali.relative_to(root)}")
    print(f"Copied ARM64 library to {(destination / 'libbismuth.so').relative_to(root)}")
    print("Next: rebuild with apktool, sign the APK, then install on an ARM64 device.")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
