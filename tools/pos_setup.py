"""Friendly Linux/Crostini installation wizard. Build the standalone pos-setup
executable with build_setup.py. No ESP-IDF or source checkout is needed to use it.
"""
from __future__ import annotations

import argparse
from functools import cmp_to_key
from contextlib import contextmanager
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import time
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen
import zipfile

import pos_setup_storage as storage
from pos_setup_storage import SetupError

VERSION = "0.5.8-dev"
REPOSITORY = "thatsockmonke/POS"
MAX_DOWNLOAD = 256 * 1024 * 1024
MAX_UNPACK = 256 * 1024 * 1024
ESPTOOL = "4.12.0"


def say(message=""):
    print(message, flush=True)


def ask(question: str, *, default=False) -> bool:
    suffix = " [Y/n] " if default else " [y/N] "
    while True:
        answer = input(question + suffix).strip().lower()
        if answer in ("y", "yes"):
            return True
        if answer in ("n", "no"):
            return False
        if not answer:
            return default
        say("Please type Y or N, then press Enter. Your previous answer was not accepted.")


def choose(title: str, items: list[str]) -> int:
    say("\n" + title)
    for i, item in enumerate(items, 1):
        say(f"  {i}. {item}")
    while True:
        answer = input("Choose a number (or Q to cancel): ").strip()
        if answer.lower() in ("q", "quit"):
            raise SetupError("Cancelled. Completed backups/downloads have been kept.")
        if answer.isdigit() and 1 <= int(answer) <= len(items):
            return int(answer) - 1
        say("Enter one of the numbers above.")


def crostini() -> bool:
    return Path("/mnt/chromeos").is_dir() or Path("/dev/.cros_milestone").exists() or "cros" in os.uname().release.lower()


def private_directory(path: Path) -> Path:
    if path.is_symlink():
        raise SetupError("The POS working directory must not be a symbolic link.")
    path.mkdir(parents=True, mode=0o700, exist_ok=True)
    if not path.is_dir() or path.stat().st_uid != os.getuid():
        raise SetupError("Choose a POS working directory owned by your Linux user.")
    if path.stat().st_mode & 0o077:
        raise SetupError(f"Keep backups private: run chmod 700 {str(path)!r}, then try again.")
    return path.absolute()


def install_linux_tools(names: list[str]) -> None:
    if not shutil.which("apt-get") or not shutil.which("sudo"):
        raise SetupError("Install these packages with your Linux package manager: " + " ".join(names))
    say("The wizard needs these Linux support packages: " + ", ".join(names))
    say("Your computer may ask for your Linux password. POS itself will still run as your normal user.")
    if not ask("Install those packages now?", default=True):
        raise SetupError("Support-package installation cancelled. No card or firmware was changed.")
    subprocess.run([storage.system_tool("sudo"), storage.system_tool("apt-get"), "update"], check=True)
    subprocess.run([storage.system_tool("sudo"), storage.system_tool("apt-get"), "install", "-y", *names], check=True)


