"""Create the local update signing key once and export its public trust anchor."""
import argparse
from pathlib import Path
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

p = argparse.ArgumentParser()
p.add_argument("key", type=Path)
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
if a.key.resolve().is_relative_to(root):
    raise ValueError("Private key must be outside the repository")
a.key.parent.mkdir(parents=True, exist_ok=True)
if not a.key.exists():
    with a.key.open("xb") as f:
        f.write(Ed25519PrivateKey.generate().private_bytes(serialization.Encoding.PEM,
                   serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
key = serialization.load_pem_private_key(a.key.read_bytes(), password=None)
public = key.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)
(root / "app/source/update_trust.hpp").write_text(
    '#pragma once\n#include <cstdint>\nnamespace UFlowUpdate {\ninline constexpr uint8_t PublicKey[32]={' +
    ','.join(str(x) for x in public) + '};\n}\n')
print("Public trust anchor exported; private key stays outside the project.")
