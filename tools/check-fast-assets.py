"""Read-only integrity check for FAST's SHN! primary zlib asset streams.

Checks stream checksum, end marker and declared decompressed length. This is
not an authenticity check or a validator for the proprietary asset semantics.
"""
import argparse
import struct
import zlib
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("content", type=Path)
    args = parser.parse_args()
    if not args.content.is_dir():
        parser.error("content directory not found")
    checked = skipped = failed = 0
    for path in args.content.rglob("*"):
        if not path.is_file():
            continue
        with path.open("rb") as source:
            header = source.read(68)
            if header[:4] != b"SHN!":
                skipped += 1
                continue
            try:
                offset, compressed, expected = struct.unpack_from(">III", header, 4)
                if offset < 68 or compressed > path.stat().st_size - offset:
                    raise ValueError("primary stream outside file")
                if expected > 64 * 1024 * 1024:
                    raise ValueError("declared stream exceeds 64 MiB check budget")
                source.seek(offset)
                payload = source.read(compressed)
                decoder = zlib.decompressobj()
                data = decoder.decompress(payload, expected + 1)
                if not decoder.eof or len(data) != expected:
                    raise ValueError(f"incomplete stream or length mismatch: {len(data)}/{expected}")
                checked += 1
            except (ValueError, zlib.error, struct.error) as error:
                print(f"FAIL {path.relative_to(args.content)}: {error}", flush=True)
                failed += 1
    print(f"Primary SHN zlib streams: passed={checked}, failed={failed}, other files={skipped}")
    raise SystemExit(1 if failed else 0)


if __name__ == "__main__":
    main()
