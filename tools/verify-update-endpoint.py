#!/usr/bin/env python3
"""Read back HTTPS release bytes and compare against locally signed artifacts."""
import argparse
import base64
import hashlib
import json
from pathlib import Path
from urllib.request import urlopen
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey
p = argparse.ArgumentParser(description=__doc__)
p.add_argument("release", type=Path)
a = p.parse_args()
local = (a.release / "manifest.json").read_bytes()
expected = json.loads(base64.b64decode(json.loads(local)["manifest"]))
base = "https://karotech.io/updates/uFlow/"
with urlopen(base + expected["channel"] + ".json", timeout=30) as response:
    envelope = response.read(32769)
assert envelope == local, "Published channel differs from prepared release"
decoded = json.loads(envelope)
key = base64.b64decode((a.release / "public-key.txt").read_text().strip())
Ed25519PublicKey.from_public_bytes(key).verify(base64.b64decode(decoded["signature"]), base64.b64decode(decoded["manifest"]))
for component in expected["components"]:
    sha = hashlib.sha256()
    size = 0
    with urlopen(base + component["url"], timeout=60) as response:
        while data := response.read(256 * 1024):
            sha.update(data)
            size += len(data)
    assert size == component["size"] and sha.hexdigest() == component["sha256"]
print("PASS: HTTPS channel, signature and full binary readback", expected["release"])
