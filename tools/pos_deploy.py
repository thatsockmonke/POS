"""Explicit USB installation and trusted-LAN developer sessions. No discovery daemon."""
from __future__ import annotations

import hashlib
import http.client
import ipaddress
import json
import os
from pathlib import Path
import re
import ssl
import subprocess
import tempfile
import time
from urllib.parse import quote, urlsplit
from urllib.request import HTTPSHandler, HTTPRedirectHandler, build_opener

REQUIRED = ("revision", "usb_download", "factory_backup", "factory_restore_boot",
            "sd_streaming", "cut_sd_commit", "cut_firmware_write", "cut_boot_select",
            "cut_boot_confirm", "cut_restore_migrate", "cut_cleanup", "ota_update", "ota_rollback")


def valid_https(value: str) -> bool:
    if not isinstance(value, str) or len(value) > 511 or any(ord(c) <= 32 or ord(c) >= 127 for c in value):
        return False
    try:
        parsed = urlsplit(value)
        return (parsed.scheme == "https" and bool(parsed.hostname) and not parsed.username and
                not parsed.password and not parsed.fragment and (parsed.port is None or 1 <= parsed.port <= 65535))
    except ValueError:
        return False


def key_file(path: Path) -> str:
    if path.is_symlink() or not path.is_file() or path.stat().st_size > 64:
        raise ValueError("Session key must be a regular private file containing 16 hex digits")
    if os.name == "posix" and path.stat().st_mode & 0o077:
        raise ValueError("Session key file must be private: chmod 600 it")
    key = path.read_text().strip()
    if not re.fullmatch("[0-9a-f]{16}", key):
        raise ValueError("Invalid session key file")
    return key


def connection(device: str):
    u = urlsplit(device)
    if (u.scheme != "http" or not u.hostname or u.username or u.password or
            u.path not in ("", "/") or u.query or u.fragment or u.port != 8080):
        raise ValueError("Use the device's displayed http://LAN-IP:8080 address without a session key")
    try:
        address = ipaddress.ip_address(u.hostname)
        private = address.is_private and not address.is_unspecified and not address.is_multicast
    except ValueError:
        private = u.hostname.endswith(".local")
    if not private:
        raise ValueError("Development sessions require a private LAN address or .local hostname")
    return http.client.HTTPConnection(u.hostname, u.port, timeout=10)


def request(device: str, key: Path, method: str, route: str, body: bytes | None = None) -> bytes:
    c = connection(device)
    try:
        c.request(method, route, body, {"Authorization": "Bearer " + key_file(key),
                                     "Content-Type": "text/plain"})
        response = c.getresponse()
        data = response.read(32769)
        if len(data) > 32768:
            raise ValueError("Unexpectedly large device response")
        if response.status not in (200, 202):
            raise ValueError(f"Device rejected request (HTTP {response.status}); check its screen and session")
        return data
    finally:
        c.close()


def status(device: str, key: Path) -> dict:
    value = json.loads(request(device, key, "GET", "/status"))
    if (not isinstance(value, dict) or value.get("format") != 1 or
            value.get("model") != "m5stack-cardputer-adv" or
            not re.fullmatch(r"(?:[0-9a-f]{2}:){5}[0-9a-f]{2}", value.get("device_id", "")) or
            not re.fullmatch("[0-9a-f]{64}", value.get("build_id", ""))):
        raise ValueError("Unsupported device status")
    return value


def readiness(report_path: Path, evidence_root: Path, target: dict, digest) -> None:
    if report_path.is_symlink() or report_path.stat().st_size > 2 * 1024 * 1024:
        raise ValueError("Invalid physical validation report")
    report = json.loads(report_path.read_text())
    if (report.get("format") != 1 or report.get("device_id") != target["device_id"] or
            report.get("validated_build_id") != target["build_id"]):
        raise ValueError("Recovery/OTA evidence must match this device and running build")
    root = evidence_root.resolve(strict=True)
    for name in REQUIRED:
        entry = report.get("checks", {}).get(name, {})
        if (entry.get("result") != "pass" or entry.get("basis") != "operator observation" or
                not entry.get("note", "").strip() or not entry.get("evidence")):
            raise ValueError(f"Physical deployment prerequisite not passed: {name}")
        for item in entry["evidence"]:
            path = root / item["path"]
            resolved = path.resolve(strict=True)
            if (not resolved.is_relative_to(root) or path.is_symlink() or not resolved.is_file() or
                    resolved.stat().st_size != item["bytes"] or digest(resolved) != item["sha256"]):
                raise ValueError(f"Missing or changed physical evidence: {name}")


