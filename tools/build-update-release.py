"""Build an immutable, signed software update directory. Never bundles a ROM/base.

Requires cryptography. Inputs must be a separately prepared, distributable software
directory, not the personal native-installer bundle. Private keys stay outside it.
"""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import re
import shutil

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

ROOT = Path(__file__).resolve().parents[1]
# Exact destinations, never arbitrary paths supplied by a downloaded manifest.
# Only installed optional components will be updated by the future client.
COMPONENTS = {
    "uflow": ("wiiu/apps/uFlow/uFlow.wuhb", False),
    "raw-loader": ("wiiu/environments/aroma/modules/RPXLoadingModule.wms", True),
    "raw-kernel": ("wiiu/environments/aroma/modules/uFlowRawKernel.wms", True),
    "mocha": ("wiiu/environments/aroma/modules/setup/00_mocha.rpx", True),
    "region-free": ("wiiu/environments/aroma/plugins/regionfree.wps", True),
    "wiiflow": ("apps/wiiflow/boot.dol", False),
    "wiiflow-app-booter": ("apps/wiiflow/bins/app_booter.bin", False),
    "wiiflow-ext-loader": ("apps/wiiflow/bins/ext_loader.bin", False),
    "wiiflow-ext-booter": ("apps/wiiflow/bins/ext_booter.bin", False),
    "usbloader-gx": ("apps/usbloader_gx/boot.dol", False),
    "nintendont": ("apps/nintendont/boot.dol", False),
    "noods": ("wiiu/apps/noods/noods.wuhb", False),
}
for name in ("ds-vc", "nes-vc", "snes-vc", "gba-vc", "n64-vc", "pce-vc", "msx-vc", "noods"):
    COMPONENTS["launcher-" + name] = (f"wiiu/apps/uFlow/launchers/{name}.ufmod", False)
WIIFLOW_GROUP = {name for name in COMPONENTS if name.startswith("wiiflow")}


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def canonical(value):
    return (json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True) + "\n").encode()


def stage(artifacts, output, key_path, release, sequence, channel, names, notices, changes="Software update"):
    config = json.loads((ROOT / "updates/server.json").read_text())
    if not re.fullmatch(r"[0-9][A-Za-z0-9._-]{0,63}", release):
        raise ValueError("Invalid release identifier")
    if type(sequence) is not int or not 0 < sequence < 2**53:
        raise ValueError("Sequence must be a positive monotonically increasing integer")
    if channel not in config["channels"]:
        raise ValueError("Unknown channel")
    if not changes.strip() or len(changes.encode()) > 2048 or '\0' in changes:
        raise ValueError("Supply version changes, maximum 2048 bytes")
    if not names or len(set(names)) != len(names) or set(names) - COMPONENTS.keys():
        raise ValueError("Select distinct known software components")
    if set(names) & WIIFLOW_GROUP and not WIIFLOW_GROUP <= set(names):
        raise ValueError("WiiFlow executable and all three booters must ship together")
    artifacts, output, key_path = artifacts.resolve(), output.resolve(), key_path.resolve()
    if output.exists():
        raise ValueError("Release output exists; published releases must be immutable")
    if key_path.is_relative_to(artifacts) or key_path.is_relative_to(output):
        raise ValueError("Signing key must be outside artifacts and release output")
    personal = (ROOT / "native-installer").resolve()
    if artifacts.is_relative_to(personal):
        raise ValueError("Personal native installer is not a public release input")
    if not notices.is_file() or not notices.stat().st_size:
        raise ValueError("Supply release notices, licenses and corresponding source links")
    key = serialization.load_pem_private_key(key_path.read_bytes(), password=None)
    if not isinstance(key, Ed25519PrivateKey):
        raise ValueError("Use an Ed25519 signing key")
    entries, copies = [], []
    for name in sorted(names):
        destination, reboot = COMPONENTS[name]
        source = (artifacts / destination).resolve(strict=True)
        if not source.is_relative_to(artifacts) or not source.is_file():
            raise ValueError("Input escaped artifact directory")
        size = source.stat().st_size
        if not 0 < size <= 256 * 1024 * 1024:
            raise ValueError("Invalid software file size")
        sha = digest(source)
        entries.append(dict(component=name, destination=destination, size=size, sha256=sha,
                            url=f"releases/{release}/files/{sha}", reboot=reboot))
        copies.append((source, sha))
    public = key.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)
    manifest = dict(schema=1, release=release, sequence=sequence, channel=channel,
                    base_url=config["base_url"], key_id=hashlib.sha256(public).hexdigest(),
                    components=entries, changes=changes, notices=f"releases/{release}/NOTICES.txt")
    body = canonical(manifest)
    signature = base64.b64encode(key.sign(body)).decode()
    envelope = canonical(dict(manifest=base64.b64encode(body).decode(), signature=signature))
    files = output / "files"
    files.mkdir(parents=True)
    for source, sha in copies:
        target = files / sha
        shutil.copyfile(source, target)
        if digest(target) != sha:
            raise ValueError("Artifact changed during packaging; discard this unpublished release")
    shutil.copyfile(notices, output / "NOTICES.txt")
    (output / "manifest.json").write_bytes(envelope)
    # Public material only. Client trust must be provisioned in its build, never
    # learned from this network file.
    (output / "public-key.txt").write_text(base64.b64encode(public).decode() + "\n")
    print(f"Prepared {release} ({channel}): {len(entries)} components; not uploaded")
    return manifest, envelope, public


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--artifacts", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--key", type=Path, required=True)
    p.add_argument("--release", required=True)
    p.add_argument("--sequence", type=int, required=True)
    p.add_argument("--channel", choices=("stable", "testing"), default="testing")
    p.add_argument("--component", action="append", choices=sorted(COMPONENTS), required=True)
    p.add_argument("--notices", type=Path, required=True)
    p.add_argument("--changes", type=Path, required=True)
    a = p.parse_args()
    stage(a.artifacts, a.output, a.key, a.release, a.sequence, a.channel, a.component, a.notices, a.changes.read_text(encoding="utf-8"))
