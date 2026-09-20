import base64
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey, Ed25519PublicKey

spec = importlib.util.spec_from_file_location("update_release", Path(__file__).resolve().parents[1] / "tools/build-update-release.py")
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)


class ReleaseTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.artifacts = self.root / "software"
        for path, _ in release.COMPONENTS.values():
            source = self.artifacts / path
            source.parent.mkdir(parents=True, exist_ok=True)
            source.write_bytes(b"software fixture " + path.encode())
        secret = self.artifacts / "wiiu/vc-bases/nes/default/content/rom.bin"
        secret.parent.mkdir(parents=True)
        secret.write_bytes(b"must never be published")
        self.key = self.root / "private.pem"
        self.key.write_bytes(Ed25519PrivateKey.generate().private_bytes(
            serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
        self.notices = self.root / "NOTICES.txt"
        self.notices.write_text("Fixture license and corresponding source")
        self.output = self.root / "release"

    def stage(self, **overrides):
        args = dict(artifacts=self.artifacts, output=self.output, key_path=self.key,
                    release="2026.09.20-test1", sequence=1, channel="testing",
                    names=["uflow"], notices=self.notices)
        args.update(overrides)
        return release.stage(**args)

    def test_signature_hashes_no_bases_or_keys(self):
        manifest, envelope, public = self.stage()
        env = json.loads(envelope)
        body, signature = base64.b64decode(env["manifest"]), base64.b64decode(env["signature"])
        Ed25519PublicKey.from_public_bytes(public).verify(signature, body)
        self.assertEqual(json.loads(body), manifest)
        with self.assertRaises(InvalidSignature):
            Ed25519PublicKey.from_public_bytes(public).verify(signature, body + b" ")
        for entry in manifest["components"]:
            self.assertEqual(release.digest(self.output / "files" / entry["sha256"]), entry["sha256"])
        self.assertEqual(len(list((self.output / "files").iterdir())), 1)
        self.assertFalse(any(p.suffix == ".pem" or "rom.bin" in str(p) for p in self.output.rglob("*")))

    def test_release_is_immutable(self):
        self.stage()
        before = (self.output / "manifest.json").read_bytes()
        with self.assertRaisesRegex(ValueError, "immutable"):
            self.stage(sequence=2)
        self.assertEqual((self.output / "manifest.json").read_bytes(), before)

    def test_wiiflow_group_required(self):
        with self.assertRaisesRegex(ValueError, "all three booters"):
            self.stage(names=["wiiflow"])
        manifest, _, _ = self.stage(names=sorted(release.WIIFLOW_GROUP))
        self.assertEqual(len(manifest["components"]), 4)

    def test_reject_untrusted_input_paths_and_channels(self):
        for changes in ({"release": "../escape"}, {"channel": "evil"}, {"sequence": 0},
                        {"names": ["rom"]}, {"names": ["uflow", "uflow"]}):
            with self.assertRaises(ValueError):
                self.stage(**changes)
        self.assertFalse(self.output.exists())

    def test_private_key_cannot_be_in_artifacts(self):
        key = self.artifacts / "signing.pem"
        key.write_bytes(self.key.read_bytes())
        with self.assertRaisesRegex(ValueError, "outside artifacts"):
            self.stage(key_path=key)

    def test_empty_or_missing_binary_refused(self):
        (self.artifacts / release.COMPONENTS["uflow"][0]).write_bytes(b"")
        with self.assertRaisesRegex(ValueError, "file size"):
            self.stage()
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
