"""Native integration and host-tool tests; no physical device or network required."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("posctl", ROOT / "tools/posctl.py")
posctl = importlib.util.module_from_spec(spec)
spec.loader.exec_module(posctl)


def image(version="0.1.0-dev", project="pos_runtime"):
    blob = bytearray(40000)
    blob[0] = 0xE9
    blob[1] = 1
    blob[2] = 2
    blob[3] = 0x20
    blob[8] = 0xEE
    blob[23] = 1
    struct.pack_into("<H", blob, 12, 9)
    struct.pack_into("<I", blob, 24, 0x3C000020)
    struct.pack_into("<I", blob, 28, len(blob) - 32)
    struct.pack_into("<I", blob, 32, 0xABCD5432)
    blob[48:48 + len(version)] = version.encode()
    blob[80:80 + len(project)] = project.encode()
    checksum = 0xEF
    for byte in blob[32:]:
        checksum ^= byte
    blob.extend(bytes(15 - len(blob) % 16))
    blob.append(checksum)
    blob.extend(hashlib.sha256(blob).digest())
    return blob


class InstallerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compilation = tempfile.TemporaryDirectory(prefix="pos-native-")
        cls.runner = Path(cls.compilation.name) / "installer"
        sdk = Path(os.environ.get("IDF_PATH", "/workspace/pos-cloud/esp-idf-v5.5.5"))
        cjson = sdk / "components/json/cJSON"
        if not (cjson / "cJSON.c").is_file():
            raise RuntimeError("Activate ESP-IDF first; tests compile its pinned cJSON with the actual POS core")
        subprocess.run(["gcc", "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=undefined", "-fno-sanitize-recover=all", "-g",
                        "-I", str(ROOT / "components/pos_install/include"), "-I", str(cjson),
                        str(ROOT / "tests/host_runner.c"), str(ROOT / "components/pos_install/pos_install.c"),
                        str(ROOT / "components/pos_install/pos_hash.c"), str(cjson / "cJSON.c"),
                        "-lcrypto", "-lm", "-o", str(cls.runner)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.compilation.cleanup()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="pos-test-")
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.sd = self.base / "card"
        self.firmware = self.base / "firmware.bin"
        self.firmware.write_bytes(image())
        self.payload = self.base / "payload"
        (self.payload / "System/Assets").mkdir(parents=True)
        (self.payload / "System/Assets/large.bin").write_bytes(bytes(range(256)) * 300)
        (self.payload / "System/empty.txt").write_bytes(b"")
        posctl.make_package(self.firmware, self.payload, self.sd)
        self.manifest_path = self.sd / "pos-install/manifest.pos"
        self.manifest = json.loads(self.manifest_path.read_text())
        self.original_package = self.snapshot(self.sd / "pos-install")

    def snapshot(self, root):
        return {p.relative_to(root).as_posix(): p.read_bytes() for p in root.rglob("*") if p.is_file()}

    def run_core(self, command="install", *args, expected=0):
        result = subprocess.run([str(self.runner), str(self.sd), command, *args], capture_output=True, text=True)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        return result

    def save_manifest(self):
        self.manifest_path.write_text(json.dumps(self.manifest))

    def assert_no_writes(self):
        self.assertFalse((self.sd / "POS").exists())
        self.assertFalse((self.sd / "POS.installing").exists())
        self.assertFalse((self.sd / ".sim-ota.bin").exists())
        self.assertFalse((self.sd / ".sim-boot-selected").exists())

    def test_install_boot_and_cleanup_preserve_unlisted_user_files(self):
        (self.sd / "Documents").mkdir()
        (self.sd / "Documents/stevie.txt").write_text("keep me")
        (self.sd / "pos-install/my-notes.txt").write_text("keep this too")
        self.run_core()
        self.assertEqual(self.snapshot(self.sd / "pos-install") | {"my-notes.txt": b"keep this too"},
                         self.original_package | {"my-notes.txt": b"keep this too"})
        self.assertFalse((self.sd / "POS/System/.boot-ok").exists())
        self.run_core("cleanup", expected=1)
        self.run_core("boot")
        self.assertTrue((self.sd / "POS/System/.boot-ok").exists())
        self.assertEqual(self.snapshot(self.sd / "pos-install"), {"my-notes.txt": b"keep this too"})
        self.assertEqual((self.sd / "Documents/stevie.txt").read_text(), "keep me")
        self.run_core("boot")  # repeated boot/cleanup is idempotent

    def test_preflight_is_read_only(self):
        self.run_core("preflight")
        self.assert_no_writes()
        self.assertEqual(self.snapshot(self.sd / "pos-install"), self.original_package)

    def test_corrupt_payload_does_not_write(self):
        (self.sd / "pos-install/payload/System/Assets/large.bin").write_bytes(b"corrupt")
        self.run_core(expected=1)
        self.assert_no_writes()

    def test_corrupt_firmware_does_not_write(self):
        (self.sd / "pos-install/firmware.bin").write_bytes(b"bad image")
        self.run_core(expected=1)
        self.assert_no_writes()

    def test_insufficient_space_does_not_write(self):
        result = self.run_core("install", "--free", "1", expected=1)
        self.assertIn("Insufficient storage", result.stderr)
        self.assert_no_writes()

    def test_existing_system_is_never_replaced(self):
        (self.sd / "POS").mkdir()
        (self.sd / "POS/user.txt").write_text("original")
        self.run_core(expected=1)
        self.assertEqual(self.snapshot(self.sd / "POS"), {"user.txt": b"original"})

    def test_unknown_staging_is_never_deleted(self):
        (self.sd / "POS.installing").mkdir()
        (self.sd / "POS.installing/user.txt").write_text("original")
        self.run_core(expected=1)
        self.assertEqual(self.snapshot(self.sd / "POS.installing"), {"user.txt": b"original"})

    def test_restarts_at_durable_boundaries(self):
        for phase in ("validated", "copy", "staged", "sd_committed", "firmware_written", "boot_selected"):
            with self.subTest(phase=phase):
                card = self.base / f"card-{phase}"
                shutil.copytree(self.sd, card)
                original = self.sd
                self.sd = card
                try:
                    self.run_core("install", "--interrupt", phase, expected=99)
                    self.assertEqual(self.snapshot(card / "pos-install"), self.original_package)
                    self.run_core()
                    self.run_core("boot")
                    self.assertTrue((card / "POS/System/.boot-ok").exists())
                finally:
                    self.sd = original

    def test_partial_owned_copy_is_repaired(self):
        self.run_core("install", "--interrupt", "staged", expected=99)
        (self.sd / "POS.installing/System/Assets/large.bin").write_bytes(b"interrupted sector")
        self.run_core()
        self.run_core("boot")

    def test_flash_failure_does_not_select_boot_or_delete_package(self):
        self.run_core("install", "--fail-write", expected=1)
        self.assertFalse((self.sd / ".sim-boot-selected").exists())
        self.assertEqual(self.snapshot(self.sd / "pos-install"), self.original_package)
        self.run_core()
        self.run_core("boot")

    def test_boot_selection_failure_is_retryable(self):
        self.run_core("install", "--fail-select", expected=1)
        self.assertFalse((self.sd / ".sim-boot-selected").exists())
        self.run_core()
        self.run_core("boot")

    def test_bad_system_or_flash_cannot_confirm_or_cleanup(self):
        self.run_core()
        original = (self.sd / "POS/System/Assets/large.bin").read_bytes()
        (self.sd / "POS/System/Assets/large.bin").write_bytes(b"bad storage")
        self.run_core("boot", expected=1)
        (self.sd / "POS/System/Assets/large.bin").write_bytes(original)
        (self.sd / ".sim-ota.bin").write_bytes(b"bad flash")
        self.run_core("boot", expected=1)
        self.assertFalse((self.sd / "POS/System/.boot-ok").exists())
        self.assertEqual(self.snapshot(self.sd / "pos-install"), self.original_package)

    def test_confirmed_install_is_not_reinstalled(self):
        self.run_core()
        self.run_core("boot")
        shutil.rmtree(self.sd / "pos-install", ignore_errors=True)
        (self.sd / "pos-install").mkdir()
        for relative, data in self.original_package.items():
            file = self.sd / "pos-install" / relative
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_bytes(data)
        result = self.run_core(expected=1)
        self.assertIn("Existing POS", result.stderr)

    def test_interrupted_cleanup_resumes_with_missing_known_file(self):
        self.run_core()
        self.run_core("confirm")  # simulate successful platform confirmation before cleanup
        (self.sd / "pos-install/payload/System/empty.txt").unlink()
        self.run_core("boot")
        self.assertFalse((self.sd / "pos-install").exists())

    def test_cleanup_preserves_different_package(self):
        self.run_core()
        self.run_core("confirm")
        self.manifest["version"] = "9.0.0"
        self.save_manifest()
        before = self.snapshot(self.sd / "pos-install")
        self.run_core("cleanup", expected=1)
        self.assertEqual(self.snapshot(self.sd / "pos-install"), before)

    def test_cleanup_checks_all_remaining_files_before_deleting_any(self):
        self.run_core()
        self.run_core("confirm")
        (self.sd / "pos-install/firmware.bin").write_bytes(b"a changed installer")
        before = self.snapshot(self.sd / "pos-install")
        self.run_core("cleanup", expected=1)
        self.assertEqual(self.snapshot(self.sd / "pos-install"), before)

    def test_invalid_manifest_fields_are_rejected(self):
        for field, value in (("format", 2), ("model", "other-board"), ("version", "../escape"),
                             ("files", []), ("files", self.manifest["files"] * 40)):
            with self.subTest(field=field, value=str(value)[:40]):
                candidate = dict(self.manifest)
                candidate[field] = value
                self.manifest_path.write_text(json.dumps(candidate))
                self.run_core(expected=1)
                self.assert_no_writes()

    def test_unsafe_fat_paths_are_rejected_by_both_parsers(self):
        for path in ("../outside", "/absolute", "Users/Stevie/file", "System/../outside", "System/a\\b",
                     "System/CON.txt", "System/AUX", "System/com1.dat", "System/a.", "System/a.part",
                     "System/installed.pos", "System/installed.pos/child", "System/.boot-ok",
                     "System//file", "System/space file", "System/café", "System/" + "a" * 49):
            with self.subTest(path=path):
                self.assertFalse(posctl.safe_path(path))
                self.manifest["files"][0]["path"] = path
                self.save_manifest()
                self.run_core(expected=1)
                self.assert_no_writes()

    def test_case_and_file_directory_collisions_are_rejected(self):
        for other in ("system/assets/LARGE.bin", "System/Assets/large.bin/child", "System/Assets"):
            with self.subTest(path=other):
                self.manifest["files"][1]["path"] = other
                self.save_manifest()
                self.run_core(expected=1)

    def test_numeric_bounds_hash_and_duplicate_keys(self):
        for value in (-1, 0.5, 2**32):
            self.manifest["files"][0]["size"] = value
            self.save_manifest()
            self.run_core(expected=1)
        self.manifest_path.write_text('{"format":1,' + json.dumps(self.manifest)[1:])
        self.run_core(expected=1)
        self.manifest_path.write_text('{"nested":' * 20 + '1' + '}' * 20)
        self.run_core(expected=1)
        self.manifest_path.write_bytes(b"x" * 32769)
        self.run_core(expected=1)

    def test_mismatched_version_or_installer_image_is_rejected(self):
        for version, project in (("9.0.0", "pos_runtime"), ("0.1.0-dev", "pos_installer")):
            data = image(version, project)
            (self.sd / "pos-install/firmware.bin").write_bytes(data)
            self.manifest["firmware"]["sha256"] = hashlib.sha256(data).hexdigest()
            self.save_manifest()
            self.run_core(expected=1)
            self.assert_no_writes()

    def test_packager_refuses_existing_outputs_symlinks_and_wrong_images(self):
        with self.assertRaises(ValueError):
            posctl.make_package(self.firmware, self.payload, self.sd)
        (self.payload / "System/link").symlink_to(self.firmware)
        with self.assertRaises(ValueError):
            posctl.make_package(self.firmware, self.payload, self.base / "new")
        self.firmware.write_bytes(image(project="pos_installer"))
        with self.assertRaises(ValueError):
            posctl.inspect_image(self.firmware)

    def test_packager_rejects_invalid_image_checksum_and_hash(self):
        for offset in (500, -1):
            data = image()
            data[offset] ^= 1
            self.firmware.write_bytes(data)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                posctl.make_package(self.firmware, self.payload, self.base / "bad-image")
        self.assertFalse((self.base / "bad-image").exists())


if __name__ == "__main__":
    unittest.main()
