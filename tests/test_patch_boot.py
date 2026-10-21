"""Check aligned FD offsets and reject malformed kernel/FD handoffs."""
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import patch_boot


class KernelAlignmentTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        directory = Path(self.temp.name)
        self.shellcode = bytes(range(0xb0))
        (directory / 'ShellCode.Nabu2.bin').write_bytes(self.shellcode)
        self.override = patch.object(patch_boot, 'OFFICIAL', directory)
        self.override.start()
        self.addCleanup(self.override.stop)

    def fixture(self, size):
        original = bytes((n * 7) & 255 for n in range(size))
        offset = (size + 15) & ~15
        fd = b'firmware volume bytes'
        kernel = bytearray(original + bytes(offset - size) + fd)
        kernel[0x40:0xb0] = self.shellcode[0x40:]
        struct.pack_into('<II', kernel, 0, 0x14000010, 0x1400002f)
        struct.pack_into('<QQQ', kernel, 0x20, 0x9fc00000, 0x300000, offset)
        return kernel, original, fd

    def test_all_alignment_remainders(self):
        for remainder in range(16):
            with self.subTest(remainder=remainder):
                kernel, original, fd = self.fixture(0xc0 + remainder)
                patch_boot.verify_patched(kernel, original, fd, 0xc0)

    def test_reject_nonzero_padding(self):
        kernel, original, fd = self.fixture(0xc8)
        kernel[len(original)] = 1
        with self.assertRaisesRegex(SystemExit, '对齐填充'):
            patch_boot.verify_patched(kernel, original, fd, 0xc0)

    def test_reject_unaligned_header_offset(self):
        kernel, original, fd = self.fixture(0xc8)
        struct.pack_into('<Q', kernel, 0x30, len(original))
        with self.assertRaisesRegex(SystemExit, '分流配置'):
            patch_boot.verify_patched(kernel, original, fd, 0xc0)

    def test_reject_missing_padding(self):
        kernel, original, fd = self.fixture(0xc8)
        del kernel[len(original):0xd0]
        with self.assertRaisesRegex(SystemExit, '内核大小'):
            patch_boot.verify_patched(kernel, original, fd, 0xc0)

    def test_reject_changed_fd(self):
        kernel, original, fd = self.fixture(0xc8)
        kernel[-1] ^= 1
        with self.assertRaisesRegex(SystemExit, 'FD 并未原样'):
            patch_boot.verify_patched(kernel, original, fd, 0xc0)


if __name__ == '__main__':
    unittest.main()
