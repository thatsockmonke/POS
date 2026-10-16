"""Linux removable-media preparation. Discovery never modifies a device.

Only the explicit format_card entry point runs privileged disk tools, after
confirmation and fresh identity/system-mount checks. Crostini folders use copy
only. No disk name, label or size alone establishes that it is safe to erase.
"""
from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import time


class SetupError(Exception):
    pass


def run(argv: list[str], **kwargs):
    result = subprocess.run(argv, text=True, capture_output=True, **kwargs)
    if result.returncode:
        raise SetupError(f"{Path(argv[0]).name} failed: {(result.stderr or result.stdout).strip()[-1200:]}")
    return result


def system_tool(name: str) -> str:
    # Never execute a user PATH shadow of a disk utility through sudo.
    for directory in ("/usr/sbin", "/usr/bin", "/sbin", "/bin"):
        path = Path(directory) / name
        if path.is_file() and os.access(path, os.X_OK):
            return str(path)
    raise SetupError(f"Missing Linux utility: {name}. The wizard can install the Linux support tools.")


def mounts(node: dict) -> list[str]:
    value = node.get("mountpoints") or []
    return [v for v in value if isinstance(v, str) and v]


def descendants(node: dict):
    yield node
    for child in node.get("children") or []:
        yield from descendants(child)


def fingerprint(node: dict) -> tuple:
    return tuple(node.get(k) for k in ("path", "maj:min", "size", "serial", "wwn", "diskseq", "log-sec"))


@dataclass
class Inventory:
    disks: list[dict]
    protected: set[str]


def inventory(protect_paths: tuple[Path, ...] = ()) -> Inventory:
    result = run([system_tool("lsblk"), "--json", "--bytes", "--output",
                  "NAME,PATH,TYPE,SIZE,RM,HOTPLUG,RO,TRAN,MODEL,SERIAL,WWN,MAJ:MIN,FSTYPE,FSVER,MOUNTPOINTS,LOG-SEC"])
    data = json.loads(result.stdout)
    disks = [n for n in data["blockdevices"] if n.get("type") == "disk"]
    protected: set[str] = set()
    # Protect mounted system filesystems and the wizard, downloads and backups.
    paths = {Path("/"), Path("/boot"), Path("/home"), Path("/usr"), Path("/var"),
             Path.cwd(), Path.home(), *protect_paths}
    # A swap file and loop-backed live system can occupy a removable disk too.
    swaps = Path("/proc/swaps")
    if swaps.is_file():
        for line in swaps.read_text().splitlines()[1:]:
            if line.split():
                paths.add(Path(line.split()[0]))
    for backing in Path("/sys/class/block").glob("loop*/loop/backing_file"):
        try:
            value = backing.read_text().strip()
            if value:
                value = re.sub(r"\\([0-7]{3})", lambda m: chr(int(m[1], 8)), value)
                path = Path("/") / value.lstrip("/")
                if not path.exists():
                    raise SetupError("Cannot safely locate active virtual-disk storage. Direct formatting is disabled; use a mounted FAT32 card folder or another Linux session.")
                paths.add(path)
        except OSError as error:
            raise SetupError("Cannot safely inspect active virtual-disk storage. Direct formatting is disabled; use a mounted FAT32 card folder.") from error
    for path in paths:
        # A planned download/backup path may not exist yet.
        while not path.exists() and path != path.parent:
            path = path.parent
        found = run([system_tool("findmnt"), "--json", "--target", str(path),
                     "--output", "MAJ:MIN,TARGET,SOURCE,FSTYPE"])
        for fs in found_json(found.stdout):
            protected.add(fs["maj:min"])
    for disk in disks:
        for node in descendants(disk):
            name = Path(node["path"]).name
            try:
                disk["diskseq"] = (Path("/sys/class/block") / Path(disk["path"]).name / "diskseq").read_text().strip()
            except OSError:
                disk["diskseq"] = None
            holders = Path("/sys/class/block") / name / "holders"
            if holders.is_dir() and any(holders.iterdir()):
                protected.add(node["maj:min"])
    return Inventory(disks, protected)


def found_json(text: str):
    for node in json.loads(text).get("filesystems", []):
        yield node
        yield from _nested_mounts(node)


def _nested_mounts(node):
    for child in node.get("children", []):
        yield child
        yield from _nested_mounts(child)


def reason_unusable(node: dict, protected: set[str]) -> str | None:
    if not re.fullmatch(r"/dev/(?:sd[a-z]+|mmcblk[0-9]+)", node.get("path", "")):
        return "not a supported whole SD/USB disk"
    if node.get("type") != "disk" or node.get("ro") or type(node.get("size")) is not int or not 512*1024*1024 <= node["size"] < 2**41:
        return "read-only disk or capacity outside 512 MiB–2 TiB"
    if node.get("log-sec") != 512:
        return "not a 512-byte-sector SD profile"
    if not node.get("rm") and not (node.get("hotplug") and node.get("tran") == "usb"):
        return "internal/non-removable disk"
    if not node.get("diskseq"):
        return "kernel device identity unavailable; use an already formatted card"
    for item in descendants(node):
        if item.get("maj:min") in protected:
            return "contains system, swap, active storage, or the wizard/download/backup"
        if item.get("type") not in ("disk", "part") or item.get("ro"):
            return "has active mapped storage or a read-only partition"
        for mount in mounts(item):
            if not mount.startswith(("/media/", "/run/media/")):
                return "mounted outside the removable-media folders"
    return None


