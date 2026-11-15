import importlib.util
import struct
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "tools" / "inspect_target_elf.py"
SPEC = importlib.util.spec_from_file_location("inspect_target_elf", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def make_elf(machine=183):
    header_struct = MODULE.ELF_HEADER
    section_struct = MODULE.SECTION_HEADER
    symbol_struct = MODULE.SYMBOL
    strings = b"\0_ZN7cocos2d14CCTextureAtlas19drawNumberOfQuadsEjj\0"
    symbol_name_offset = 1
    symbols = symbol_struct.pack(0, 0, 0, 0, 0, 0)
    symbols += symbol_struct.pack(symbol_name_offset, 0x12, 0, 1, 0x00B33AF0, 64)
    shoff = header_struct.size
    symoff = shoff + 3 * section_struct.size
    stroff = symoff + len(symbols)
    sections = section_struct.pack(0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
    sections += section_struct.pack(0, MODULE.SHT_DYNSYM, 0, 0, symoff,
                                    len(symbols), 2, 0, 8, symbol_struct.size)
    sections += section_struct.pack(0, MODULE.SHT_STRTAB, 0, 0, stroff,
                                    len(strings), 0, 0, 1, 0)
    ident = b"\x7fELF" + bytes([2, 1, 1]) + bytes(9)
    header = header_struct.pack(ident, MODULE.ET_DYN, machine, 1, 0, 0,
                                shoff, 0, header_struct.size, 0, 0,
                                section_struct.size, 3, 0)
    return header + sections + symbols + strings


class TargetElfInspectorTests(unittest.TestCase):
    def test_reads_arm64_dynamic_cocos2d_symbol(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "libcocos2dcpp.so"
            path.write_bytes(make_elf())
            self.assertEqual(
                MODULE.inspect(path),
                [("_ZN7cocos2d14CCTextureAtlas19drawNumberOfQuadsEjj",
                  0x00B33AF0, 64)],
            )

    def test_rejects_non_arm64_library(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "wrong.so"
            path.write_bytes(make_elf(machine=62))
            with self.assertRaises(MODULE.ElfError):
                MODULE.inspect(path)


if __name__ == "__main__":
    unittest.main()
