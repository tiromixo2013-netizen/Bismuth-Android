import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "tools" / "inject_android_library.py"

class AndroidInjectorTests(unittest.TestCase):
    def test_injects_after_cocos_and_copies_arm64_library(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / "decoded"
            smali = root / "smali" / "com" / "robtopx" / "geometryjump" / "GeometryJump.smali"
            smali.parent.mkdir(parents=True)
            smali.write_text(
                ".method static constructor <clinit>()V\n"
                "    .locals 1\n"
                '    const-string v0, "cocos2dcpp"\n'
                "    invoke-static {v0}, Ljava/lang/System;->loadLibrary(Ljava/lang/String;)V\n"
                "    return-void\n"
                ".end method\n",
                encoding="utf-8",
            )
            library = Path(temp) / "libbismuth.so"
            elf = bytearray(64)
            elf[:4] = b"\x7fELF"
            elf[4] = 2
            elf[5] = 1
            struct.pack_into("<H", elf, 18, 183)
            library.write_bytes(elf)

            subprocess.run([sys.executable, str(SCRIPT), str(root), str(library)],
                           check=True, capture_output=True, text=True)
            patched = smali.read_text(encoding="utf-8")
            self.assertLess(patched.index('"cocos2dcpp"'), patched.index('"bismuth"'))
            self.assertIn('invoke-static {v0}, Ljava/lang/System;->loadLibrary(Ljava/lang/String;)V',
                          patched)
            copied = root / "lib" / "arm64-v8a" / "libbismuth.so"
            self.assertTrue(copied.is_file())
            self.assertEqual(copied.read_bytes(), library.read_bytes())

            # A second pass must not add a duplicate load statement.
            subprocess.run([sys.executable, str(SCRIPT), str(root), str(library)],
                           check=True, capture_output=True, text=True)
            self.assertEqual(patched, smali.read_text(encoding="utf-8"))

if __name__ == "__main__":
    unittest.main()