def candidates(current: Inventory) -> list[dict]:
    return [d for d in current.disks if reason_unusable(d, current.protected) is None]


def description(disk: dict) -> str:
    label = " ".join(re.sub(r"[\x00-\x1f\x7f-\x9f]", "?", str(disk.get("model") or "SD/USB reader")).split())
    filesystems = ", ".join(sorted({str(n["fstype"]) for n in descendants(disk) if n.get("fstype")})) or "unformatted"
    return f"{disk['path']} — {disk['size']/10**9:.1f} GB — {label} — {filesystems}"


def block_matches(path: str, major_minor: str) -> bool:
    st = os.stat(path, follow_symlinks=False)
    return stat.S_ISBLK(st.st_mode) and f"{os.major(st.st_rdev)}:{os.minor(st.st_rdev)}" == major_minor


def check_identity(original: dict, protect_paths: tuple[Path, ...], *, unmounted=False) -> dict:
    current = inventory(protect_paths)
    match = next((d for d in current.disks if d.get("path") == original["path"]), None)
    if not match or fingerprint(match) != fingerprint(original):
        raise SetupError("The selected card was removed or changed. Nothing else will be erased. Rescan and select it again.")
    why = reason_unusable(match, current.protected)
    if why:
        raise SetupError("Refusing to erase this disk: " + why)
    if unmounted and any(mounts(n) for n in descendants(match)):
        raise SetupError("The card is still mounted/in use. Close Files and other apps, then try again.")
    if not block_matches(original["path"], original["maj:min"]):
        raise SetupError("The card's Linux device identity changed; refusing to write.")
    return match


def format_card(disk: dict, protect_paths: tuple[Path, ...], confirmation: str, announce=print) -> dict:
    """Erase precisely one freshly rechecked removable disk; never called by scan."""
    expected = "ERASE " + disk["path"]
    if confirmation != expected:
        raise SetupError("Formatting cancelled. The confirmation did not match; no disk was changed.")
    current = check_identity(disk, protect_paths)
    sudo, sfdisk, mkfs, umount = [system_tool(n) for n in ("sudo", "sfdisk", "mkfs.fat", "umount")]
    run([sudo, "-v"])
    # Every mounted filesystem has already passed the removable-path policy.
    for node in descendants(current):
        if mounts(node):
            check_identity(disk, protect_paths)
            announce("Closing the card's mounted filesystem...")
            run([sudo, "-n", umount, "--", node["path"]])
    check_identity(disk, protect_paths, unmounted=True)
    announce("Creating one FAT32 partition on " + disk["path"] + "...")
    run([sudo, "-n", sfdisk, "--lock=yes", "--wipe=always", "--wipe-partitions=always", disk["path"]],
        input="label: dos\nunit: sectors\n\nstart=2048, type=c\n")
    settle = shutil.which("udevadm")
    if settle:
        run([settle, "settle", "--timeout=10"])
    partition = None
    for _ in range(20):
        current = check_identity(disk, protect_paths, unmounted=True)
        parts = [n for n in current.get("children", []) if n.get("type") == "part"]
        if len(parts) == 1:
            partition = parts[0]
            break
        time.sleep(0.25)
    if partition is None:
        raise SetupError("Linux did not expose the new partition. Reinsert the card and run the wizard again.")
    suffix = "p1" if disk["path"].startswith("/dev/mmcblk") else "1"
    if partition["path"] != disk["path"] + suffix:
        raise SetupError("Unexpected partition identity; formatting stopped.")
    if not block_matches(partition["path"], partition["maj:min"]):
        raise SetupError("The partition changed; formatting stopped.")
    check_identity(disk, protect_paths, unmounted=True)
    run([sudo, "-n", mkfs, "-F", "32", "-n", "POSINSTALL", "--", partition["path"]])
    if settle:
        run([settle, "settle", "--timeout=10"])
    announce("FAT32 format completed.")
    return partition


def mount_partition(disk: dict, partition: dict, protect_paths: tuple[Path, ...]) -> Path:
    current = check_identity(disk, protect_paths)
    node = next((n for n in descendants(current) if n["path"] == partition["path"] and n["maj:min"] == partition["maj:min"]), None)
    if not node:
        raise SetupError("Partition disappeared; reinsert and rescan the card.")
    if not mounts(node):
        run([system_tool("udisksctl"), "mount", "--block-device", node["path"]])
    for _ in range(20):
        current = check_identity(disk, protect_paths)
        node = next((n for n in descendants(current) if n["path"] == partition["path"] and n["maj:min"] == partition["maj:min"]), None)
        if node and mounts(node):
            path = Path(mounts(node)[0])
            if path.is_dir():
                return path
        time.sleep(0.25)
    raise SetupError("Card formatted, but Linux did not mount it. Open it in Files, then select its folder in the wizard.")


