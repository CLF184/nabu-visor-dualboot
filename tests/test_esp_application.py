"""FD passthrough accepts both formats without requiring a particular BDS."""
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import boot_inputs


def make_fd(filesystem):
    fd = bytearray(b'\xff' * 0x300000)
    fd[:72] = bytes(72)
    fd[16:32] = uuid.UUID(filesystem).bytes_le
    struct.pack_into('<Q', fd, 32, len(fd))
    fd[40:44] = b'_FVH'
    struct.pack_into('<H', fd, 48, 72)
    fd[55] = 2
    struct.pack_into('<II', fd, 56, 0x300, 4096)
    struct.pack_into('<H', fd, 50, -sum(struct.unpack('<36H', fd[:72])) & 0xffff)
    return bytes(fd)


class PassthroughTests(unittest.TestCase):
    def test_ffs2_ffs3_are_not_repacked_or_inspected_for_known_bds(self):
        for guid in ('8c8ce578-8a3d-4f1c-9935-896185c32dd3',
                     '5473c07a-3dcb-4dca-bd6f-1e9689e7349a'):
            with self.subTest(guid=guid), tempfile.TemporaryDirectory() as name:
                root = Path(name)
                image = root/'uefi.fd'
                fd = make_fd(guid)
                image.write_bytes(b'shim' + fd)
                selected, record = boot_inputs.load_uefi(image, None, root)
                self.assertEqual(selected, fd)
                self.assertEqual(record['fd_offset_in_payload'], 4)
                self.assertEqual(image.read_bytes(), b'shim' + fd)

    def test_ignore_false_signature_and_reject_ambiguous_fds(self):
        fd = make_fd('8c8ce578-8a3d-4f1c-9935-896185c32dd3')
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            image = root/'uefi.img'
            image.write_bytes(b'_FVH' + bytes(150) + fd)
            selected, record = boot_inputs.load_uefi(image, None, root)
            self.assertEqual(selected, fd)
            self.assertEqual(record['fd_offset_in_payload'], 154)
            image.write_bytes(fd + fd)
            with self.assertRaisesRegex(ValueError, '唯一'):
                boot_inputs.load_uefi(image, None, root)

    def test_reject_corrupt_outer_header_and_wrong_capacity(self):
        fd = make_fd('5473c07a-3dcb-4dca-bd6f-1e9689e7349a')
        corrupted = bytearray(fd)
        corrupted[50] ^= 1
        with self.assertRaisesRegex(ValueError, 'checksum'):
            boot_inputs.validate_passthrough_fd(bytes(corrupted))
        with self.assertRaises(ValueError):
            boot_inputs.validate_passthrough_fd(fd[:-16])

    def test_reject_invalid_block_map_even_with_valid_checksum(self):
        fd = bytearray(make_fd('8c8ce578-8a3d-4f1c-9935-896185c32dd3'))
        struct.pack_into('<I', fd, 56, 1)
        struct.pack_into('<H', fd, 50, 0)
        struct.pack_into('<H', fd, 50, -sum(struct.unpack('<36H', fd[:72])) & 0xffff)
        with self.assertRaisesRegex(ValueError, 'block map'):
            boot_inputs.validate_passthrough_fd(bytes(fd))


if __name__ == '__main__':
    unittest.main()