class GitHub:
    def __init__(self, interactive=False):
        self.interactive = interactive
        self.use_gh = False

    def json(self, endpoint: str):
        # Reuse GitHub CLI authentication, including private-repository access.
        if shutil.which("gh"):
            r = subprocess.run(["gh", "api", "--hostname", "github.com", endpoint], text=True, capture_output=True)
            if not r.returncode:
                if len(r.stdout) > 16*1024*1024:
                    raise SetupError("GitHub release metadata is unexpectedly large.")
                self.use_gh = True
                return json.loads(r.stdout)
        request = Request("https://api.github.com/" + endpoint,
                          headers={"User-Agent": "POS-Setup/" + VERSION, "Accept": "application/vnd.github+json"})
        try:
            with urlopen(request, timeout=30) as response:
                data = response.read(16*1024*1024+1)
            if len(data) > 16*1024*1024:
                raise SetupError("GitHub release metadata is unexpectedly large.")
            return json.loads(data)
        except HTTPError as error:
            if error.code not in (401, 403, 404) or not self.interactive:
                raise SetupError("Cannot read POS releases. For this private repository, sign in with 'gh auth login' and rerun. A public repository does not require sign-in.") from error
        say("POS may be private. Sign in to the GitHub account that can access " + REPOSITORY + ".")
        if not shutil.which("gh"):
            install_linux_tools(["gh"])
        if not ask("Open GitHub's browser sign-in now?", default=True):
            raise SetupError("Sign-in cancelled. Your GitHub password/token is never requested by POS Setup.")
        subprocess.run(["gh", "auth", "login", "--hostname", "github.com", "--git-protocol", "https", "--web"], check=True)
        result = subprocess.run(["gh", "api", "--hostname", "github.com", endpoint], text=True, capture_output=True)
        if result.returncode:
            raise SetupError("GitHub access is still unavailable. Check that your account can open the POS repository, then rerun.")
        self.use_gh = True
        return json.loads(result.stdout)

    def release(self, tag: str | None = None) -> dict:
        from pos_updates import compare, version
        releases = []
        if tag:
            version(tag.removeprefix("v"))
            releases = [self.json(f"repos/{REPOSITORY}/releases/tags/v{tag.removeprefix('v')}")]
        else:
            # /releases/latest excludes development prereleases. POS must include
            # them, and compare versions rather than mistake a backport for latest.
            for page in range(1, 11):
                batch = self.json(f"repos/{REPOSITORY}/releases?per_page=100&page={page}")
                if not isinstance(batch, list):
                    raise SetupError("GitHub returned invalid release metadata.")
                releases.extend(batch)
                if len(batch) < 100:
                    break
            else:
                raise SetupError("Too many release pages; select a specific version with --release.")
        compatible = []
        for r in releases:
            if not isinstance(r, dict) or r.get("draft") or not isinstance(r.get("tag_name"), str):
                continue
            tag_value = r["tag_name"]
            if not tag_value.startswith("v"):
                continue
            try:
                version(tag_value[1:])
            except ValueError:
                continue
            names = {a.get("name") for a in r.get("assets", [])}
            name = f"pos-{tag_value[1:]}.zip"
            if name in names and name + ".sha256" in names:
                compatible.append(r)
        if not compatible:
            raise SetupError("No published POS release has a complete firmware/SD download yet. Try again after the release build finishes.")
        return max(compatible, key=cmp_to_key(lambda a, b: compare(a["tag_name"][1:], b["tag_name"][1:])))

    def asset(self, release: dict, name: str, destination: Path, limit: int) -> None:
        matches = [a for a in release.get("assets", []) if a.get("name") == name]
        if len(matches) != 1:
            raise SetupError("Missing or duplicate release asset: " + name)
        asset = matches[0]
        size = asset.get("size")
        if type(size) is not int or not 0 < size <= limit or type(asset.get("id")) is not int:
            raise SetupError("Release asset size/identity is invalid: " + name)
        part = destination.with_name(destination.name + ".part-" + str(time.time_ns()))
        say("Downloading " + name + f" ({size / 1024 / 1024:.1f} MiB)...")
        with part.open("xb") as output:
            if self.use_gh:
                process = subprocess.Popen(["gh", "api", "--hostname", "github.com",
                    f"repos/{REPOSITORY}/releases/assets/{asset['id']}", "--header", "Accept: application/octet-stream"],
                    stdout=output, stderr=subprocess.DEVNULL)
                try:
                    while process.poll() is None:
                        if part.stat().st_size > size:
                            raise SetupError("Download exceeded its expected size; partial retained.")
                        time.sleep(0.1)
                    process.wait()
                    if process.returncode:
                        raise SetupError("GitHub download failed; partial retained. Check sign-in/network and retry.")
                finally:
                    if process.poll() is None:
                        process.kill(); process.wait()
            else:
                # Construct the URL ourselves; credentials are never forwarded to
                # an arbitrary host supplied in release metadata.
                url = f"https://github.com/{REPOSITORY}/releases/download/{release['tag_name']}/{name}"
                with urlopen(Request(url, headers={"User-Agent": "POS-Setup/" + VERSION}), timeout=30) as response:
                    count = 0
                    while chunk := response.read(65536):
                        count += len(chunk)
                        if count > size:
                            raise SetupError("Download exceeded its expected size; partial retained.")
                        output.write(chunk)
            output.flush(); os.fsync(output.fileno())
        if part.stat().st_size != size:
            raise SetupError("Download was incomplete; partial retained. Retry with a working connection.")
        digest = asset.get("digest")
        if digest and (not isinstance(digest, str) or not re.fullmatch(r"sha256:[0-9a-f]{64}", digest) or
                       storage.sha256(part) != digest[7:]):
            raise SetupError("GitHub asset digest did not match; nothing will be installed.")
        if destination.exists() or destination.is_symlink():
            raise SetupError("Download destination already exists; choose a fresh working folder.")
        part.rename(destination)


def download_release(github: GitHub, state: Path, tag=None) -> tuple[Path, str]:
    release = github.release(tag)
    version = release["tag_name"][1:]
    say("Latest selected POS: " + version + (" (experimental prerelease)" if release.get("prerelease") else ""))
    say(f"https://github.com/{REPOSITORY}/releases/tag/v{version}")
    folder = state / "downloads" / ("v" + version + "-" + str(time.time_ns()))
    folder.mkdir(parents=True, mode=0o700)
    archive = folder / f"pos-{version}.zip"
    github.asset(release, archive.name + ".sha256", Path(str(archive) + ".sha256"), 1024)
    github.asset(release, archive.name, archive, MAX_DOWNLOAD)
    verify_checksum(archive)
    return archive, version


