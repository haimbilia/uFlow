#!/usr/bin/env python3
"""Reject known private Cloud implementation in a proposed public Git tree/index.

Source-only guard: intentionally does not inspect/block official update binaries.
Run for every new public commit and review renamed implementations separately.
"""
import argparse
from pathlib import PurePosixPath
import subprocess
import sys

PRIVATE_PREFIXES = ("app/content/cloud/", "tests/cloud", "tools/skins/")
PRIVATE_FILES = {"app/source/cloud_media.cpp", "app/source/cloud_media.hpp",
                 "app/source/cloud_settings_members.hpp", "app/source/cloud_systems.hpp",
                 "app/source/skin_store.hpp", "app/source/skin_download_members.hpp",
                 "docs/cloud-media.md"}
MARKERS = (b"CloudMedia", b"cloud_settings_members", b"cloud_media.hpp",
           b"cloudOpen_", b"cloudFocus_", b"OpenCloud(", b"KARO CLOUD",
           b"SkinStore", b"skinDownloads_", b"/uflow-skins", b"wp-json/karo-uflow",
           b"karo-uflow-media", b"/uflow-cloud", b"/content/cloud/")
CODE_SUFFIXES = {".cpp", ".hpp", ".c", ".h", ".inc", ".php", ".js", ".ts"}


def violation(path, data):
    if path in PRIVATE_FILES or path.startswith(PRIVATE_PREFIXES):
        return "private Cloud file/asset"
    if PurePosixPath(path).suffix in CODE_SUFFIXES:
        for marker in MARKERS:
            if marker in data:
                return "private Cloud integration marker: " + marker.decode()
    return None


def git(*args):
    return subprocess.check_output(["git", *args])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--staged", action="store_true")
    group.add_argument("--ref", help="commit/tag to audit, not the working tree")
    args = parser.parse_args()
    if args.staged:
        paths = git("ls-files", "-z").split(b"\0")
        prefix = ":"
    else:
        ref = git("rev-parse", "--verify", args.ref + "^{commit}").decode().strip()
        paths = git("ls-tree", "-r", "--name-only", "-z", ref).split(b"\0")
        prefix = ref + ":"
    errors = []
    for raw in paths:
        if not raw:
            continue
        path = raw.decode("utf-8")
        reason = violation(path, b"")
        if reason is None and PurePosixPath(path).suffix in CODE_SUFFIXES:
            reason = violation(path, git("show", prefix + path))
        if reason:
            errors.append(path + ": " + reason)
    if errors:
        print("PUBLIC SOURCE BLOCKED:\n" + "\n".join(errors), file=sys.stderr)
        return 1
    print("PASS: no known private Cloud files/integration in public source candidate")
    return 0


if __name__ == "__main__":
    sys.exit(main())
