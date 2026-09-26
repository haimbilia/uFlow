"""Expand RPL sections into an analysis-only ELF for readelf/objdump.

Never write back to a game; output must be a separate file. No linking or code
patching is performed. Relocations remain available for import-call analysis.
"""
import argparse
import pathlib
import struct
import zlib


def expand(source):
    if source[:6] != b"\x7fELF\x01\x02":
        raise ValueError("expected 32-bit big-endian ELF/RPL")
    shoff = struct.unpack_from(">I", source, 32)[0]
    shentsize, shnum = struct.unpack_from(">HH", source, 46)
    if shentsize != 40 or shoff + shnum * 40 > len(source):
        raise ValueError("invalid section table")
    sections = [list(struct.unpack_from(">10I", source, shoff + i * 40))
                for i in range(shnum)]
    output = bytearray(source[:52])
    # RPL uses a proprietary ELF type/ABI; ordinary BFD accepts relocatable ELF.
    output[7:9] = b"\0\0"
    struct.pack_into(">H", output, 16, 1)
    for section in sections:
        if section[1] in (0, 8):
            continue
        offset, size = section[4:6]
        if offset + size > len(source):
            raise ValueError("truncated section")
        data = source[offset:offset + size]
        if section[2] & 0x08000000:
            expected = struct.unpack_from(">I", data)[0]
            data = zlib.decompress(data[4:])
            if len(data) != expected:
                raise ValueError("section size mismatch")
            section[2] &= ~0x08000000
        if section[1] == 2:
            data = bytearray(data)
            for i in range(0, len(data), 16):
                index = struct.unpack_from(">H", data, i + 14)[0]
                value = struct.unpack_from(">I", data, i + 4)[0]
                if 0 < index < len(sections):
                    struct.pack_into(">I", data, i + 4, value - sections[index][3])
        elif section[1] == 4:
            data = bytearray(data)
            for i in range(0, len(data), 12):
                value = struct.unpack_from(">I", data, i)[0]
                struct.pack_into(">I", data, i, value - sections[section[7]][3])
        output.extend(b"\0" * (-len(output) % max(1, section[8])))
        section[4], section[5] = len(output), len(data)
        if section[1] == 2:
            section[7] = next((i for i in range(len(data) // 16)
                               if data[i * 16 + 12] >> 4 != 0), len(data) // 16)
        output.extend(data)
    output.extend(b"\0" * (-len(output) % 4))
    struct.pack_into(">I", output, 32, len(output))
    for section in sections:
        output.extend(struct.pack(">10I", *section))
    return output


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve():
        parser.error("output must differ from source")
    result = expand(args.source.read_bytes())
    with args.output.open("xb") as target:
        target.write(result)