def verify_checksum(archive: Path) -> None:
    sidecar = Path(str(archive) + ".sha256")
    if archive.is_symlink() or not archive.is_file() or archive.stat().st_size > MAX_DOWNLOAD or sidecar.is_symlink() or sidecar.stat().st_size > 1024:
        raise SetupError("Choose a regular POS ZIP and its matching .sha256 file.")
    match = re.fullmatch(r"([0-9a-f]{64})  ([A-Za-z0-9._-]+)\n?", sidecar.read_text())
    if not match or match[2] != archive.name or storage.sha256(archive) != match[1]:
        raise SetupError("Download checksum did not match. Stop and download a fresh copy; no card or firmware was changed.")


def members(archive: zipfile.ZipFile) -> dict[str, zipfile.ZipInfo]:
    result = {}; total = 0; folded = set()
    for entry in archive.infolist():
        path = PurePosixPath(entry.filename)
        if (not entry.filename or "\\" in entry.filename or path.is_absolute() or
                any(p in ("", ".", "..") for p in entry.filename.rstrip("/").split("/")) or
                len(entry.filename) > 512 or len(path.parts) > 20 or entry.flag_bits & 1 or
                stat.S_IFMT(entry.external_attr >> 16) not in (0, stat.S_IFREG, stat.S_IFDIR)):
            raise SetupError("Unsafe or unsupported path in the installation ZIP.")
        key = entry.filename.rstrip("/").casefold()
        if key in folded:
            raise SetupError("Duplicate/case-conflicting ZIP paths; installation stopped.")
        folded.add(key)
        if not entry.is_dir():
            result[entry.filename] = entry
            total += entry.file_size
        if len(folded) > 2048 or total > MAX_UNPACK or entry.file_size < 0:
            raise SetupError("Installation ZIP exceeds its safe extraction limits.")
    # Reject file/parent collisions even if an archive lists children first.
    file_keys = {name.casefold() for name in result}
    for name in result:
        if any(str(parent).casefold() in file_keys for parent in PurePosixPath(name).parents if str(parent) != "."):
            raise SetupError("ZIP file/directory paths conflict.")
    return result


def extract_verified(archive: Path, state: Path) -> tuple[Path, dict]:
    """No downloaded Python/shell code is executed; only our embedded helpers run."""
    import posctl
    from pos_updates import validate_package
    verify_checksum(archive)
    unpack = state / "unpacked"
    unpack.mkdir(exist_ok=True)
    target = Path(tempfile.mkdtemp(prefix="bundle-", dir=unpack))
    with zipfile.ZipFile(archive) as z:
        items = members(z)
        if "SHA256SUMS" not in items or items["SHA256SUMS"].file_size > 256*1024:
            raise SetupError("The download is not a complete POS installation bundle.")
        expected = {}
        for line in z.read("SHA256SUMS").decode().splitlines():
            match = re.fullmatch(r"([0-9a-f]{64})  (.+)", line)
            if not match or match[2] in expected or match[2] not in items or match[2] == "SHA256SUMS":
                raise SetupError("Invalid bundle checksum list.")
            expected[match[2]] = match[1]
        if set(expected) != set(items) - {"SHA256SUMS"}:
            raise SetupError("Bundle has unchecked/missing files; installation stopped.")
        for name, info in items.items():
            out = target / name; out.parent.mkdir(parents=True, exist_ok=True)
            h = hashlib.sha256()
            with z.open(info) as source, out.open("xb") as dest:
                while chunk := source.read(65536):
                    h.update(chunk); dest.write(chunk)
            if name != "SHA256SUMS" and h.hexdigest() != expected[name]:
                raise SetupError("Bundle file failed verification: " + name)
    package = target / "card/pos-install"
    manifest_path = package / "manifest.pos"
    if not manifest_path.is_file() or manifest_path.stat().st_size > 32768:
        raise SetupError("No compatible SD installation manifest found. Download the current pos-setup executable.")
    manifest = json.loads(manifest_path.read_text())
    validate_package(manifest, posctl)
    firmware = package / "firmware.bin"
    if posctl.inspect_image(firmware) != manifest["version"]:
        raise SetupError("Runtime version and SD package do not match.")
    for record, file in [(manifest["firmware"], firmware), *[(r, package / "payload" / r["path"]) for r in manifest["files"]]]:
        if not file.is_file() or file.stat().st_size != record["size"] or storage.sha256(file) != record["sha256"]:
            raise SetupError("SD payload/firmware does not match its manifest.")
    expected_files = {"manifest.pos", "firmware.bin", *["payload/"+r["path"] for r in manifest["files"]]}
    if {p.relative_to(package).as_posix() for p in package.rglob("*") if p.is_file()} != expected_files:
        raise SetupError("SD package contains unexpected files; installation stopped.")
    merged = target / "pos-installer-usb.bin"
    if not merged.is_file() or not 0x10120 <= merged.stat().st_size <= 0x110000:
        raise SetupError("USB installer missing or too large; download the current pos-setup.")
    with tempfile.TemporaryDirectory(prefix="descriptor-", dir=unpack) as tmp:
        app = Path(tmp) / "installer.bin"; app.write_bytes(merged.read_bytes()[0x10000:])
        if posctl.inspect_image(app, "pos_installer", 0x100000) != manifest["version"]:
            raise SetupError("USB installer and runtime versions do not match.")
    say("Download, firmware versions and SD payload hashes verified.")
    return target, manifest


