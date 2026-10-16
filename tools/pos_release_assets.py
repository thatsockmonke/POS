#!/usr/bin/env python3
"""Attach signed update downloads, preserving all already published bytes."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile


def attach(tag: str, directory: Path, names: list[str]) -> None:
    if not re.fullmatch(r'v[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.-]+)?', tag):
        raise ValueError('Invalid POS tag')
    for name in names:
        if not re.fullmatch(r'pos-update-[0-9A-Za-z.-]+\.zip(?:\.sha256)?', name):
            raise ValueError('Invalid update asset name')
        if (directory/name).is_symlink() or not (directory/name).is_file():
            raise ValueError('Expected a regular update asset')
    assets = json.loads(subprocess.run(['gh', 'release', 'view', tag, '--json', 'assets'],
                         check=True, capture_output=True, text=True).stdout)['assets']
    existing = {asset['name'] for asset in assets}
    missing = []
    with tempfile.TemporaryDirectory(prefix='pos-release-asset-') as temp:
        for name in names:
            if name not in existing:
                missing.append(str(directory/name)); continue
            subprocess.run(['gh', 'release', 'download', tag, '--pattern', name, '--dir', temp], check=True)
            if hashlib.sha256((Path(temp)/name).read_bytes()).digest() != hashlib.sha256((directory/name).read_bytes()).digest():
                raise ValueError('Published update asset differs; existing downloads were preserved')
    if missing:
        subprocess.run(['gh', 'release', 'upload', tag, *missing], check=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tag', required=True)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--name', action='append', required=True)
    args = parser.parse_args()
    attach(args.tag, args.directory, args.name)
