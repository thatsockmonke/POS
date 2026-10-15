"""The downloadable ZIP must include a complete bundle and preserve old outputs."""
import hashlib
from pathlib import Path
import tempfile
import unittest
import zipfile
from test_installer import posctl


class DownloadTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="pos-download-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.bundle = self.root / "bundle"
        self.bundle.mkdir()
        self.files = {"pos-installer-usb.bin": b"installer", "pos-install.zip": b"sd-package",
                      "INSTALL.md": b"instructions", "tools/posctl.py": b"usb helpers"}
        for name, data in self.files.items():
            path = self.bundle / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        self.output = self.root / "pos-test.zip"

    def test_complete_archive_and_checksum_are_downloadable_without_source_checkout(self):
        posctl.archive_bundle(self.bundle, self.output)
        with zipfile.ZipFile(self.output) as archive:
            self.assertIsNone(archive.testzip())
            self.assertEqual(set(archive.namelist()), set(self.files))
            for name, data in self.files.items():
                self.assertEqual(archive.read(name), data)
        expected = hashlib.sha256(self.output.read_bytes()).hexdigest()
        self.assertEqual(Path(str(self.output)+".sha256").read_text(), f"{expected}  pos-test.zip\n")
        before = self.output.read_bytes()
        with self.assertRaises(ValueError):
            posctl.archive_bundle(self.bundle, self.output)
        self.assertEqual(self.output.read_bytes(), before)

    def test_export_refuses_nested_output_partial_files_and_symlinks(self):
        with self.assertRaises(ValueError):
            posctl.archive_bundle(self.bundle, self.bundle/"recursive.zip")
        Path(str(self.output)+".part").write_bytes(b"interrupted previous export")
        with self.assertRaises(ValueError):
            posctl.archive_bundle(self.bundle, self.output)
        Path(str(self.output)+".part").unlink()
        (self.bundle/"unexpected-link").symlink_to(self.bundle/"INSTALL.md")
        with self.assertRaises(ValueError):
            posctl.archive_bundle(self.bundle, self.output)
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