def ensure_esptool(state: Path) -> None:
    try:
        from importlib.metadata import version
        import esptool
        import serial.tools.list_ports
        if version("esptool") == ESPTOOL:
            return
    except ImportError:
        pass
    venv = state / "usb-helper"
    python = venv / "bin/python"
    if python.exists():
        checked = subprocess.run([str(python), "-c", f"import esptool,serial;from importlib.metadata import version;assert version('esptool')=='{ESPTOOL}'"], capture_output=True)
        if not checked.returncode:
            os.execv(str(python), [str(python), str(Path(sys.argv[0]).absolute()), *sys.argv[1:]])
    say("USB flashing needs a small Python helper (esptool). It does not need ESP-IDF or a compiler.")
    if not ask("Download/install the USB helper in POS's private folder?", default=True):
        raise SetupError("USB setup cancelled; you can still prepare the SD card without it.")
    created = subprocess.run([sys.executable, "-m", "venv", str(venv)], capture_output=True, text=True)
    if created.returncode:
        install_linux_tools(["python3-venv"])
        subprocess.run([sys.executable, "-m", "venv", str(venv)], check=True)
    subprocess.run([str(python), "-m", "pip", "--isolated", "install", "--index-url", "https://pypi.org/simple", "esptool==" + ESPTOOL], check=True)
    say("USB helper ready. Restarting the wizard with it...")
    os.execv(str(python), [str(python), str(Path(sys.argv[0]).absolute()), *sys.argv[1:]])


def serial_port() -> str:
    from serial.tools import list_ports
    while True:
        ports = [p for p in list_ports.comports() if re.fullmatch(r"/dev/tty(?:ACM|USB)[0-9]+", p.device)]
        if ports:
            labels = [p.device + " — " + (p.description or "USB serial connection") for p in ports]
            index = choose("Connect Panputer with a USB DATA cable. Which connection is it?", labels + ["Rescan USB connections"])
            if index == len(ports):
                continue
            port = ports[index].device
            if not os.access(port, os.R_OK | os.W_OK):
                raise SetupError("Linux cannot access " + port + ". On Debian/Ubuntu run: sudo usermod -aG dialout \"$USER\" ; then log out/in (or restart Linux on Chromebook) and rerun. Do not run the whole wizard with sudo.")
            return port
        say("\nNo Cardputer USB connection is visible yet.")
        say("Use a data-capable cable, close serial monitors, and reconnect Panputer.")
        if crostini():
            say("ChromeOS: Settings → Developers → Linux development environment → Manage USB devices. Share the Cardputer with Linux if it is listed. Some ChromeOS versions/devices do not support this; use a regular Linux computer if it cannot be shared.")
        say("If necessary, use the ADV's documented G0/BOOT + power/reset procedure to enter USB download mode.")
        if not ask("Rescan after reconnecting?", default=True):
            raise SetupError("USB flashing paused. The downloaded package is kept. SD-only preparation is available in the main menu.")


def usb_output_lines(stream):
    """Esptool progress erases with backspaces, often without any newline."""
    pending = ""
    while char := stream.read(1):
        if char in "\b\r\n" or len(pending) >= 4096:
            if pending.strip():
                yield pending.strip()
            pending = ""
        if char not in "\b\r\n" and (char == "\t" or char.isprintable()):
            pending += char
    if pending.strip():
        yield pending.strip()


