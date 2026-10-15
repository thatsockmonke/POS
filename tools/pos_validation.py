"""Offline hardware evidence collection. Never flashes or invents observations."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import time

CHECKS = (
    "board_revision", "usb_download", "factory_backup", "factory_restore_boot",
    "display", "keyboard_56", "global_home", "sd_mount", "sd_streaming",
    "heap", "boot", "frame_timing", "battery", "wifi", "sleep_wake", "cap_conflicts",
    "cut_marker", "cut_sd_copy", "cut_sd_commit", "cut_firmware_write",
    "cut_boot_select", "cut_boot_confirm", "cut_backup_finalize",
    "cut_reinstall_retire", "cut_restore_migrate", "cut_cleanup",
)


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()


def read_json(path: Path):
    if path.is_symlink() or not path.is_file() or path.stat().st_size > 2 * 1024 * 1024:
        raise ValueError(f"Expected a regular JSON evidence file below 2 MiB: {path.name}")
    return json.loads(path.read_text())


def write_new(path: Path, value) -> None:
    # Only a finalized complete report receives the requested filename.
    part = Path(str(path) + ".part")
    if path.exists() or part.exists():
        raise ValueError("Report or partial report exists; choose a fresh output path")
    with part.open("x") as f:
        f.write(json.dumps(value, indent=2, allow_nan=False) + "\n")
        f.flush()
        import os
        os.fsync(f.fileno())
    # Exclusive publication on Linux (normal development host).
    import os
    os.link(part, path)
    part.unlink()


def template(output: Path) -> None:
    write_new(output, {"format": 1, "observations": [
        {"check": check, "result": "not_run", "note": "", "evidence": []}
        for check in CHECKS
    ]})


def hardware_report(root: Path, observations: Path | None, output: Path) -> dict:
    root = root.resolve(strict=True)
    checks = {check: {"result": "not_run", "note": "", "evidence": []} for check in CHECKS}
    measurements = []
    for directory in (root / "Diagnostics", root / "POS/Users/Owner/Diagnostics"):
        if not directory.exists():
            continue
        if directory.is_symlink() or not directory.is_dir():
            raise ValueError("Diagnostics must be an ordinary directory")
        for path in sorted(directory.glob("*.json")):
            data = read_json(path)
            if not isinstance(data, dict):
                raise ValueError(f"Invalid diagnostic object: {path.name}")
            if data.get("kind") == "pos-device-measurement":
                if (data.get("format") != 1 or data.get("measured_on_device") is not True or
                        not isinstance(data.get("build_id"), str) or len(data["build_id"]) != 64 or
                        any(c not in "0123456789abcdef" for c in data["build_id"]) or
                        not isinstance(data.get("boot_id"), str) or len(data["boot_id"]) != 16 or
                        any(c not in "0123456789abcdef" for c in data["boot_id"]) or
                        not isinstance(data.get("phase"), str)):
                    raise ValueError(f"Unsupported measurement: {path.name}")
                for field in ("heap", "minimum_heap", "largest_internal_block", "boot_ms", "firmware_bytes"):
                    if type(data.get(field)) is not int or data[field] < 0:
                        raise ValueError(f"Invalid {field} in {path.name}")
            elif {"bytes", "crc32", "write_us", "read_us"} <= data.keys():
                if any(type(data[k]) is not int or data[k] < 0 for k in ("bytes", "crc32", "write_us", "read_us")):
                    raise ValueError(f"Invalid SD measurement: {path.name}")
            else:
                continue
            measurements.append({"path": str(path.relative_to(root)), "sha256": digest(path), "measurement": data})
    if observations:
        data = read_json(observations)
        if not isinstance(data, dict) or data.get("format") != 1 or not isinstance(data.get("observations"), list):
            raise ValueError("Unsupported observation format")
        seen = set()
        base = observations.resolve().parent
        for item in data["observations"]:
            if not isinstance(item, dict) or item.get("check") not in checks or item["check"] in seen:
                raise ValueError("Unknown or duplicate hardware check")
            check = item["check"]; seen.add(check)
            result, note, evidence = item.get("result"), item.get("note"), item.get("evidence")
            if (result not in ("not_run", "pass", "fail") or not isinstance(note, str) or
                    not isinstance(evidence, list) or not all(isinstance(p, str) for p in evidence)):
                raise ValueError("Each check needs a result, note and evidence array")
            if result != "not_run" and (not note.strip() or not evidence):
                raise ValueError("Pass/fail requires an actual observation and evidence; do not infer it from a host test")
            records = []
            for name in evidence:
                path = (base / name).resolve(strict=True)
                if not path.is_relative_to(base) or not path.is_file():
                    raise ValueError("Evidence must be a file inside the observation folder")
                records.append({"path": name, "sha256": digest(path), "bytes": path.stat().st_size})
            checks[check] = {"result": result, "note": note, "evidence": records, "basis": "operator observation"}
    # Comparisons are grouped by build, never across unrelated firmware images.
    groups = {}
    for entry in measurements:
        data = entry["measurement"]
        if data.get("kind") == "pos-device-measurement":
            groups.setdefault((data["build_id"],data["boot_id"]), {}).setdefault(data["phase"], []).append(data)
    comparisons = []
    for (build, boot), phases in groups.items():
        for before, after in (("wifi-before", "wifi-connected"), ("apps-before", "apps-after")):
            # Multiple runs require human matching; don't pair arbitrary samples.
            if len(phases.get(before, [])) == len(phases.get(after, [])) == 1:
                a, b = phases[before][0], phases[after][0]
                if b.get("uptime_ms", 0) >= a.get("uptime_ms", 0):
                    comparisons.append({"build_id": build, "boot_id": boot, "before": before, "after": after,
                                        "heap_change_bytes": b["heap"] - a["heap"],
                                        "interpretation": "Observed change; background services and allocator state may contribute"})
    report = {"format": 1, "created_at": int(time.time()), "checks": checks,
              "measurements": measurements, "comparisons": comparisons,
              "physical_validation_complete": all(c["result"] == "pass" for c in checks.values()),
              "evidence_policy": "Measurements are collected evidence; physical checks require explicit operator observations."}
    write_new(output, report)
    return report


def serial_logs(port: str, seconds: float, output: Path) -> None:
    if not 0 < seconds <= 600:
        raise ValueError("Capture duration must be between 0 and 600 seconds")
    if output.exists() or Path(str(output) + ".part").exists():
        raise ValueError("Log output exists; choose a new filename")
    import serial  # Supplied by the pinned ESP-IDF Python environment.
    import os
    part = Path(str(output) + ".part")
    # Suppress automatic DTR/RTS toggles; capture must not deliberately reset.
    device = serial.Serial(port=None, baudrate=115200, timeout=0.2)
    device.dtr = False
    device.rts = False
    device.port = port
    try:
        device.open()
        with part.open("xb") as f:
            os.chmod(part, 0o600)
            end = time.monotonic() + seconds
            try:
                while time.monotonic() < end:
                    chunk = device.read(4096)
                    if chunk:
                        f.write(chunk)
            except KeyboardInterrupt:
                pass
            f.flush(); os.fsync(f.fileno())
        os.link(part, output); part.unlink()
    finally:
        device.close()