def shared_cards() -> list[Path]:
    roots = (Path("/mnt/chromeos/removable"), Path("/media") / __import__("getpass").getuser(),
             Path("/run/media") / __import__("getpass").getuser())
    return sorted({p for root in roots if root.is_dir() for p in root.iterdir() if p.is_dir() and not p.is_symlink()})


def card_folder_type(card: Path) -> str:
    if card.is_symlink() or not card.is_dir():
        raise SetupError("That card folder is unavailable. Share/mount the card and select its top-level folder.")
    found = list(found_json(run([system_tool("findmnt"), "--json", "--target", str(card.resolve()),
                                "--output", "MAJ:MIN,TARGET,SOURCE,FSTYPE"]).stdout))
    if len(found) != 1:
        raise SetupError("Cannot identify the card's mounted folder; reinsert/share it and try again.")
    kind, target = found[0].get("fstype", ""), Path(found[0]["target"])
    if kind == "vfat":
        if card.resolve() != target.resolve() or str(target).startswith(("/boot", "/efi")):
            raise SetupError("Choose the card's top-level mounted folder, not a subfolder or system boot folder.")
    elif kind in ("9p", "virtiofs") or kind.startswith("fuse."):
        chrome = Path("/mnt/chromeos/removable")
        if card.resolve().is_relative_to(chrome) and len(card.resolve().relative_to(chrome).parts) != 1:
            raise SetupError("Choose the card name directly under /mnt/chromeos/removable, not a subfolder.")
    else:
        raise SetupError(f"This folder uses {kind or 'an unknown filesystem'}, not FAT32. Select the actual card, or choose its direct disk and Format as FAT32. A Linux home/Downloads folder is not the SD card.")
    return kind


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()


def copy_package(package: Path, card: Path, progress=print, check=None) -> None:
    """Copy and read back a staged package; never format or replace user folders."""
    if card.is_symlink() or not card.is_dir() or card.resolve() == Path("/") or card.resolve() == Path.home().resolve():
        raise SetupError("Choose the microSD's top-level folder, not your home/system folder.")
    card = card.resolve()
    identity = (card.stat().st_dev, card.stat().st_ino)
    def guard():
        if check:
            check()
        current = card.stat()
        if (current.st_dev, current.st_ino) != identity:
            raise SetupError("The card folder was replaced/unmounted during copying. Partial files retained.")
    guard()
    if package.resolve().is_relative_to(card):
        raise SetupError("Keep the downloaded package on your computer, outside the card being prepared.")
    if (card / "POS").exists() or (card / "POS").is_symlink():
        raise SetupError("This card already has a POS installation. Back it up before reinstalling; the wizard will not replace it.")
    final, staged = card / "pos-install", card / "pos-install.copying"
    files = sorted(p for p in package.rglob("*") if p.is_file())
    if not files or any(p.is_symlink() for p in package.rglob("*")):
        raise SetupError("Installation package contains an unsafe path or no files.")
    total = sum(p.stat().st_size for p in files)
    if shutil.disk_usage(card).free < total*2 + 16*1024*1024:
        raise SetupError("Not enough space on the card for the package plus installation workspace. Space for both the package and installed system, plus 16 MiB, is required.")
    for target in (final, staged):
        if target.is_symlink():
            raise SetupError("Unexpected link on card; no files copied.")
    if final.exists():
        existing = {p.relative_to(final).as_posix(): p for p in final.rglob("*") if p.is_file()}
        wanted = {p.relative_to(package).as_posix(): p for p in files}
        if any(p.is_symlink() for p in final.rglob("*")) or existing.keys() != wanted.keys() or any(sha256(existing[k]) != sha256(p) for k, p in wanted.items()):
            raise SetupError("A different pos-install folder already exists. Preserve/move it before preparing this card; it will not be overwritten.")
        progress("Matching installer files are already on this card and verified.")
        return
    if staged.exists():
        raise SetupError("An interrupted pos-install.copying folder is on this card. Preserve/move it in Files, then rerun; it has not been marked complete.")
    staged.mkdir()
    copied = 0
    for source in files:
        guard()
        relative = source.relative_to(package)
        target = staged / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        with source.open("rb") as src, target.open("xb") as dst:
            while chunk := src.read(65536):
                dst.write(chunk)
            dst.flush(); os.fsync(dst.fileno())
        if source.stat().st_size != target.stat().st_size or sha256(source) != sha256(target):
            raise SetupError("Card readback did not match. Partial files retained; do not install from them.")
        copied += source.stat().st_size
        progress(f"Copied and verified {copied}/{total} bytes — {relative}")
    guard()
    # Recheck destination at commit. Ordinary removable-media FAT has no links.
    if final.exists() or final.is_symlink() or (card / "POS").exists():
        raise SetupError("Card contents changed during preparation; staged files retained.")
    staged.rename(final)
    os.sync()
    progress("Installer files are verified and ready on the card.")
