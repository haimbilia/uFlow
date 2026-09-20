"""Read-only XML, RPX section and asset-header checks for a raw game folder."""
import argparse
import struct
import zlib
import xml.etree.ElementTree as ET
from collections import Counter
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path)
    args = parser.parse_args()
    root = args.game
    app = ET.parse(root / "code/app.xml").getroot()
    cos = ET.parse(root / "code/cos.xml").getroot()
    meta = ET.parse(root / "meta/meta.xml").getroot()
    assert app.findtext("title_id").lower() == meta.findtext("title_id").lower(), "title ID mismatch"
    rpx = root / "code" / cos.findtext("argstr").split()[0]
    data = rpx.read_bytes()
    assert data[:6] == b"\x7fELF\x01\x02", "not big-endian ELF32"
    assert struct.unpack_from(">H", data, 18)[0] == 20, "not PowerPC"
    section_offset = struct.unpack_from(">I", data, 32)[0]
    section_size, section_count = struct.unpack_from(">HH", data, 46)
    assert section_size == 40 and section_offset + section_size * section_count <= len(data)
    compressed = 0
    for index in range(section_count):
        _, kind, flags, _, offset, size, *_ = struct.unpack_from(">10I", data, section_offset + index * 40)
        if kind in (0, 8):
            continue
        assert offset + size <= len(data), f"RPX section {index} truncated"
        if flags & 0x08000000:
            assert size >= 4
            expected = struct.unpack_from(">I", data, offset)[0]
            assert expected <= 256 * 1024 * 1024
            decoder = zlib.decompressobj()
            inflated = decoder.decompress(data[offset + 4:offset + size], expected + 1)
            assert decoder.eof and len(inflated) == expected, f"RPX section {index} invalid stream/size"
            compressed += 1
    files = [p for p in root.rglob("*") if p.is_file()]
    empty = [str(p.relative_to(root)) for p in files if p.stat().st_size == 0]
    print(f"XML title IDs match: {app.findtext('title_id')}; RPX {rpx.name}: {section_count} sections, {compressed} compressed sections passed")
    print(f"Files={len(files)} bytes={sum(p.stat().st_size for p in files)} zero-byte={len(empty)}")
    print("Empty files:", empty)
    headers = Counter()
    for p in root.joinpath("content").rglob("*.gfa"):
        with p.open("rb") as source:
            header = source.read(64)
        headers[header[:4]] += 1
    print("GFA magic counts:", dict(headers))
    for p in list(root.joinpath("content").rglob("*.gfa"))[:2]:
        with p.open("rb") as source:
            print(p.name, p.stat().st_size, source.read(64).hex(" "))
    print("PASS structural checks; proprietary asset payloads and file-set completeness NOT verified")


if __name__ == "__main__":
    main()