def push(device: str, key: Path, source: Path, name: str, api) -> None:
    if (source.is_symlink() or not source.is_file() or source.stat().st_size > 0x7fffffff or
            not name or len(name) > 120 or any(c in name for c in '/\\:?*<>|"') or
            name in (".", "..") or name.endswith((".", " ")) or any(ord(c) < 32 for c in name)):
        raise ValueError("Choose a regular file below 2 GiB and a simple destination filename")
    before = api.sha256(source)
    size = source.stat().st_size
    c = connection(device)
    try:
        c.putrequest("POST", "/upload?name=" + quote(name, safe=""))
        c.putheader("Authorization", "Bearer " + key_file(key))
        c.putheader("Content-Length", str(size))
        c.putheader("X-POS-SHA256", before)
        c.endheaders()
        with source.open("rb") as f:
            remaining = size
            while remaining:
                chunk = f.read(min(65536, remaining))
                if not chunk:
                    raise ValueError("Upload source changed; inspect the device partial before retrying")
                c.send(chunk); remaining -= len(chunk)
            if f.read(1):
                raise ValueError("Upload source grew during transfer; result not confirmed")
        r = c.getresponse()
        r.read(32769)
        if r.status != 200 or api.sha256(source) != before:
            raise ValueError("Upload not verified; inspect Downloads/partials on device before retrying")
    finally:
        c.close()


def pull(device: str, key: Path, output: Path, api) -> None:
    part = Path(str(output) + ".part")
    if output.exists() or output.is_symlink() or part.exists() or part.is_symlink():
        raise ValueError("Output or partial exists; choose a fresh filename")
    c = connection(device)
    try:
        c.request("GET", "/download", headers={"Authorization": "Bearer " + key_file(key)})
        r = c.getresponse()
        expected = r.getheader("X-POS-SHA256", "")
        if r.status != 200 or not re.fullmatch("[0-9a-f]{64}", expected):
            raise ValueError("Select an export file in Files > Share > Wi-Fi first")
        with part.open("xb") as f:
            os.chmod(part, 0o600)
            size = 0
            while chunk := r.read(65536):
                size += len(chunk)
                if size > 0x7fffffff:
                    raise ValueError("Export exceeds 2 GiB; partial retained")
                f.write(chunk)
            f.flush(); os.fsync(f.fileno())
        if api.sha256(part) != expected:
            raise ValueError("Download hash mismatch; partial retained")
        os.link(part, output); part.unlink()
    finally:
        c.close()


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        raise ValueError("Publish the update at its final HTTPS URL; redirects are refused")


