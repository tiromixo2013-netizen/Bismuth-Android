#!/usr/bin/env python3
"""Inspect a Geometry Dash Android ARM64 libcocos2dcpp.so for Cocos2d dynsyms.

This is reconnaissance only: symbol presence/values do not prove a safe hook point.
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

ELF_HEADER = struct.Struct("<16sHHIQQQIHHHHHH")
SECTION_HEADER = struct.Struct("<IIQQQQIIQQ")
SYMBOL = struct.Struct("<IBBHQQ")
EM_AARCH64 = 183
ET_DYN = 3
SHT_DYNSYM = 11
SHT_STRTAB = 3

TOKENS = ("CCNode", "CCSprite", "CCSpriteBatchNode", "CCTextureAtlas")


class ElfError(ValueError):
    pass


def inspect(path: Path) -> list[tuple[str, int, int]]:
    data = path.read_bytes()
    if len(data) < ELF_HEADER.size:
        raise ElfError("file is too small to contain an ELF64 header")
    header = ELF_HEADER.unpack_from(data)
    ident = header[0]
    if ident[:4] != b"\\x7fELF":
        raise ElfError("not an ELF file")
    if ident[4] != 2 or ident[5] != 1:
        raise ElfError("expected ELF64 little-endian")
    e_type, e_machine = header[1], header[2]
    if e_type != ET_DYN:
        raise ElfError("expected a shared-library ELF (ET_DYN)")
    if e_machine != EM_AARCH64:
        raise ElfError(f"expected AArch64 (e_machine=183), got {e_machine}")
    shoff, shentsize, shnum = header[6], header[11], header[12]
    if shentsize < SECTION_HEADER.size or shnum == 0:
        raise ElfError("missing or unsupported section-header table")
    if shoff + shentsize * shnum > len(data):
        raise ElfError("section-header table extends beyond file")

    sections = []
    for i in range(shnum):
        off = shoff + i * shentsize
        sections.append(SECTION_HEADER.unpack_from(data, off))
    found: list[tuple[str, int, int]] = []
    for section in sections:
        sh_type, sh_offset, sh_size, sh_link, sh_entsize = (
            section[1], section[4], section[5], section[6], section[9]
        )
        if sh_type != SHT_DYNSYM:
            continue
        if sh_link >= len(sections):
            raise ElfError("dynamic-symbol section has invalid string-table link")
        strings_section = sections[sh_link]
        if strings_section[1] != SHT_STRTAB:
            raise ElfError("dynamic-symbol link does not point to a string table")
        strings_off, strings_size = strings_section[4], strings_section[5]
        if strings_off + strings_size > len(data) or sh_offset + sh_size > len(data):
            raise ElfError("symbol or string table extends beyond file")
        entsize = sh_entsize or SYMBOL.size
        if entsize < SYMBOL.size or sh_size % entsize:
            raise ElfError("invalid dynamic-symbol entry size")
        strings = data[strings_off:strings_off + strings_size]
        for pos in range(sh_offset, sh_offset + sh_size, entsize):
            name_off, _info, _other, shndx, value, size = SYMBOL.unpack_from(data, pos)
            if not name_off or name_off >= len(strings) or shndx == 0:
                continue
            end = strings.find(b"\\0", name_off)
            if end < 0:
                continue
            name = strings[name_off:end].decode("utf-8", "replace")
            if any(token in name for token in TOKENS):
                found.append((name, value, size))
    return sorted(set(found), key=lambda item: (item[1], item[0]))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("library", type=Path, help="path to libcocos2dcpp.so")
    args = parser.parse_args()
    try:
        symbols = inspect(args.library)
    except (OSError, ElfError, struct.error) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2
    print(f"ELF: {args.library}")
    if not symbols:
        print("No matching exported dynamic symbols found (library may be stripped).")
        return 1
    print("VALUE              SIZE               SYMBOL")
    for name, value, size in symbols:
        print(f"0x{value:016x} 0x{size:016x} {name}")
    print("\\nNOTE: values are ELF symbol values, not verified runtime hook addresses.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
