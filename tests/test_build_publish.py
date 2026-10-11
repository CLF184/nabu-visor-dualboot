"""Failure injection for build publication; temporary files only."""
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile
import build_publish as publisher


class PublicationTests(unittest.TestCase):
    def setUp(self):
        self.workspace = tempfile.TemporaryDirectory()
        self.root = Path(self.workspace.name)
        self.backup = self.root/'out/.build-backup'

    def tearDown(self):
        self.workspace.cleanup()

    def file(self, name, contents):
        path = self.root/name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(contents)
        return path

    def test_success_overwrites_only_selected_files_and_saves_backup(self):
        src = self.file('work/boot.img', b'new boot')
        dst = self.file('out/boot.img', b'old boot')
        note = self.file('out/my-note.txt', b'keep')
        backup = publisher.publish_files([(src, dst)], self.backup)
        self.assertEqual(dst.read_bytes(), b'new boot')
        self.assertEqual(note.read_bytes(), b'keep')
        with zipfile.ZipFile(backup) as archive:
            self.assertEqual(archive.read('files/0'), b'old boot')
        self.assertFalse((self.backup/'publish.lock').exists())

    def test_staging_failure_leaves_all_existing_outputs_unchanged(self):
        src = self.file('work/boot.img', b'new boot')
        dst = self.file('out/boot.img', b'old boot')
        with patch.object(publisher.shutil, 'copy2', side_effect=OSError('simulated disk full')):
            with self.assertRaises(OSError):
                publisher.publish_files([(src, dst)], self.backup)
        self.assertEqual(dst.read_bytes(), b'old boot')
        self.assertFalse(list(dst.parent.glob('.nabu-publish-*')))
        self.assertFalse((self.backup/'publish.lock').exists())

    def test_commit_failure_restores_old_files_and_removes_new_outputs(self):
        first = self.file('out/boot.img', b'old boot')
        added = self.root/'out/new.efi'
        last = self.file('out/SHA256SUMS.txt', b'old checksums')
        replacements = [(self.file('work/boot.img', b'new boot'), first),
                        (self.file('work/new.efi', b'new app'), added),
                        (self.file('work/SHA256SUMS.txt', b'new checksums'), last)]
        original_replace = publisher.os.replace
        def fail_last(src, dst):
            if Path(dst)==last:
                raise PermissionError('simulated locked checksum file')
            return original_replace(src, dst)
        with patch.object(publisher.os, 'replace', side_effect=fail_last):
            with self.assertRaises(PermissionError):
                publisher.publish_files(replacements, self.backup)
        self.assertEqual(first.read_bytes(), b'old boot')
        self.assertEqual(last.read_bytes(), b'old checksums')
        self.assertFalse(added.exists())
        self.assertTrue((self.backup/'backup.zip').exists())
        self.assertFalse(list(first.parent.glob('.nabu-*')))
        self.assertFalse((self.backup/'publish.lock').exists())


if __name__ == '__main__':
    unittest.main()
