"""Reproduce the missing-file removal fix against pinned ContentRedirection."""
import argparse
from pathlib import Path
import subprocess

REVISION = "31b319b6b4370caf4ab173f0db91cf838c6819d0"


def prepare(root, apply=False):
    if subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip() != REVISION:
        raise ValueError("Use the pinned ContentRedirection revision")
    name = "src/FSWrapper.cpp"
    original = subprocess.check_output(["git", "-C", str(root), "show", f"{REVISION}:{name}"], text=True)
    start = original.index("FSError FSWrapper::FSRemoveWrapper(")
    end = original.index("FSError FSWrapper::FSRenameWrapper(", start)
    before = original[start:end]
    after = before.replace('"[%s] Rename failed', '"[%s] Remove failed').replace(
        "if (err == ENOTDIR) {",
        "if (err == ENOENT) {\n            return FS_ERROR_NOT_FOUND;\n        } else if (err == ENOTDIR) {")
    if before == after or after.count("FS_ERROR_NOT_FOUND") != 1:
        raise ValueError("Unexpected removal implementation")
    expected = original[:start] + after + original[end:]
    path = root / name
    actual = path.read_text()
    if actual != expected:
        if not apply or actual != original:
            raise ValueError("Unexpected ContentRedirection edits; refusing to overwrite")
        path.write_text(expected, newline="\n")
    print("PASS: ContentRedirection missing-file removal patch matches pinned upstream")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    prepare(args.source, args.apply)
