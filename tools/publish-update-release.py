#!/usr/bin/env python3
"""Publish a prepared software release using strict SSH and atomic channel promotion."""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import re

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("release", type=Path)
p.add_argument("--identity", type=Path, required=True)
a = p.parse_args()
config = json.loads((Path(__file__).resolve().parents[1] / "updates/server.json").read_text())
manifest_path = a.release / "manifest.json"
envelope = json.loads(manifest_path.read_bytes())
body = base64.b64decode(envelope["manifest"], validate=True)
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey
public = base64.b64decode((a.release / "public-key.txt").read_text().strip(), validate=True)
Ed25519PublicKey.from_public_bytes(public).verify(base64.b64decode(envelope["signature"], validate=True), body)
m = json.loads(body)
assert re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}", m["release"])
assert m["channel"] in config["channels"] and m["base_url"] == config["base_url"]
for item in m["components"]:
    assert re.fullmatch(r"[a-f0-9]{64}", item["sha256"])
    data = (a.release / "files" / item["sha256"]).read_bytes()
    assert len(data) == item["size"] and hashlib.sha256(data).hexdigest() == item["sha256"]
host = config["ssh_user"] + "@" + config["ssh_host"]
options = ["-o", "BatchMode=yes", "-o", "StrictHostKeyChecking=yes", "-i", str(a.identity)]
def ssh(command):
    subprocess.run(["ssh", *options, host, command], check=True)
q = shlex.quote
root = config["remote_root"]
staging = "/var/tmp/uflow-upload-" + m["release"]
destination = root + "/releases/" + m["release"]
ssh(f"test ! -e {q(staging)} && test ! -e {q(destination)} && mkdir -p {q(root + '/releases')}")
subprocess.run(["scp", *options, "-r", str(a.release.resolve()), host + ":" + staging], check=True)
# Verify every uploaded byte before exposing the immutable directory.
checks = []
for local in a.release.rglob("*"):
    if local.is_file():
        relative = local.relative_to(a.release).as_posix()
        sha = hashlib.sha256(local.read_bytes()).hexdigest()
        checks.append(f"echo {q(sha + '  ' + staging + '/' + relative)} | sha256sum -c -")
channel = root + "/" + m["channel"] + ".json"
ssh("set -e; " + "; ".join(checks) + f"; chmod -R a+rX {q(staging)}; mv -T {q(staging)} {q(destination)}; cp {q(destination + '/manifest.json')} {q(channel + '.tmp')}; mv -T {q(channel + '.tmp')} {q(channel)}")
print("Published", m["release"], "to", config["base_url"] + m["channel"] + ".json")
