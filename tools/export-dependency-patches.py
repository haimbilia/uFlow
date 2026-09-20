#!/usr/bin/env python3
"""Export local dependency changes without committing nested repositories."""
from pathlib import Path
import subprocess
ROOT = Path(__file__).resolve().parents[1]
for folder, name in [("emulators/NooDS", "noods")]:
    patch = subprocess.check_output(["git", "-C", str(ROOT / folder), "diff", "--binary", "HEAD"])
    (ROOT / "emulators" / (name + ".patch")).write_bytes(patch)
