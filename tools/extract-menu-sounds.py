"""Extract embedded Nintendo FWAV files from a locally supplied sound archive.

Optional vgmstream-cli decoding writes standard WAV files for auditioning.
Does not download Nintendo assets or alter the source archive.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess


def waves(data):
    offset = 0
    while True:
        offset = data.find(b"FWAV", offset)
        if offset < 0:
            return
        start = offset
        offset += 4
        if start + 20 > len(data):
            continue
        bom = data[start + 4:start + 6]
        if bom not in (b"\xfe\xff", b"\xff\xfe"):
            continue
        endian = ">" if bom == b"\xfe\xff" else "<"
        header = struct.unpack_from(endian + "H", data, start + 6)[0]
        length = struct.unpack_from(endian + "I", data, start + 12)[0]
        count = struct.unpack_from(endian + "H", data, start + 16)[0]
        if not (2 <= count <= 8 and 20 + count * 12 <= header <= length <= 16 * 1024 * 1024):
            continue
        if start + length > len(data):
            continue
        valid = True
        for block in range(count):
            relative, size = struct.unpack_from(endian + "II", data, start + 24 + block * 12)
            if relative < header or relative + size > length:
                valid = False
                break
        if valid:
            yield start, data[start:start + length]
            offset = start + length


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--decoder", type=Path, help="vgmstream-cli executable")
    parser.add_argument("--manifest", type=Path, help="Select named samples using a verified source.json")
    args = parser.parse_args()
    data = args.archive.read_bytes()
    selected = None
    if args.manifest:
        manifest = json.loads(args.manifest.read_text())
        if hashlib.sha256(data).hexdigest() != manifest["source_sha256"]:
            raise SystemExit("Archive differs from the mapped source; do not guess sample indices")
        selected = {item["sample_sha256"]: Path(item["file"]).stem for item in manifest["sounds"]}
    args.output.mkdir(parents=True, exist_ok=True)
    entries = []
    seen = set()
    for index, (offset, blob) in enumerate(waves(data)):
        digest = hashlib.sha256(blob).hexdigest()
        if selected is not None and digest not in selected:
            continue
        if selected is not None and digest in seen:
            continue
        seen.add(digest)
        name = selected[digest] if selected is not None else f"wave-{index:03d}"
        target = args.output / f"{name}.bfwav"
        target.write_bytes(blob)
        entry = dict(file=target.name, offset=offset, bytes=len(blob), sha256=hashlib.sha256(blob).hexdigest())
        if args.decoder:
            decoded = target.with_suffix(".wav")
            subprocess.run([str(args.decoder.resolve()), "-i", "-o", str(decoded), str(target)], check=True)
            entry["wav"] = decoded.name
        entries.append(entry)
    (args.output / "extraction.json").write_text(json.dumps(dict(
        source=str(args.archive.resolve()), source_sha256=hashlib.sha256(data).hexdigest(), waves=entries), indent=2))
    print(f"Extracted {len(entries)} embedded FWAV files")
    if not entries:
        raise SystemExit("No validated FWAV files found; this archive may need unpacking first")
    if selected is not None and {entry["sha256"] for entry in entries} != set(selected):
        raise SystemExit("Some mapped samples were not found")


if __name__ == "__main__":
    main()
