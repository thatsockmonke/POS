#!/usr/bin/env python3
"""Publish signed OTA files from an exact, verified POS release bundle.

Private signing keys stay outside Git. CI supplies POS_UPDATE_SIGNING_KEY through
an encrypted repository secret; this helper accepts a mode-0600 key file too.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from types import SimpleNamespace
import zipfile

import posctl
import pos_deploy
import pos_setup
import pos_updates

ROOT = Path(__file__).resolve().parents[1]
LICENSE_FILES = ("THIRD_PARTY.md", "ESP-IDF-LICENSE.txt", "licenses/minimp3-CC0.txt",
    "licenses/Semtech-Clear-BSD.txt", "licenses/M5Stack-MIT.txt", "licenses/NimBLE-LICENSE.txt",
    "licenses/NimBLE-NOTICE.txt", "licenses/TinyCrypt-LICENSE.txt")


def fingerprint(public: bytes) -> str:
    der = subprocess.run(['openssl', 'pkey', '-pubin', '-outform', 'DER'],
                         input=public, check=True, capture_output=True).stdout
    return hashlib.sha256(der).hexdigest()


def configuration(root: Path) -> dict:
    root = root.resolve(strict=True)
    for path in (root/'updates', root/'updates/publisher.json', root/'updates/public-key.pem', root/'updates/releases', root/'updates/manifest.json', root/'updates/latest.json'):
        if path.is_symlink() or not path.resolve().is_relative_to(root):
            raise ValueError('Publisher paths must stay inside the checkout without symlinks')
    config = json.loads((root/'updates/publisher.json').read_text())
    if (config.get('format') != 1 or
            not re.fullmatch(r'[A-Za-z0-9_-]+/[A-Za-z0-9_.-]+', config.get('repository', '')) or
            not re.fullmatch(r'[A-Za-z0-9_-]+', config.get('branch', '')) or
            config.get('manifest') != 'updates/latest.json' or
            fingerprint((root/'updates/public-key.pem').read_bytes()) != config.get('fingerprint')):
        raise ValueError('Invalid publisher configuration or trusted public key')
    return config


def signed_directory(directory: Path, public: Path):
    """Verify signature and every firmware/System file, never just the channel JSON."""
    return pos_deploy.validate_update(directory, public, posctl)[1]


def expected_base(config: dict, version: str) -> str:
    return (f"https://raw.githubusercontent.com/{config['repository']}/"
            f"{config['branch']}/updates/releases/{version}/")


def validate_feed(root: Path, legacy: bool = False) -> dict:
    config = configuration(root)
    channel = root/('updates/manifest.json' if legacy else config['manifest'])
    if not legacy and not channel.exists() and not channel.is_symlink():
        channel = root/'updates/manifest.json'  # Import the last pre-0.6 sequence.
    if not channel.is_file() or channel.is_symlink():
        raise ValueError('Signed update channel is missing or is a symlink')
    if channel.stat().st_size > 32768:
        raise ValueError('Signed update channel is too large')
    envelope = json.loads(channel.read_bytes())
    # Use untrusted release only for a validated, confined directory name;
    # signed_directory then verifies all metadata before it is trusted.
    release = json.loads(envelope['payload'])['release']
    pos_updates.version(release)
    directory = root/'updates/releases'/release
    if directory.is_symlink() or not directory.is_dir():
        raise ValueError('Channel has no corresponding release directory')
    data = signed_directory(directory, root/'updates/public-key.pem')
    if pos_updates.compare(release, pos_updates.TRANSITION_RELEASE) > 0 and 'minimum_version' in data:
        raise ValueError('Updates after 0.6.0-dev must omit the obsolete minimum_version field')
    if (channel.read_bytes() != (directory/'manifest.json').read_bytes() or
            data['release'] != release or data['base_url'] != expected_base(config, release)):
        raise ValueError('Channel and immutable signed release do not match')
    return data


def verified_bundle(archive: Path, state: Path):
    try:
        return pos_setup.extract_verified(archive, state)
    except pos_setup.SetupError as exc:
        raise ValueError(str(exc)) from exc


def write_channel(channel: Path, data: bytes) -> None:
    temporary = channel.with_suffix('.json.part')
    if channel.is_symlink() or temporary.exists() or temporary.is_symlink():
        raise ValueError('Interrupted or symlinked channel write preserved; inspect before retrying')
    with temporary.open('xb') as stream:
        stream.write(data)
        stream.flush(); os.fsync(stream.fileno())
    os.replace(temporary, channel)


def publish(root: Path, archive: Path, private: Path) -> dict:
    config = configuration(root)
    if private.is_symlink() or not private.is_file() or private.stat().st_mode & 0o077:
        raise ValueError('Signing key must be a regular private file (chmod 600) outside Git')
    if private.resolve().is_relative_to(root.resolve()):
        raise ValueError('Keep the private signing key outside the source checkout')
    public = subprocess.run(['openssl', 'pkey', '-in', str(private), '-pubout'],
                            check=True, capture_output=True).stdout
    if fingerprint(public) != config['fingerprint']:
        raise ValueError('Signing key differs from the key trusted by firmware')
    previous = validate_feed(root) if (root/config['manifest']).exists() or (root/'updates/manifest.json').exists() else None
    with tempfile.TemporaryDirectory(prefix='pos-release-update-') as work:
        state = Path(work)
        bundle, manifest = verified_bundle(archive, state)
        release = manifest['version']
        if archive.name != f'pos-{release}.zip':
            raise ValueError('Release archive filename and firmware version differ')
        pos_updates.version(release)
        future = pos_updates.compare(release, pos_updates.TRANSITION_RELEASE) > 0
        if future:
            bridge = validate_feed(root, legacy=True)
            if bridge['release'] != pos_updates.TRANSITION_RELEASE or 'minimum_version' not in bridge:
                raise ValueError('Publish the required 0.6.0-dev transition release first')
        destination = root/'updates/releases'/release
        if destination.exists() or destination.is_symlink():
            existing = signed_directory(destination, root/'updates/public-key.pem')
            if (existing['package'] != manifest or existing['release'] != release or
                    existing['base_url'] != expected_base(config, release)):
                raise ValueError('Published version already exists with different bytes; bump the version')
            # Retry after an interrupted channel write, but never move it backward.
            if previous and pos_updates.compare(release, previous['release']) < 0:
                raise ValueError('Refusing to replace the current channel with an older release')
            if previous and previous['release'] != release and existing['sequence'] <= previous['sequence']:
                raise ValueError('Published update sequence is below the accepted channel')
            result = existing
        else:
            if previous and pos_updates.compare(release, previous['release']) <= 0:
                raise ValueError('Update versions must increase; published versions are immutable')
            sequence = previous['sequence'] + 1 if previous else 1
            install = bundle/'card/pos-install'
            stage = state/'signed'
            args = SimpleNamespace(kind='firmware', release=release,
                sequence=sequence,
                base_url=expected_base(config, release), private_key=private,
                firmware=install/'firmware.bin', payload=install/'payload', output=stage)
            pos_updates.make_update(args, posctl)
            for name in LICENSE_FILES:
                source = bundle/name
                if not source.is_file() or source.is_symlink():
                    raise ValueError('Release bundle is missing distribution licenses')
                target = stage/name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source, target)
            checks = ''.join(posctl.sha256(p)+'  '+p.relative_to(stage).as_posix()+'\n'
                for p in sorted(stage.rglob('*')) if p.is_file() and p.name != 'SHA256SUMS')
            (stage/'SHA256SUMS').write_text(checks)
            result = signed_directory(stage, root/'updates/public-key.pem')
            if result['package'] != manifest:
                raise ValueError('Signed update differs from the release installation bundle')
            destination.parent.mkdir(parents=True, exist_ok=True)
            # Fail rather than merge into an existing/incomplete release directory.
            shutil.copytree(stage, destination)
            signed_directory(destination, root/'updates/public-key.pem')
        write_channel(root/config['manifest'], (destination/'manifest.json').read_bytes())
        if not future:
            write_channel(root/'updates/manifest.json', (destination/'manifest.json').read_bytes())
        return validate_feed(root)


def verify_bundle(root: Path, archive: Path) -> dict:
    payload = validate_feed(root)
    with tempfile.TemporaryDirectory(prefix='pos-update-bundle-check-') as temp:
        _, manifest = verified_bundle(archive, Path(temp))
    if payload['package'] != manifest or archive.name != f"pos-{manifest['version']}.zip":
        raise ValueError('Published update does not match this exact release bundle')
    return payload


def export(root: Path, output: Path) -> None:
    payload = validate_feed(root)
    directory = root/'updates/releases'/payload['release']
    expected = {'manifest.json', 'firmware.bin', 'public-key.pem', 'SHA256SUMS'}
    expected.update('payload/'+r['path'] for r in payload['package']['files'])
    expected.update(LICENSE_FILES)
    files = {p.relative_to(directory).as_posix(): p for p in directory.rglob('*') if p.is_file()}
    if set(files) != expected or any(p.is_symlink() for p in directory.rglob('*')):
        raise ValueError('Unexpected or symlinked files in published update')
    partial = Path(str(output)+'.part')
    checksum = Path(str(output)+'.sha256')
    if any(p.exists() or p.is_symlink() for p in (output, partial, checksum)):
        raise ValueError('Update archive, checksum or partial exists; choose a fresh output')
    output.parent.mkdir(parents=True, exist_ok=True)
    # Fixed ZIP timestamps make retries byte-identical across machines/checkouts.
    with partial.open('xb') as stream:
        with zipfile.ZipFile(stream, 'w', compression=zipfile.ZIP_STORED) as archive:
            for name, source in sorted(files.items()):
                info = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_STORED
                info.external_attr = 0o100644 << 16
                with source.open('rb') as src, archive.open(info, 'w') as dest:
                    shutil.copyfileobj(src, dest, 65536)
        stream.flush(); os.fsync(stream.fileno())
    os.link(partial, output)
    partial.unlink()
    with checksum.open('x') as stream:
        stream.write(posctl.sha256(output)+'  '+output.name+'\n')


def configure_signing(root: Path, private: Path) -> None:
    config = configuration(root)
    if private.is_symlink() or not private.is_file() or private.stat().st_mode & 0o077:
        raise ValueError('Signing key must be a private regular file (chmod 600)')
    public = subprocess.run(['openssl', 'pkey', '-in', str(private), '-pubout'],
                            capture_output=True, check=True).stdout
    if fingerprint(public) != config['fingerprint']:
        raise ValueError('Key does not match the public key trusted by POS')
    with private.open('rb') as stream:
        subprocess.run(['gh', 'secret', 'set', 'POS_UPDATE_SIGNING_KEY',
                        '--repo', config['repository']], stdin=stream, check=True)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT)
    parser.add_argument('--bundle', type=Path, help='Complete release ZIP and its matching .sha256')
    parser.add_argument('--private-key', type=Path, help='Private P-256 key outside the checkout')
    parser.add_argument('--verify-bundle', type=Path, help='Verify an offline-signed feed matches this release ZIP')
    parser.add_argument('--configure-signing-key', type=Path, help='Securely install a matching local key as the GitHub Actions secret')
    parser.add_argument('--verify', action='store_true', help='Verify the current channel without writing')
    parser.add_argument('--export', type=Path, help='Export current signed update as ZIP plus checksum')
    args = parser.parse_args(argv)
    try:
        if args.configure_signing_key:
            configure_signing(args.root, args.configure_signing_key)
            print('Automatic release signing configured in GitHub Actions.')
            return 0
        if args.verify_bundle:
            payload = verify_bundle(args.root, args.verify_bundle)
        elif args.verify:
            payload = validate_feed(args.root)
        else:
            if not args.bundle:
                parser.error('--bundle is required to publish')
            if args.private_key:
                payload = publish(args.root, args.bundle, args.private_key)
            else:
                secret = os.environ.get('POS_UPDATE_SIGNING_KEY', '')
                if not secret:
                    raise ValueError('POS_UPDATE_SIGNING_KEY is not configured. Supply the repository secret or --private-key; unsigned updates are never published.')
                with tempfile.TemporaryDirectory(prefix='pos-signing-') as temp:
                    key = Path(temp)/'key.pem'
                    fd = os.open(key, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
                    with os.fdopen(fd, 'w') as f:
                        f.write(secret)
                    payload = publish(args.root, args.bundle, key)
        if args.export:
            export(args.root, args.export)
        print(f"Verified signed update: {payload['release']} (sequence {payload['sequence']})")
        return 0
    except (ValueError, OSError, KeyError, TypeError, subprocess.CalledProcessError, zipfile.BadZipFile) as exc:
        # Never echo secret PEM contents or subprocess output.
        print(f'Update publication stopped: {exc}', file=__import__('sys').stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
