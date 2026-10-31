import hashlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import boot_inputs
import root_patch


def cpio(files):
    result = bytearray()
    for name, data in [*files.items(), ('TRAILER!!!', b'')]:
        encoded = name.encode() + b'\0'
        fields = [1, 0o100644, 0, 0, 1, 0, len(data), 0, 0, 0, 0, len(encoded), 0]
        result += b'070701' + b''.join(f'{value:08x}'.encode() for value in fields)
        result += encoded
        result += bytes(-len(result) % 4)
        result += data
        result += bytes(-len(result) % 4)
    return bytes(result)


class InputTests(unittest.TestCase):
    def test_no_optional_files_keeps_existing_boot_exactly(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            stock = root / 'stock.img'
            original = b'existing boot\0Recovery\0Magisk\0KernelSU\0' + bytes(4096)
            stock.write_bytes(original)
            with patch.object(boot_inputs, 'unpack', side_effect=AssertionError('Unexpected unpack')), \
                 patch.object(root_patch, 'patch_root', side_effect=AssertionError('Unexpected root patch')):
                output, record = boot_inputs.prepare_boot(stock, None, None, None, None, root, root)
            self.assertEqual(output, stock)
            self.assertEqual(stock.read_bytes(), original)
            self.assertIsNone(record['recovery'])
            self.assertIsNone(record['root'])
            self.assertEqual(set(root.iterdir()), {stock})

    def test_cpio_payload_and_bounds(self):
        data = cpio({'init': b'\0abc', 'system/bin/recovery': b'ELF'})
        self.assertEqual(boot_inputs.cpio_entries(data)['init'], b'\0abc')
        with self.assertRaises(ValueError):
            boot_inputs.cpio_entries(data[:-112])

    def test_recovery_does_not_replace_kernel_or_write_sources(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            stock, recovery = root / 'stock.img', root / 'rec.img'
            stock.write_bytes(bytes(4096))
            recovery.write_bytes(b'original-recovery')
            original_sha = hashlib.sha256(recovery.read_bytes()).digest()
            ramdisk = cpio({'system/bin/recovery': b'recovery'})

            def unpack(tool, image, directory):
                directory.mkdir()
                (directory / 'kernel').write_bytes(b'android-kernel' if directory.name != 'recovery-input' else b'recovery-kernel')
                (directory / 'ramdisk.cpio').write_bytes(ramdisk)
                return directory

            def repack(tool, action, source, target, cwd):
                self.assertEqual((cwd / 'kernel').read_bytes(), b'android-kernel')
                Path(target).write_bytes(b'packed')

            with patch.object(boot_inputs, 'unpack', unpack), patch.object(boot_inputs, 'command', repack):
                output, record = boot_inputs.prepare_boot(stock, recovery, None, None, None, root, root)
            self.assertEqual(output.stat().st_size, stock.stat().st_size)
            self.assertTrue(record['recovery']['original_kernel_preserved'])
            self.assertEqual(hashlib.sha256(recovery.read_bytes()).digest(), original_sha)
            self.assertEqual(stock.read_bytes(), bytes(4096))

    def test_ksu_reads_apk_assets_and_rejects_missing_kmi(self):
        header = bytearray(64)
        header[:6] = b'\x7fELF\x02\x01'
        struct.pack_into('<HH', header, 16, 1, 183)
        module = bytes(header) + b'name=kernelsu\0vermagic=6.1.1-android14 SMP\0'
        struct.pack_into('<H', header, 16, 2)
        init = bytes(header) + b'/init\0kernelsu.ko\0'
        compressor = lambda blob: zlib.compress(blob)[2:-4]
        data = b'android14-6.1_kernelsu.ko\0' + compressor(module) + bytes(512) + compressor(init) + bytes(512)
        with tempfile.TemporaryDirectory() as name:
            apk = Path(name) / 'ksu.apk'
            with zipfile.ZipFile(apk, 'w') as archive:
                archive.writestr('lib/arm64-v8a/libksud.so', data)
            self.assertEqual(root_patch.ksu_payloads(apk, 'android14-6.1'), (module, init))
            with self.assertRaisesRegex(ValueError, '无法'):
                root_patch.ksu_payloads(apk, 'android13-5.10')


if __name__ == '__main__':
    unittest.main()