def hardware_command_live(port: str, *arguments: str) -> str:
    from posctl import USBCommandError
    process = subprocess.Popen([sys.executable, "-u", "-m", "esptool", "--chip", "esp32s3", "--port", port, *arguments],
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace", bufsize=1)
    output = ""; displayed = 0.0
    try:
        for line in usb_output_lines(process.stdout):
            output = (output + line + "\n")[-65536:]
            if "%" not in line or time.monotonic()-displayed >= 1 or re.search(r"\b100\s*%", line):
                say("  " + line)
                if "%" in line:
                    displayed = time.monotonic()
        if process.wait():
            raise USBCommandError("USB operation failed. Check the data cable and Linux USB sharing.\n" + output[-2500:])
        return output
    finally:
        if process.poll() is None:
            process.kill(); process.wait()
        process.stdout.close()


@contextmanager
def live_usb():
    import posctl
    original = posctl.hardware_command
    posctl.hardware_command = hardware_command_live
    try:
        yield
    finally:
        posctl.hardware_command = original


def saved_backups(folder: Path, device_id: str) -> list[Path]:
    import posctl
    result = []
    for metadata in folder.glob("*.bin.json"):
        backup = metadata.with_suffix("")
        try:
            if metadata.is_symlink() or metadata.stat().st_size>32768 or backup.is_symlink() or not backup.is_file():
                continue
            data = json.loads(metadata.read_text())
            if not isinstance(data,dict):
                continue
            size = data.get("bytes")
            if (data.get("format")==1 and data.get("chip")=="esp32s3" and data.get("device_id")==device_id and
                data.get("plain_development_flash") is True and data.get("readback_verified") is True and
                type(size) is int and 4*1024*1024<=size<=128*1024*1024 and not size&(size-1) and
                backup.stat().st_size==size and posctl.sha256(backup)==data.get("sha256")):
                result.append((backup.stat().st_mtime, backup))
        except (OSError, ValueError, TypeError):
            continue  # A damaged/unknown backup is preserved, never offered as good.
    return [p for _,p in sorted(result)]


def backup_pos_version(backup: Path) -> str | None:
    """Recognize POS in its factory slot; never claim unknown firmware is stock."""
    with backup.open("rb") as stream:
        stream.seek(0x10000)
        header = stream.read(288)
    if (len(header) != 288 or header[0] != 0xe9 or header[12:14] != b"\x09\x00" or
            header[32:36] != b"\x32\x54\xcd\xab"):
        return None
    project = header[80:112].split(b"\0", 1)[0]
    if project not in (b"pos_installer", b"pos_runtime"):
        return None
    version = header[48:80].split(b"\0", 1)[0].decode("ascii", errors="replace")
    return version if re.fullmatch(r"[0-9][A-Za-z0-9._-]{0,30}", version) else "unknown version"


def recovery_backup_label(backup: Path) -> str:
    version = backup_pos_version(backup)
    if version:
        return backup.name + " — POS " + version + " snapshot (not factory firmware)"
    proof = Path(str(backup) + ".pos-setup-recovery.json")
    try:
        if not proof.is_symlink() and proof.stat().st_size <= 32768:
            observation = json.loads(proof.read_text())
            metadata = json.loads(Path(str(backup) + ".json").read_text())
            if (isinstance(observation, dict) and isinstance(metadata, dict) and
                    metadata.get("sha256") and metadata.get("device_id") and
                    observation.get("factory_boot_observed") is True and
                    observation.get("backup_sha256") == metadata.get("sha256") and
                    observation.get("device_id") == metadata.get("device_id")):
                return backup.name + " — original recovery backup; factory boot previously confirmed"
    except (OSError, ValueError, TypeError):
        pass
    return backup.name + " — saved software; original/factory identity needs confirmation"


def backup_and_recovery(state: Path) -> tuple[str, Path]:
    import posctl
    port = serial_port()
    if not ask("Is this the M5Stack Cardputer ADV (not the original Cardputer)?"):
        raise SetupError("Only Cardputer ADV is supported by this installer.")
    say("Reading device information. This does not change its flash.")
    with live_usb():
        probe = posctl.hardware_probe(port)
    if not probe.get("device_id") or not probe.get("plain_development_flash"):
        raise SetupError("Device identity/security state is unsupported. No firmware will be written; use the correct vendor recovery instructions.")
    folder = private_directory(state / "backups")
    previous = saved_backups(folder, probe["device_id"])
    if previous:
        say("For factory recovery, select the earliest ORIGINAL pre-POS backup. Making a new snapshot copies whatever is on the device now; it does not install the downloaded POS firmware.")
    choice = choose("Choose the ORIGINAL recovery backup (earliest first)", [recovery_backup_label(p) for p in previous]+["Make a NEW snapshot of current software (may already be POS)"]) if previous else 0
    if choice < len(previous):
        backup = previous[choice]
        say("Using your existing verified backup: " + str(backup))
    else:
        backup = folder / ("panputer-" + probe["device_id"].replace(":", "") + "-" + str(time.time_ns()) + ".bin")
        say("Making a private full-flash backup of the software currently on your Cardputer. The entire flash is read twice and the copies must match. This is the manufacturer's factory software only if it is still installed.")
        with live_usb():
            try:
                posctl.factory_backup(port, backup)
            except (ValueError, OSError) as error:
                raise SetupError("Backup did not complete. POS has NOT been flashed; no restore/write was attempted.\n"
                                 "Incomplete files are kept at " + str(backup) + ".* and cannot be used as verified backups.\n"
                                 "If the screen stays black after this stopped command: unplug USB, switch OFF, release G0/all keys, "
                                 "wait 10 seconds, then switch ON. Reconnect/share USB with Linux before retrying.\n" + str(error)) from error
        say("Verified backup saved: " + str(backup))
        say("Keep this .bin AND its .json file on your computer. They are never saved onto the card being erased.")
    metadata = json.loads(Path(str(backup)+".json").read_text())
    if metadata.get("device_id") != probe["device_id"]:
        raise SetupError("The connected Cardputer changed during backup. Backup retained; reconnect the correct device and rerun.")
    version = backup_pos_version(backup)
    if version:
        raise SetupError("This backup contains POS " + version + ", not the original factory software. "
                         "It is safely kept as a POS snapshot, but cannot prove factory recovery. "
                         "Rerun Flash Cardputer only and select the earliest pre-POS backup instead. No restore or POS installer flash was attempted.")
    say("Next, restore the selected saved software and confirm the ORIGINAL manufacturer software boots. The NEW POS USB installer is flashed AFTER this recovery check; downloaded SD files do not change the current firmware.")
    if not ask("Restore the verified original backup now?", default=True):
        raise SetupError("POS flashing paused. Your backup is kept; complete recovery verification before installing.")
    with live_usb():
        posctl.factory_restore(port, backup, True)
    say("\nSaved software restored and readback verified. The NEW POS installer has NOT been flashed yet. Power-cycle the ADV if needed.")
    if not ask("Can you SEE the original M5Stack factory software (not the POS Installer menu)?"):
        raise SetupError("Factory boot was not confirmed. POS has not been flashed; keep the backup and resolve recovery first.")
    report = {"format": 1, "device_id": probe["device_id"], "backup_sha256": posctl.sha256(backup),
              "factory_boot_observed": True, "basis": "operator observation", "utc": int(time.time())}
    report_path = Path(str(backup) + ".pos-setup-recovery.json")
    if report_path.exists() or report_path.is_symlink():
        # A retry can reuse the same known-good backup. Keep earlier evidence
        # and record this fresh operator observation separately.
        report_path = Path(str(backup) + f".pos-setup-recovery-{time.time_ns()}.json")
    with report_path.open("x") as f:
        os.chmod(report_path, 0o600); json.dump(report, f, indent=2); f.write("\n"); f.flush(); os.fsync(f.fileno())
    return port, backup


def prepare_card(package: Path, state: Path, explicit_folder: Path | None = None) -> None:
    protect = (state, state/"backups", package, Path(sys.argv[0]).absolute())
    while True:
        raw = []
        if not explicit_folder:
            try:
                raw = storage.candidates(storage.inventory(protect))
            except (SetupError, OSError, ValueError) as error:
                say("Direct card access unavailable: " + str(error))
        shared = [explicit_folder] if explicit_folder else storage.shared_cards()
        labels = [storage.description(d) for d in raw] + [str(p) + " — shared/mounted folder (copy only)" for p in shared]
        index = choose("Select your microSD card. Internal/system disks are excluded.", labels + ["Choose a card folder manually", "Rescan cards"])
        if index == len(labels) + 1:
            continue
        if index >= len(raw):
            card = shared[index-len(raw)] if index < len(labels) else Path(input("Card's top-level folder: ").strip()).expanduser()
            storage.card_folder_type(card)
            say("Folder mode does not format or erase the card.")
            say("Crostini cannot determine the physical filesystem behind a shared folder. POS needs FAT32, not exFAT/NTFS.")
            if not ask("Is this the microSD's TOP-LEVEL folder, and have you confirmed it is FAT32?"):
                raise SetupError("Card not changed. Format it to FAT32 in ChromeOS if that option is available, or use pos-setup on a Linux computer with direct card access. Then share the card with Linux and retry.")
            storage.copy_package(package, card)
            say("Use Eject in your computer's Files app before removing the card.")
            return
        disk = raw[index]
        say("\nSelected: " + storage.description(disk))
        current = storage.check_identity(disk, protect)
        parts = [n for n in current.get("children", []) if n.get("type") == "part"]
        fat32 = len(parts) == 1 and parts[0].get("fstype") == "vfat" and str(parts[0].get("fsver", "")).upper() == "FAT32"
        mode = choose("Prepare this card", (["Keep existing FAT32 files and add the installer"] if fat32 else []) + ["Format card as FAT32 (ERASES EVERYTHING on this card)", "Choose another card"])
        if mode == (2 if fat32 else 1):
            continue
        if not fat32 or mode == 1:
            needed = [n for n in ("sfdisk", "mkfs.fat", "udisksctl") if not shutil.which(n) and not any((Path(p)/n).is_file() for p in ("/usr/sbin", "/sbin"))]
            if needed:
                install_linux_tools(["dosfstools", "fdisk", "udisks2", "util-linux"])
            say("\nWARNING: Formatting will erase EVERY file and partition on this selected card.")
            say(storage.description(disk))
            confirmation = input("Type exactly ERASE " + disk["path"] + " to erase this card (anything else cancels): ").strip()
            partition = storage.format_card(disk, protect, confirmation)
        else:
            partition = parts[0]
        folder = storage.mount_partition(disk, partition, protect)
        storage.copy_package(package, folder, check=lambda:storage.check_identity(disk, protect))
        try:
            current = storage.check_identity(disk, protect)
            for part in storage.descendants(current):
                if storage.mounts(part):
                    storage.run([storage.system_tool("udisksctl"), "unmount", "--block-device", part["path"]])
            say("Card safely unmounted. It is ready to remove.")
        except (SetupError, OSError) as error:
            say("Files are verified. Use Eject in Files before removal: " + str(error))
        return


def flash(bundle: Path, state: Path, port: str, backup: Path) -> None:
    import posctl, pos_deploy
    say("\nPower OFF Panputer, put the prepared microSD into it, then reconnect USB.")
    input("Press Enter once the card is inserted and the Cardputer is connected... ")
    port = serial_port()  # Re-enumeration/download mode can change the port name.
    say("Flashing replaces the internal bootloader, partition table, NVS and installer. Your verified original backup is kept.")
    if input("Type FLASH to install the POS USB installer: ").strip() != "FLASH":
        raise SetupError("Flash cancelled. The prepared card and original backup are kept.")
    image = bundle / "pos-installer-usb.bin"
    version = json.loads((bundle / "card/pos-install/manifest.pos").read_text())["version"]
    say("NOW flashing the new POS " + version + " USB installer. This is separate from preparing the microSD or restoring your backup.")
    args = argparse.Namespace(confirm_flash=True, image=image, sha256=posctl.sha256(image), factory_backup=backup, port=port)
    say("Writing the POS installer over USB and verifying flash readback...")
    with live_usb():
        pos_deploy.flash(args, posctl)
    say("\nNEW POS " + version + " installer USB flash and readback completed. The on-device POS installation is NEXT:")
    say("  1. Power Panputer off and on, leaving its microSD inserted.")
    say("  2. On its screen choose 'Install POS / resume' with Up/Down and Enter.")
    say("     ADV arrows: hold Fn with ; (Up) or . (Down).")
    say("  3. Type INSTALL and press Enter. Leave power/card connected while it works.")
    say("  4. When it says installation is complete, power off and on again.")
    say("  5. Set your name/preferences; Wi-Fi can be skipped. Leave this SD inserted for normal use.")
    say("Your backup: " + str(backup))
    say("Full instructions: " + str(bundle / "INSTALL.md"))


def doctor(state: Path) -> None:
    say("POS Setup " + VERSION + " — read-only access check")
    say("Environment: " + ("ChromeOS Linux / Crostini" if crostini() else "Linux"))
    say("Python: " + sys.version.split()[0])
    say("GitHub CLI: " + ("available" if shutil.which("gh") else "not installed (needed only for private-repo sign-in)"))
    say("Detected serial paths: " + (", ".join(str(p) for pattern in ("ttyACM*", "ttyUSB*") for p in Path("/dev").glob(pattern)) or "none"))
    try:
        snapshot = storage.inventory((state, Path(sys.argv[0]).absolute()))
        usable = storage.candidates(snapshot)
        say("Directly accessible candidate SD/USB disks: " + str(len(usable)))
        for d in usable:
            say("  " + storage.description(d))
    except (SetupError, OSError, ValueError) as error:
        say("Direct disk discovery: " + str(error))
    folders = storage.shared_cards()
    say("Shared/mounted card folders: " + (", ".join(map(str, folders)) or "none"))
    if crostini():
        say("Raw-card formatting is often unavailable in Crostini. In ChromeOS Files, right-click the SD → Share with Linux. Folder mode copies only; it cannot convert exFAT to FAT32. USB forwarding also depends on ChromeOS support.")
    say("No downloads, installs, formatting, mounts or flash writes were performed.")


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="Guided POS installer for Linux and Chromebook Linux. Run without arguments for the menu.")
    parser.add_argument("--version", action="version", version="POS Setup " + VERSION)
    parser.add_argument("--doctor", action="store_true", help="Read-only check of Linux/Crostini card/USB access")
    parser.add_argument("--download-only", action="store_true", help="Download and verify the latest published POS, including development releases")
    parser.add_argument("--release", help="Choose a specific release, e.g. v0.5.8-dev")
    parser.add_argument("--bundle", type=Path, help="Use an already downloaded POS ZIP plus matching .sha256 file")
    parser.add_argument("--sd-folder", type=Path, help="Offer this mounted/shared card folder (copy only; never formats)")
    parser.add_argument("--work-dir", type=Path, default=Path.home()/".local/share/pos-setup", help="Private download/backup folder; default ~/.local/share/pos-setup")
    args = parser.parse_args(argv)
    try:
        if not sys.platform.startswith("linux"):
            raise SetupError("This tool is for Linux, including Chromebook's Linux development environment.")
        if args.doctor:
            doctor(args.work_dir)
            return 0
        if os.geteuid() == 0:
            raise SetupError("Run pos-setup as your normal user, without sudo. It requests admin access only for support packages/card formatting.")
        if not args.download_only and not sys.stdin.isatty():
            raise SetupError("Open a terminal and run ./pos-setup interactively. Formatting/flashing cannot be approved through a pipe or --yes flag.")
        state = private_directory(args.work_dir.expanduser())
        say("\nPOS Setup — guided Panputer installation")
        say("Cardputer ADV only. Firmware goes over USB; the microSD holds the POS installation files.")
        if args.download_only:
            archive, version = download_release(GitHub(), state, args.release) if not args.bundle else (args.bundle.expanduser(), None)
            bundle, manifest = extract_verified(archive, state)
            if version and manifest["version"] != version:
                raise SetupError("Downloaded release tag and firmware version differ; installation stopped.")
            say("Verified POS " + manifest["version"] + ": " + str(bundle))
            return 0
        action = choose("What would you like to do?", ["Install POS: prepare microSD + flash Cardputer", "Prepare the microSD only", "Flash Cardputer only (microSD already prepared)", "Restore a saved backup (original firmware or POS snapshot)", "Show connection help / read-only doctor"])
        if action == 4:
            doctor(state)
            return 0
        if action != 1:
            ensure_esptool(state)
        if action == 3:
            import posctl
            port = serial_port()
            with live_usb():
                identity = posctl.hardware_probe(port)
            folder = private_directory(state/"backups")
            available = saved_backups(folder, identity.get("device_id", ""))
            index = choose("Restore saved software for this Cardputer", [recovery_backup_label(p) for p in available]+["Choose a .bin file from another folder"])
            backup = available[index] if index<len(available) else Path(input("Path to the saved .bin (keep its .json beside it): ").strip()).expanduser()
            if ask("Restore the selected saved software to the matching Cardputer? This replaces its internal flash; it does not install the downloaded POS release."):
                with live_usb():
                    posctl.factory_restore(port, backup, True)
                say("Selected backup restored and readback verified. The downloaded POS installer was not flashed. Power-cycle and observe the saved software boot.")
            return 0
        archive, version = download_release(GitHub(interactive=True), state, args.release) if not args.bundle else (args.bundle.expanduser(), None)
        bundle, manifest = extract_verified(archive, state)
        if version and manifest["version"] != version:
            raise SetupError("Downloaded release tag and firmware version differ; installation stopped.")
        if action in (0, 2):
            import posctl
            # Full image checksum/digest validation before card erasure or any USB write.
            import pos_deploy
            pos_deploy.verify_installer(bundle/"pos-installer-usb.bin",posctl.sha256(bundle/"pos-installer-usb.bin"),posctl)
            posctl.verify_image(bundle/"card/pos-install/firmware.bin")
            with tempfile.TemporaryDirectory(prefix="image-check-", dir=state) as tmp:
                app = Path(tmp)/"installer.bin"; app.write_bytes((bundle/"pos-installer-usb.bin").read_bytes()[0x10000:])
                posctl.verify_image(app)
            port, backup = backup_and_recovery(state)
        if action in (0, 1):
            prepare_card(bundle/"card/pos-install", state, args.sd_folder)
        if action in (0, 2):
            flash(bundle, state, port, backup)
        else:
            say("\nSD preparation complete. Next run pos-setup → Flash Cardputer only, or use a Linux computer with USB access. On-device install steps are in " + str(bundle/"INSTALL.md"))
        return 0
    except (SetupError, ValueError, OSError, KeyError, zipfile.BadZipFile, subprocess.CalledProcessError) as error:
        say("\nPOS Setup stopped: " + str(error))
        say("Completed backups/downloads and incomplete card copies are kept. Fix the issue and rerun; do not delete your only backup.")
        return 1
    except (KeyboardInterrupt, EOFError):
        say("\nCancelled. Keep your backup and any partial files; no incomplete operation is marked successful.")
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