def verify_update_artifact(directory: Path, public_key: Path, api):
    """Authenticate an immutable historical artifact and verify every resource."""
    from pos_updates import validate_package, version
    path = directory / "manifest.json"
    if path.is_symlink() or path.stat().st_size > 32768:
        raise ValueError("Invalid signed manifest")
    raw = path.read_bytes()
    envelope = json.loads(raw)
    if set(envelope) != {"payload", "signature"} or not isinstance(envelope["payload"], str):
        raise ValueError("Unsupported signed envelope")
    payload = json.loads(envelope["payload"])
    fields = {"format", "model", "release", "sequence", "kind", "base_url", "package"}
    if (not fields.issubset(payload) or
            payload["format"] != 1 or payload["model"] != api.MODEL or payload["kind"] not in ("firmware", "assets") or
            type(payload["sequence"]) is not int or not 1 <= payload["sequence"] <= 0xffffffff or
            not valid_https(payload["base_url"])):
        raise ValueError("Invalid signed update metadata")
    if not isinstance(payload['release'], str):
        raise ValueError('Invalid release version')
    version(payload['release'])
    validate_package(payload["package"], api)
    public = public_key.read_bytes()
    if len(public) > 512 or b"PRIVATE" in public:
        raise ValueError("Use the separately trusted P-256 public key")
    info = subprocess.run(["openssl", "pkey", "-pubin", "-text", "-noout"], input=public, check=True, capture_output=True).stdout
    if b"ASN1 OID: prime256v1" not in info:
        raise ValueError("Signing key must be P-256")
    der = subprocess.run(["openssl", "pkey", "-pubin", "-outform", "DER"], input=public, check=True, capture_output=True).stdout
    with tempfile.TemporaryDirectory(prefix="pos-verify-update-") as temp:
        signature = Path(temp) / "signature.der"; signature.write_bytes(bytes.fromhex(envelope["signature"]))
        subprocess.run(["openssl", "dgst", "-sha256", "-verify", str(public_key), "-signature", str(signature)],
                       input=envelope["payload"].encode(), check=True, capture_output=True)
    expected_build = None
    if payload["kind"] == "firmware":
        firmware = directory / "firmware.bin"
        record = payload["package"]["firmware"]
        if (firmware.is_symlink() or firmware.stat().st_size != record["size"] or api.sha256(firmware) != record["sha256"] or
                api.inspect_image(firmware) != payload["package"]["version"]):
            raise ValueError("Local firmware differs from the signed package")
        api.verify_image(firmware)
        with firmware.open("rb") as f:
            f.seek(176); expected_build = f.read(32).hex()
    for record in payload["package"]["files"]:
        file = directory / "payload" / record["path"]
        # Asset packages may omit unchanged records, which the device reuses.
        if not file.exists() and payload["kind"] == "assets":
            continue
        if (not file.resolve().is_relative_to(directory.resolve()) or file.is_symlink() or
                not file.is_file() or file.stat().st_size != record["size"] or api.sha256(file) != record["sha256"]):
            raise ValueError("Local payload differs from the signed package")
    return raw, payload, hashlib.sha256(der).hexdigest(), expected_build


def validate_update(directory: Path, public_key: Path, api):
    result = verify_update_artifact(directory, public_key, api)
    if set(result[1]) != {"format", "model", "release", "sequence", "kind", "base_url", "package"}:
        raise ValueError("Invalid signed update fields")
    return result


def deploy(args, api) -> dict:
    if not valid_https(args.manifest_url) or not 1 <= args.wait <= 900:
        raise ValueError("Use an HTTPS manifest URL and wait duration of 1..900 seconds")
    raw, payload, fingerprint, expected_build = validate_update(args.update_dir, args.public_key, api)
    before = status(args.device, args.key_file)
    readiness(args.validation_report, args.evidence_root, before, api.sha256)
    if (before.get("trusted_key") != fingerprint or before.get("healthy") is not True or before.get("ota_confirmed") is not True or
            before.get("update_pending") is not False or before.get("accepted_sequence", 0) >= payload["sequence"]):
        raise ValueError("Target trust/health/update sequence does not permit this deployment")
    # Check the hosted bytes with normal system TLS trust before any request.
    opener = build_opener(NoRedirect(), HTTPSHandler(context=ssl.create_default_context()))
    with opener.open(args.manifest_url, timeout=30) as response:
        if response.status != 200 or response.read(32769) != raw:
            raise ValueError("Hosted signed manifest differs from the reviewed local update")
    request(args.device, args.key_file, "POST", "/deploy", args.manifest_url.encode())
    print("Request sent. Approve the signed update on Panputer. After firmware reboot, reopen Developer deployment and replace the private key file with its new session key to verify the boot.", flush=True)
    end = time.monotonic() + args.wait
    while time.monotonic() < end:
        try:
            current = status(args.device, args.key_file)
        except (OSError, ValueError, http.client.HTTPException):
            time.sleep(1)
            continue
        if current["device_id"] != before["device_id"] or current.get("trusted_key") != fingerprint:
            raise ValueError("Target identity or trusted publisher changed; deployment not confirmed")
        if current.get("deployment") == 4:
            raise ValueError("Deployment was cancelled or rejected; inspect the device screen")
        if (current.get("accepted_sequence") == payload["sequence"] and
                current.get("version") == payload["package"]["version"] and
                current.get("build_id") == (expected_build or before["build_id"]) and
                current.get("healthy") is True and current.get("update_pending") is False and
                (payload["kind"] == "assets" or current.get("ota_confirmed") is True)):
            return current
        time.sleep(1)
    raise ValueError("Deployment not confirmed before timeout. Do not assume success; inspect Software Update, recovery and version on device")


