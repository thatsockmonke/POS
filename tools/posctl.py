#!/usr/bin/env python3
"""POS development tools. Python 3.10+, standard library; esptool comes from ESP-IDF."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zipfile

ROOT = Path(__file__).resolve().parents[1]
MODEL = "m5stack-cardputer-adv"
MAX_FILES = 64
MAX_MANIFEST = 32768
FW_LIMIT = 0x170000
BUFFER = 8192


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(BUFFER):
            digest.update(chunk)
    return digest.hexdigest()


def safe_path(path: str) -> bool:
    parts = path.split("/")
    if not path.startswith("System/") or len(path) >= 160 or len(parts) > 8:
        return False
    for part in parts:
        if not re.fullmatch(r"[A-Za-z0-9_-][A-Za-z0-9_.-]{0,47}", part):
            return False
        stem = part.split(".")[0].upper()
        if part.endswith(".") or part.lower().endswith(".part") or stem in {"CON", "PRN", "AUX", "NUL"}:
            return False
        if re.fullmatch(r"(?:COM|LPT)[1-9]", stem):
            return False
    lower = path.lower()
    if lower in {"system/apps", "system/assets", "system/fonts", "system/sounds", "system/config"}:
        return False
    return not any(lower == name or lower.startswith(name + "/") for name in
                   ("system/installed.pos", "system/.boot-ok"))


def inspect_image(path: Path, project: str = "pos_runtime", limit: int = FW_LIMIT) -> str:
    """Check ESP32-S3 header and the standard ESP-IDF app descriptor."""
    if path.is_symlink() or not path.is_file() or not 288 <= path.stat().st_size <= limit:
        raise ValueError("Runtime firmware must be a regular image that fits an OTA slot")
    with path.open("rb") as stream:
        header = stream.read(288)
    if header[0] != 0xE9 or struct.unpack_from("<H", header, 12)[0] != 9:
        raise ValueError("Firmware is not an ESP32-S3 image")
    if struct.unpack_from("<I", header, 28)[0] < 256 or struct.unpack_from("<I", header, 32)[0] != 0xABCD5432:
        raise ValueError("Missing ESP-IDF application descriptor")

    def string(start: int) -> str:
        value = header[start:start + 32]
        if b"\0" not in value:
            raise ValueError("Unterminated image descriptor")
        return value.split(b"\0", 1)[0].decode("ascii")

    version = string(48)
    if string(80) != project or not re.fullmatch(r"[0-9][A-Za-z0-9._-]{0,30}", version):
        raise ValueError(f"Expected a versioned {project} image")
    return version


def verify_image(path: Path) -> None:
    # image_info prints "invalid" but still returns success in esptool 4.12.
    # Compare parsed values explicitly; a CLI exit code alone is insufficient.
    try:
        from esptool.bin_image import LoadFirmwareImage
        from esptool.util import FatalError
    except ImportError as exc:
        raise ValueError("Activate the ESP-IDF Python environment to use its esptool") from exc
    try:
        parsed = LoadFirmwareImage("esp32s3", str(path))
    except (FatalError, ValueError, struct.error, IndexError) as exc:
        raise ValueError(f"Cannot parse firmware image: {exc}") from exc
    if (parsed.chip_id != 9 or parsed.checksum != parsed.calculate_checksum() or
            not parsed.append_digest or parsed.stored_digest != parsed.calc_digest or
            parsed.data_length + 32 != path.stat().st_size):
        raise ValueError("Firmware checksum, appended SHA-256, length or chip is invalid")


def make_bundle(build_root: Path, payload: Path, output: Path) -> Path:
    if output.exists() or output.is_symlink():
        raise ValueError("Bundle output already exists; choose a new directory")
    installer = build_root / "installer"
    runtime = build_root / "runtime"
    version = inspect_image(installer / "pos_installer.bin", "pos_installer", 0x100000)
    if inspect_image(runtime / "pos_runtime.bin") != version:
        raise ValueError("Installer and runtime versions must match")
    verify_image(installer / "pos_installer.bin")
    verify_image(installer / "bootloader/bootloader.bin")
    if (installer / "partition_table/partition-table.bin").read_bytes() != (runtime / "partition_table/partition-table.bin").read_bytes():
        raise ValueError("Installer and runtime partition tables differ")
    arguments = json.loads((installer / "flasher_args.json").read_text())
    expected = {"0x0": "bootloader/bootloader.bin", "0x8000": "partition_table/partition-table.bin",
                "0xd000": "ota_data_initial.bin", "0x10000": "pos_installer.bin"}
    if arguments["flash_files"] != expected or arguments["flash_settings"]["flash_size"] != "4MB":
        raise ValueError("Unexpected installer flash layout")
    output.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix=".pos-bundle-", dir=output.parent))
    try:
        make_package(runtime / "pos_runtime.bin", payload, stage / "card")
        merged = stage / "pos-installer-usb.bin"
        merge = [sys.executable, "-m", "esptool", "--chip", "esp32s3", "merge_bin", "-o", str(merged),
                 *arguments["write_flash_args"]]
        for address, relative in sorted(expected.items(), key=lambda entry: int(entry[0], 16)):
            merge.extend([address, str((installer / relative).absolute())])
        subprocess.run(merge, check=True)
        with zipfile.ZipFile(stage / "pos-install.zip", "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for path in sorted((stage / "card").rglob("*")):
                if path.is_file():
                    archive.write(path, path.relative_to(stage / "card"))
        shutil.copyfile(ROOT / "docs/INSTALL.md", stage / "INSTALL.md")
        for name in ("RECOVERY.md", "HARDWARE.md", "HARDWARE_VALIDATION.md", "FORMAT.md", "VALIDATION.md"):
            shutil.copyfile(ROOT / "docs" / name, stage / name)
        for name in ("STATUS.md", "DOWNLOADS.md", "APPS.md", "PROTOCOLS.md", "UPDATES.md", "DEVELOPMENT.md", "MIGRATIONS.md", "EASY_INSTALL.md", "FIRMWARE_HISTORY.md", "BUG_REPORT_0.6.2.md", "BUG_REPORT_0.6.5.md", "MAPS.md"):
            shutil.copyfile(ROOT / "docs" / name, stage / name)
        # USB commands work with pip-installed esptool; no SDK/build is required.
        (stage / "tools").mkdir()
        for name in ("posctl.py", "pos_validation.py", "pos_updates.py", "pos_deploy.py", "pos_setup.py", "pos_setup_storage.py", "build_setup.py", "pos_release_updates.py", "pos_release_assets.py"):
            shutil.copyfile(ROOT / "tools" / name, stage / "tools" / name)
        (stage / "updates").mkdir()
        for name in ("public-key.pem", "publisher.json"):
            shutil.copyfile(ROOT / "updates" / name, stage / "updates" / name)
        from build_setup import build as build_setup
        build_setup(stage / "pos-setup")
        shutil.copyfile(ROOT / "THIRD_PARTY.md", stage / "THIRD_PARTY.md")
        sdk = Path(os.environ.get("IDF_PATH", "/workspace/pos-cloud/esp-idf-v5.5.5"))
        sdk_license = sdk / "LICENSE"
        if sdk_license.is_file():
            shutil.copyfile(sdk_license, stage / "ESP-IDF-LICENSE.txt")
        licenses = stage / "licenses"
        licenses.mkdir()
        shutil.copyfile(ROOT / "components/pos_core/vendor/minimp3/LICENSE", licenses / "minimp3-CC0.txt")
        shutil.copyfile(ROOT / "components/pos_device/vendor/sx126x/LICENSE.txt", licenses / "Semtech-Clear-BSD.txt")
        shutil.copyfile(ROOT / "components/pos_core/vendor/bmi270/LICENSE", licenses / "M5Stack-MIT.txt")
        nimble = sdk / "components/bt/host/nimble/nimble"
        for source, destination in ((nimble / "LICENSE", "NimBLE-LICENSE.txt"), (nimble / "NOTICE", "NimBLE-NOTICE.txt"),
                                    (nimble / "ext/tinycrypt/LICENSE", "TinyCrypt-LICENSE.txt")):
            if source.is_file():
                shutil.copyfile(source, licenses / destination)
        sums = []
        for path in sorted(stage.rglob("*")):
            if path.is_file():
                sums.append(f"{sha256(path)}  {path.relative_to(stage).as_posix()}\n")
        (stage / "SHA256SUMS").write_text("".join(sums))
        stage.rename(output)
    except BaseException:
        shutil.rmtree(stage)
        raise
    return output


def archive_bundle(bundle: Path, output: Path) -> Path:
    """Export one downloadable ZIP containing the entire ready-to-install bundle."""
    part = Path(str(output) + ".part")
    checksum = Path(str(output) + ".sha256")
    if output.exists() or output.is_symlink() or part.exists() or checksum.exists():
        raise ValueError("Download archive, partial or checksum exists; choose a fresh output")
    if output.resolve().is_relative_to(bundle.resolve()):
        raise ValueError("Download archive must be outside its input bundle")
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(part, "x", compression=zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(bundle.rglob("*")):
            if path.is_symlink():
                raise ValueError("Bundle symlinks cannot be exported")
            if path.is_file():
                archive.write(path, path.relative_to(bundle))
    with zipfile.ZipFile(part) as archive:
        if archive.testzip() is not None:
            raise ValueError("Download archive failed its CRC check; partial retained")
    digest = sha256(part)
    os.link(part, output)
    part.unlink()
    with checksum.open("x") as stream:
        stream.write(f"{digest}  {output.name}\n")
    return output


def make_package(firmware: Path, payload: Path, output: Path) -> Path:
    firmware, payload, output = firmware.absolute(), payload.absolute(), output.absolute()
    if output.exists() or output.is_symlink():
        raise ValueError("Output already exists; choose a new directory (nothing will be overwritten)")
    if payload.is_symlink() or not payload.is_dir():
        raise ValueError("Payload must be a directory without symlinks")
    # Refuse an output beneath the input: it would package its own temporary files.
    if output.resolve().is_relative_to(payload.resolve()):
        raise ValueError("Output must be outside the payload directory")
    version = inspect_image(firmware)
    verify_image(firmware)
    files: list[tuple[str, Path]] = []
    for directory, dirs, names in os.walk(payload, followlinks=False):
        for name in dirs + names:
            if (Path(directory) / name).is_symlink():
                raise ValueError("Payload symlinks are not supported")
        for name in names:
            source = Path(directory) / name
            relative = source.relative_to(payload).as_posix()
            if not source.is_file() or not safe_path(relative) or source.stat().st_size > 0xFFFFFFFF:
                raise ValueError(f"Unsafe or unsupported payload file: {relative}")
            files.append((relative, source))
    if not 1 <= len(files) <= MAX_FILES:
        raise ValueError(f"Package must contain 1..{MAX_FILES} System files")
    files.sort()
    paths = [relative.lower() for relative, _ in files]
    for i, path in enumerate(paths):
        for other in paths[:i]:
            if path == other or path.startswith(other + "/") or other.startswith(path + "/"):
                raise ValueError("Payload has paths that collide on FAT")
    output.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix=".pos-package-", dir=output.parent))
    try:
        package = stage / "pos-install"
        package.mkdir()
        shutil.copyfile(firmware, package / "firmware.bin")
        if sha256(firmware) != sha256(package / "firmware.bin"):
            raise ValueError("Firmware changed during packaging")
        records = []
        for relative, source in files:
            dest = package / "payload" / relative
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, dest)
            digest = sha256(dest)
            if digest != sha256(source):
                raise ValueError(f"Payload changed during packaging: {relative}")
            records.append({"path": relative, "size": dest.stat().st_size, "sha256": digest})
        manifest = {"format": 1, "model": MODEL, "version": version,
                    "firmware": {"path": "firmware.bin", "size": (package / "firmware.bin").stat().st_size,
                                 "sha256": sha256(package / "firmware.bin")}, "files": records}
        data = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode()
        if len(data) > MAX_MANIFEST:
            raise ValueError("Manifest exceeds firmware parser limit")
        (package / "manifest.pos").write_bytes(data)
        # Recheck the actual copy, including image integrity, before exposing the package.
        if inspect_image(package / "firmware.bin") != version:
            raise ValueError("Firmware descriptor changed during packaging")
        verify_image(package / "firmware.bin")
        stage.rename(output)
    except BaseException:
        shutil.rmtree(stage)
        raise
    return output / "pos-install"


def build(role: str, build_root: Path) -> None:
    if not os.environ.get("IDF_PATH") or not shutil.which("idf.py"):
        raise ValueError("Activate ESP-IDF first: . /workspace/pos-cloud/activate.sh (cloud), or source your IDF export.sh")
    for selected in ("installer", "runtime") if role == "all" else (role,):
        directory = build_root.absolute() / selected
        config = directory.parent / f"{selected}.sdkconfig"
        command = ["idf.py", "-C", str(ROOT), "-B", str(directory),
                   "-D", f"POS_ROLE={selected}", "-D", f"SDKCONFIG={config}", "-D", "IDF_TARGET=esp32s3"]
        subprocess.run(command + ["reconfigure"], check=True)
        subprocess.run(["ninja", "-C", str(directory), "-j", "4"], check=True)
        subprocess.run(["idf.py", "-C", str(ROOT), "-B", str(directory), "size"], check=True)


def native_helper() -> Path:
    sdk = Path(os.environ.get("IDF_PATH", "/workspace/pos-cloud/esp-idf-v5.5.5"))
    cjson = sdk / "components/json/cJSON"
    if not (cjson / "cJSON.c").is_file():
        raise ValueError("Activate the pinned ESP-IDF SDK; native services use its cJSON")
    sources = [ROOT / "tools/pos_host.c", ROOT / "components/pos_install/pos_install.c",
               ROOT / "components/pos_install/pos_hash.c", cjson / "cJSON.c"]
    headers = []
    for component in ("pos_core", "pos_recovery"):
        sources.extend(sorted((ROOT / "components" / component).glob("*.c")))
    for component in ("pos_install", "pos_core", "pos_recovery"):
        headers.extend((ROOT / "components" / component).rglob("*.h"))
    target = ROOT / "build/host/pos_host"
    if not target.exists() or any(p.stat().st_mtime_ns > target.stat().st_mtime_ns for p in sources + headers):
        target.parent.mkdir(parents=True, exist_ok=True)
        command = ["gcc", "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-O2", "-Wall", "-Wextra", "-Werror"]
        for component in ("pos_install", "pos_core", "pos_recovery"):
            command.extend(["-I", str(ROOT / "components" / component / "include")])
        subprocess.run(command + ["-I", str(cjson), *map(str, sources), "-lcrypto", "-lm", "-o", str(target)], check=True)
    return target


def preview(output: Path, screen: str = "launcher", animate: bool = False) -> None:
    if animate and screen not in ("boot", "ui"):
        raise ValueError("Animated preview is available for boot or ui")
    if output.exists():
        raise ValueError("Preview output exists; choose a fresh filename")
    output.parent.mkdir(parents=True, exist_ok=True)
    import zlib

    def chunk(kind: bytes, contents: bytes) -> bytes:
        return (struct.pack(">I", len(contents)) + kind + contents +
                struct.pack(">I", zlib.crc32(kind + contents)))

    def pixels(path: Path) -> bytes:
        data = path.read_bytes()
        prefix = b"P6\n240 135\n255\n"
        if not data.startswith(prefix) or len(data) != len(prefix) + 240 * 135 * 3:
            raise ValueError("Native renderer returned an invalid image")
        rgb = data[len(prefix):]
        scanlines = b"".join(b"\0" + rgb[y*720:(y+1)*720] for y in range(135))
        return zlib.compress(scanlines)

    with tempfile.TemporaryDirectory(prefix="pos-preview-") as temp:
        directory = Path(temp)
        helper = str(native_helper())
        payload = str(ROOT / "sd-payload")
        png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 240, 135, 8, 2, 0, 0, 0))
        if animate:
            result = subprocess.run([helper, payload, "boot-frames" if screen=="boot" else "ui-frames", temp],
                                    check=True, capture_output=True, text=True)
            holds = [int(value) for value in result.stdout.splitlines()]
            if not holds or any(not 0 < value <= 65535 for value in holds):
                raise ValueError("Native renderer returned invalid frame timings")
            png += chunk(b"acTL", struct.pack(">II", len(holds), 0))
            sequence = 0
            for frame, hold in enumerate(holds):
                png += chunk(b"fcTL", struct.pack(">IIIIIHHBB", sequence, 240, 135,
                                                    0, 0, hold, 1000, 0, 0))
                sequence += 1
                data = pixels(directory / f"{frame}.ppm")
                if frame == 0:
                    png += chunk(b"IDAT", data)
                else:
                    png += chunk(b"fdAT", struct.pack(">I", sequence) + data)
                    sequence += 1
        else:
            ppm = directory / "preview.ppm"
            subprocess.run([helper, payload, {"boot": "boot-preview", "picker": "color-preview", "launcher": "preview", "recorder":"recorder-preview", "files":"files-preview", "background":"background-preview", "counter":"counter-preview", "ui":"preview", "maps":"maps-preview", "world":"world-preview"}[screen],
                            str(ppm)], check=True)
            png += chunk(b"IDAT", pixels(ppm))
        png += chunk(b"IEND", b"")
        with output.open("xb") as stream:
            stream.write(png)


class USBCommandError(ValueError):
    """Esptool failed; distinguish transport/tool errors from local file errors."""


def hardware_command(port: str, *arguments: str) -> str:
    result = subprocess.run([sys.executable, "-m", "esptool", "--chip", "esp32s3", "--port", port,
                             *arguments], capture_output=True, text=True)
    if result.returncode:
        raise USBCommandError(f"USB operation failed: {' '.join(arguments)}\n{(result.stdout + result.stderr)[-2500:]}")
    return result.stdout


def hardware_probe(port: str, *, rom=False) -> dict:
    # During backup, keep the original application stopped: booting it between
    # reads could change NVS and make a correct pair of reads disagree.
    options = ("--no-stub", "--after", "no_reset") if rom else ()
    results = {command: hardware_command(port, *options, command) for command in ("chip_id", "flash_id", "get_security_info")}
    match = re.search(r"\bMAC:\s*([0-9a-fA-F:]{17})", results["chip_id"])
    security=results["get_security_info"]
    plain=bool(re.search(r"(?m)^Secure Boot: Disabled\s*$",security) and
               re.search(r"(?m)^Flash Encryption: Disabled\s*$",security))
    capacity = re.search(r"(?mi)^Detected flash size:\s*(\d+)([KM])B\s*$", results["flash_id"])
    flash_bytes = int(capacity[1]) * (1024 if capacity[2].upper() == "K" else 1024*1024) if capacity else None
    return {"format": 1, "chip": "esp32s3", "device_id": match.group(1).lower() if match else None,
            "flash_bytes": flash_bytes,
            "plain_development_flash": plain,
            "observations": results}


def factory_backup(port: str, output: Path) -> None:
    output = output.absolute()
    part, readback, metadata = [Path(str(output) + suffix) for suffix in (".part", ".readback.part", ".json")]
    failed = [Path(str(p) + ".stub-failed") for p in (part, readback)]
    if any(p.exists() or p.is_symlink() for p in (output, part, readback, metadata, *failed)):
        raise ValueError("Factory backup output or partial files exist; preserve them and choose a new filename")
    report = hardware_probe(port, rom=True)
    size = report.get("flash_bytes")
    if (not report.get("device_id") or not report.get("plain_development_flash") or
            type(size) is not int or not 4*1024*1024 <= size <= 128*1024*1024 or size & (size-1)):
        raise ValueError("Cannot verify device identity, security state or full flash capacity; no backup/write attempted")

    def check_device():
        current = hardware_probe(port, rom=True)
        if any(current.get(key) != report.get(key) for key in ("device_id", "flash_bytes", "plain_development_flash")):
            raise ValueError("Connected device identity, security or flash capacity changed; incomplete backup retained, no write attempted")

    output.parent.mkdir(parents=True, exist_ok=True)
    methods = []
    rom = False
    for number, destination in enumerate((part, readback), 1):
        with destination.open("xb"):
            pass
        destination.chmod(0o600)

        def read():
            print(f"Backup read {number}/2: {size//(1024*1024)} MiB, " +
                  ("ROM compatibility mode (slower; allow several minutes)." if rom else "fast USB mode."), flush=True)
            options = ("--no-stub",) if rom else ()
            hardware_command(port, *options, "--after", "no_reset", "read_flash", "0", str(size), str(destination))

        try:
            read()
        except USBCommandError as error:
            if rom:
                raise USBCommandError(f"ROM backup read failed; no firmware was written. Incomplete files are not verified backups.\n{error}") from error
            print("Fast USB backup read failed. Keeping its incomplete file and restarting this read from byte zero\n"
                  "using the ROM's smaller request/response transfers. No firmware has been written.", flush=True)
            destination.rename(failed[number-1])
            check_device()
            with destination.open("xb"):
                pass
            destination.chmod(0o600)
            rom = True
            try:
                read()
            except USBCommandError as retry_error:
                raise USBCommandError(f"ROM backup read also failed; no firmware was written. Incomplete files are not verified backups.\n{retry_error}") from retry_error
        if destination.stat().st_size != size:
            raise ValueError("Backup read did not contain the detected full flash capacity; partial files retained, no write attempted")
        check_device()
        methods.append("rom" if rom else "stub")
    if sha256(part) != sha256(readback):
        raise ValueError("Factory reads do not agree; partial files retained, no write attempted")
    report.update({"bytes": size, "sha256": sha256(part), "read_methods": methods, "readback_verified": True,
                   "recovery_restore_verified": False})
    part.rename(output)
    with metadata.open("x") as stream:
        json.dump(report, stream, indent=2)
        stream.write("\n")
    metadata.chmod(0o600)
    readback.unlink()


def hardware_write_verified(port: str, image: Path) -> None:
    # The merged image includes NVS/OTA data. Never boot it before comparing:
    # its first boot legitimately writes those regions (e.g. health counters).
    hardware_command(port, "--after", "no_reset", "write_flash", "0x0", str(image.absolute()))
    hardware_command(port, "--after", "no_reset", "verify_flash", "0x0", str(image.absolute()))
    # A read-only command with an explicit post-command hard reset starts the
    # app only after verification; do not depend on ESPTOOL_AFTER in the shell.
    try:
        hardware_command(port, "--after", "hard_reset", "read_mac")
    except USBCommandError as error:
        raise USBCommandError("Flash readback verified, but automatic restart failed. Power-cycle the Cardputer.\n" + str(error)) from error


def factory_restore(port: str, backup: Path, confirmed: bool) -> None:
    if not confirmed:
        raise ValueError("Restoring replaces internal flash. Supply --confirm-restore only after reviewing the matching backup")
    metadata = Path(str(backup) + ".json")
    report = json.loads(metadata.read_text())
    if (report.get("format") != 1 or report.get("chip") != "esp32s3" or not report.get("readback_verified") or
            not report.get("device_id") or backup.stat().st_size != report.get("bytes") or sha256(backup) != report.get("sha256")):
        raise ValueError("Backup metadata/size/hash is invalid or device identity is missing; no write attempted")
    current = hardware_probe(port)
    if current["device_id"] != report["device_id"]:
        raise ValueError("Backup belongs to a different device; no write attempted")
    if not current.get("plain_development_flash") or not report.get("plain_development_flash"):
        raise ValueError("This restore tool supports unencrypted development flash with Secure Boot disabled; use the vendor's recovery for other configurations")
    hardware_write_verified(port, backup)
    # Record evidence separately; never rewrite or delete the known-good backup.
    evidence = Path(str(backup) + f".restore-verified-{time.time_ns()}.json")
    with evidence.open("x") as stream:
        json.dump({"format": 1, "device_id": report["device_id"], "sha256": report["sha256"],
                   "flash_readback_verified": True, "factory_boot_observed": False}, stream, indent=2)
    evidence.chmod(0o600)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    p = commands.add_parser("flash", help="Flash/readback a checksum-verified USB installer with a matching factory backup")
    p.add_argument("--port", required=True)
    p.add_argument("--image", type=Path, required=True)
    p.add_argument("--sha256", required=True)
    p.add_argument("--factory-backup", type=Path, required=True)
    p.add_argument("--confirm-flash", action="store_true")
    p = commands.add_parser("files", help="Transfer user files through an explicit trusted-LAN session")
    p.add_argument("action", choices=["status", "push", "pull"])
    p.add_argument("--device", required=True)
    p.add_argument("--key-file", type=Path, required=True)
    p.add_argument("--source", type=Path)
    p.add_argument("--name")
    p.add_argument("--output", type=Path)
    p = commands.add_parser("deploy", help="Request a signed HTTPS update after verified physical recovery/OTA evidence")
    p.add_argument("--device", required=True)
    p.add_argument("--key-file", type=Path, required=True)
    p.add_argument("--update-dir", type=Path, required=True)
    p.add_argument("--manifest-url", required=True)
    p.add_argument("--public-key", type=Path, required=True)
    p.add_argument("--validation-report", type=Path, required=True)
    p.add_argument("--evidence-root", type=Path, required=True)
    p.add_argument("--wait", type=int, default=300)
    p = commands.add_parser("update-keygen", help="Create new user-owned P-256 signing keys; never overwrite keys")
    p.add_argument("--private-key",type=Path,required=True)
    p.add_argument("--public-key",type=Path,required=True)
    p = commands.add_parser("update-package",help="Create a signed HTTPS update directory")
    p.add_argument("--kind",choices=["firmware","assets"],required=True)
    p.add_argument("--firmware",type=Path)
    p.add_argument("--installed-manifest",type=Path)
    p.add_argument("--payload",type=Path,required=True)
    p.add_argument("--release",required=True)
    p.add_argument("--sequence",type=int,required=True)
    p.add_argument("--base-url",required=True)
    p.add_argument("--private-key",type=Path,required=True)
    p.add_argument("--output",type=Path,required=True)
    p = commands.add_parser("build", help="Build firmware with the activated ESP-IDF")
    p.add_argument("--role", choices=["installer", "runtime", "all"], default="all")
    p.add_argument("--build-root", type=Path, default=ROOT / "build")
    p = commands.add_parser("package", help="Create a new SD install tree; never overwrite an existing output")
    p.add_argument("--firmware", type=Path, default=ROOT / "build/runtime/pos_runtime.bin")
    p.add_argument("--payload", type=Path, default=ROOT / "sd-payload")
    p.add_argument("--output", type=Path, required=True)
    p = commands.add_parser("preview", help="Render the actual portable launcher primitives to PNG")
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--screen", choices=["launcher", "boot", "picker", "recorder", "files", "background", "counter", "ui", "maps", "world"], default="launcher")
    p.add_argument("--animate", action="store_true", help="Export the boot sequence as an animated PNG")
    p = commands.add_parser("recovery", help="Run POS's portable backup/restore services on a Linux SD mount")
    p.add_argument("action", choices=["scan", "backup", "backup-set", "resume-backup", "verify", "restore"])
    p.add_argument("--root", type=Path, required=True)
    p.add_argument("--archive", type=Path)
    p.add_argument("--confirm-restore", action="store_true")
    p = commands.add_parser("probe", help="Read chip, flash and security information over USB; does not flash")
    p.add_argument("--port", required=True)
    p = commands.add_parser("factory-backup", help="Read full flash twice and preserve a verified private backup")
    p.add_argument("--port", required=True)
    p.add_argument("--output", type=Path, required=True)
    p = commands.add_parser("factory-restore", help="Restore a matching verified backup and verify flash readback")
    p.add_argument("--port", required=True)
    p.add_argument("--backup", type=Path, required=True)
    p.add_argument("--confirm-restore", action="store_true")
    p = commands.add_parser("logs", help="Capture USB serial evidence without deliberate reset or flashing")
    p.add_argument("--port", required=True)
    p.add_argument("--seconds", type=float, default=30)
    p.add_argument("--output", type=Path, required=True)
    p = commands.add_parser("validation-template", help="Create an unrun hardware observation checklist")
    p.add_argument("--output", type=Path, required=True)
    p = commands.add_parser("hardware-report", help="Collect SD diagnostics and explicitly recorded physical observations")
    p.add_argument("--root", type=Path, required=True)
    p.add_argument("--observations", type=Path)
    p.add_argument("--output", type=Path, required=True)
    p = commands.add_parser("bundle", help="Create a USB installer image plus zipped SD package")
    p.add_argument("--build-root", type=Path, default=ROOT / "build")
    p.add_argument("--payload", type=Path, default=ROOT / "sd-payload")
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--zip-output", type=Path, help="Also export a complete downloadable ZIP and SHA-256 sidecar")
    args = parser.parse_args()
    try:
        if args.command in ("flash", "files", "deploy"):
            import pos_deploy
            api = sys.modules[__name__]
            if args.command == "flash":
                pos_deploy.flash(args, api)
                print("USB installer write and readback verified. Observe the device and complete SD installation.")
            elif args.command == "deploy":
                print(json.dumps(pos_deploy.deploy(args, api), indent=2))
            elif args.action == "status":
                print(json.dumps(pos_deploy.status(args.device, args.key_file), indent=2))
            elif args.action == "push":
                if not args.source:
                    raise ValueError("files push requires --source")
                pos_deploy.push(args.device, args.key_file, args.source, args.name or args.source.name, api)
                print("User file streamed and SHA-256 verified in Downloads.")
            else:
                if not args.output:
                    raise ValueError("files pull requires --output")
                pos_deploy.pull(args.device, args.key_file, args.output, api)
                print("Selected user export streamed and SHA-256 verified.")
        elif args.command in ("update-keygen","update-package"):
            from pos_updates import keygen,make_update
            if args.command=="update-keygen":
                print("Public-key SHA-256 (DER SPKI): "+keygen(args.private_key,args.public_key))
            else:
                print("Signed update directory: "+str(make_update(args,sys.modules[__name__])))
        elif args.command == "build":
            build(args.role, args.build_root)
        elif args.command == "package":
            package = make_package(args.firmware, args.payload, args.output)
            print(f"Verified development package: {package}\nCopy pos-install to the FAT32 card root; firmware is UNSIGNED.")
        elif args.command == "bundle":
            bundle = make_bundle(args.build_root.absolute(), args.payload, args.output.absolute())
            print(f"Development installer bundle: {bundle}")
            if args.zip_output:
                print(f"Complete download: {archive_bundle(bundle, args.zip_output.absolute())}")
        elif args.command == "preview":
            preview(args.output,args.screen,args.animate)
        elif args.command == "recovery":
            if args.action == "restore" and not args.confirm_restore:
                raise ValueError("Review the archive and destination, then supply --confirm-restore; conflicts are preserved")
            if args.action in ("backup", "backup-set", "resume-backup") and args.archive:
                raise ValueError("Backup selects ZIP or a plain folder at the root; existing backups are preserved")
            command = [str(native_helper()), str(args.root.resolve()), args.action]
            if args.archive:
                command.append(str(args.archive.resolve()))
            subprocess.run(command, check=True)
        elif args.command in ("logs", "validation-template", "hardware-report"):
            from pos_validation import serial_logs, template, hardware_report
            if args.command == "logs":
                serial_logs(args.port,args.seconds,args.output)
            elif args.command == "validation-template":
                template(args.output)
            else:
                report=hardware_report(args.root,args.observations,args.output)
                print(f"Saved {len(report['measurements'])} measurements; physical validation complete: {report['physical_validation_complete']}")
        elif args.command == "probe":
            print(json.dumps(hardware_probe(args.port), indent=2))
        elif args.command == "factory-backup":
            factory_backup(args.port, args.output)
            print("Factory backup read twice and verified. Restoring and observing factory boot are still required.")
        elif args.command == "factory-restore":
            factory_restore(args.port, args.backup, args.confirm_restore)
            print("Factory flash readback verified. Observe the factory application's boot on the physical device.")
    except (ValueError, OSError, subprocess.CalledProcessError, struct.error) as exc:
        print(f"posctl: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
