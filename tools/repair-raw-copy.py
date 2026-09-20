"""Compare one local raw title with its FTP copy; optionally repair and hash-verify.

Never deletes files or overlays an update. Local title is the source of truth.
"""
import argparse
import ftplib
import hashlib
from pathlib import Path
import time


def entries(ftp, path):
    lines = []
    ftp.retrlines("LIST " + path, lines.append)
    result = {}
    for line in lines:
        parts = line.split(maxsplit=8)
        if len(parts) != 9 or parts[0][0] not in "d-":
            raise RuntimeError("Unrecognized FTP listing: " + line)
        name = parts[8]
        if name not in (".", ".."):
            result[name] = (parts[0][0] == "d", int(parts[4]))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host")
    parser.add_argument("source", type=Path)
    parser.add_argument("destination")
    parser.add_argument("--repair", action="store_true")
    args = parser.parse_args()
    source = args.source.resolve(strict=True)
    destination = args.destination.rstrip("/")
    if not source.is_dir() or not destination.startswith("/fs/vol/external01/wiiu/raw-games/"):
        raise ValueError("Expected one source directory and a raw-games FTP destination")
    with ftplib.FTP(args.host, timeout=30) as ftp:
        ftp.login()
        differences = []
        missing_dirs = []
        checked = 0
        pending = [(source, destination, True)]
        while pending:
            local_dir, remote_dir, exists = pending.pop()
            remote = entries(ftp, remote_dir) if exists else {}
            if not exists:
                missing_dirs.append(remote_dir)
            for item in sorted(local_dir.iterdir()):
                # A raw title consists of these three trees, not download-site
                # shortcuts, readmes, or other unrelated files in its root.
                if local_dir == source and item.name not in ("code", "content", "meta"):
                    continue
                if item.is_symlink():
                    raise RuntimeError("Refusing source symlink: " + str(item))
                target = remote_dir + "/" + item.name
                entry = remote.get(item.name)
                if entry is not None and entry[0] != item.is_dir():
                    raise RuntimeError("File/directory collision: " + target)
                if item.is_dir():
                    pending.append((item, target, entry is not None))
                elif item.is_file():
                    checked += 1
                    size = item.stat().st_size
                    if entry is None or entry[1] != size:
                        differences.append((item, target, size))
        total = sum(size for _, _, size in differences)
        print(f"Compared {checked} files: {len(differences)} missing/size-mismatched, {total / 1048576:.2f} MiB", flush=True)
        print(f"Missing directories: {len(missing_dirs)}", flush=True)
        if not args.repair:
            for item, _, size in differences[:12]:
                print(f"  {item.relative_to(source)} ({size} bytes)", flush=True)
            return
        for directory in sorted(missing_dirs, key=lambda p: p.count("/")):
            ftp.mkd(directory)
        last_report = time.monotonic()
        transferred = 0
        for index, (item, target, size) in enumerate(differences, 1):
            digest = hashlib.sha256()
            with item.open("rb") as stream:
                ftp.storbinary("STOR " + target, stream, 65536, digest.update)
            remote_digest = hashlib.sha256()
            ftp.retrbinary("RETR " + target, remote_digest.update, 65536)
            if digest.digest() != remote_digest.digest():
                raise RuntimeError("Readback hash mismatch: " + target)
            transferred += size
            if time.monotonic() - last_report >= 10 or index == len(differences):
                print(f"Verified {index}/{len(differences)} files, {transferred / 1048576:.2f}/{total / 1048576:.2f} MiB", flush=True)
                last_report = time.monotonic()
        print("Repair complete; every transferred file readback SHA256 verified.", flush=True)


if __name__ == "__main__":
    main()