def verify_installer(image: Path, expected_sha: str, api) -> None:
    if (not re.fullmatch("[0-9a-f]{64}", expected_sha) or image.is_symlink() or
            api.sha256(image) != expected_sha or not 0x10120 <= image.stat().st_size <= 0x110000):
        raise ValueError("Installer checksum/size invalid; no write attempted")
    data = image.read_bytes()
    # Enforce the existing POS layout, including both retained runtime slots.
    import struct
    records = []
    for at in range(0x8000, 0x8c00, 32):
        magic, kind, sub, offset, size = struct.unpack_from("<HBBII", data, at)
        if magic != 0x50aa:
            break
        records.append((kind, sub, offset, size))
    labels = (b"nvs", b"otadata", b"phy_init", b"factory", b"ota_0", b"ota_1")
    for i, label in enumerate(labels):
        name, flags = struct.unpack_from("<16sI", data, 0x8000+i*32+12)
        if name.rstrip(b"\0") != label or flags:
            raise ValueError("Unexpected POS partition names/flags; no write attempted")
    md5_at = 0x8000+6*32
    if data[md5_at:md5_at+16] != b"\xeb\xeb"+b"\xff"*14 or data[md5_at+16:md5_at+32] != hashlib.md5(data[0x8000:md5_at]).digest():
        raise ValueError("Partition table MD5 invalid; no write attempted")
    expected = [(1,2,0x9000,0x4000),(1,0,0xd000,0x2000),(1,1,0xf000,0x1000),
                (0,0,0x10000,0x100000),(0,0x10,0x110000,0x170000),(0,0x11,0x280000,0x170000)]
    if records != expected:
        raise ValueError("Unexpected POS partition layout; no write attempted")
    with tempfile.TemporaryDirectory(prefix="pos-installer-check-") as temp:
        app = Path(temp) / "installer.bin"; app.write_bytes(data[0x10000:])
        api.inspect_image(app, "pos_installer", 0x100000); api.verify_image(app)
        from esptool.bin_image import LoadFirmwareImage
        boot = Path(temp) / "bootloader.bin"; boot.write_bytes(data[:0x8000])
        parsed = LoadFirmwareImage("esp32s3", str(boot))
        boot.write_bytes(data[:parsed.data_length+32]); api.verify_image(boot)


def flash(args, api) -> None:
    if not args.confirm_flash:
        raise ValueError("Flashing replaces the bootloader/partition table/NVS/installer. Review the verified factory backup and SD package, then supply --confirm-flash")
    verify_installer(args.image, args.sha256, api)
    backup = args.factory_backup
    metadata = json.loads(Path(str(backup) + ".json").read_text())
    current = api.hardware_probe(args.port)
    if (metadata.get("format") != 1 or metadata.get("chip") != "esp32s3" or
            type(metadata.get("bytes")) is not int or not 4*1024*1024 <= metadata["bytes"] <= 128*1024*1024 or metadata["bytes"] & (metadata["bytes"]-1) or
            not metadata.get("readback_verified") or not metadata.get("plain_development_flash") or
            not current.get("plain_development_flash") or metadata.get("device_id") != current.get("device_id") or
            not current.get("device_id") or backup.is_symlink() or backup.stat().st_size != metadata.get("bytes") or
            api.sha256(backup) != metadata.get("sha256")):
        raise ValueError("Factory backup does not match this unencrypted development device; no write attempted")
    api.hardware_write_verified(args.port, args.image)
