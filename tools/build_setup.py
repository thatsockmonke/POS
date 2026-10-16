#!/usr/bin/env python3
"""Build a Python 3 executable zipapp with all POS helpers, no third-party code."""
import argparse
import os
from pathlib import Path
import shutil
import tempfile
import zipapp

ROOT = Path(__file__).resolve().parent


def build(output: Path) -> None:
    if output.exists() or output.is_symlink() or Path(str(output)+".sha256").exists():
        raise ValueError("Executable/checksum output already exists; choose a fresh path")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="pos-setup-build-") as tmp:
        source = Path(tmp)/"source"; source.mkdir()
        for name in ("pos_setup.py", "pos_setup_storage.py", "posctl.py", "pos_deploy.py", "pos_updates.py", "pos_validation.py"):
            shutil.copyfile(ROOT/name, source/name)
        (source/"__main__.py").write_text('import sys\nif sys.version_info < (3,10):\n    sys.exit("POS Setup needs Python 3.10 or newer. Update your Linux environment.")\nfrom pos_setup import main\nsys.exit(main())\n')
        app = Path(tmp)/"pos-setup"
        zipapp.create_archive(source, app, interpreter="/usr/bin/env python3", compressed=True)
        with app.open("rb") as src, output.open("xb") as dest:
            shutil.copyfileobj(src, dest); dest.flush(); os.fsync(dest.fileno())
    output.chmod(0o755)
    import hashlib
    with Path(str(output)+".sha256").open("x") as f:
        f.write(hashlib.sha256(output.read_bytes()).hexdigest()+"  "+output.name+"\n")


if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output",type=Path,required=True)
    build(parser.parse_args().output)
