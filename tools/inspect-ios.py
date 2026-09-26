"""Extract a bounded virtual address range from a local IOS image for analysis.

Outputs a separate binary, never an installable firmware. Encrypted ancast
input optionally takes the retail ancast key as a hex argument (not saved).
"""
import argparse
import pathlib
import struct


def extract(data, address, size, key=None):
    if key:
        from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
        decrypt = Cipher(algorithms.AES(bytes.fromhex(key)), modes.CBC(
            bytes.fromhex("91c9d008312851ef6b228bf14bad4322"))).decryptor()
        data = data[:512] + decrypt.update(data[512:]) + decrypt.finalize()
    offset = data.find(b"\x7fELF", 0, 4096)
    if offset < 0 or data[offset:offset+6] != b"\x7fELF\x01\x02":
        raise ValueError("no big-endian IOS ELF header")
    elf = data[offset:]
    phoff = struct.unpack_from(">I", elf, 28)[0]
    entsize, count = struct.unpack_from(">HH", elf, 42)
    if entsize != 32 or phoff + entsize * count > len(elf):
        raise ValueError("invalid program header table")
    for i in range(count):
        kind, start, virtual, _, length, _, _, _ = struct.unpack_from(
            ">8I", elf, phoff + i * entsize)
        if kind == 1 and virtual <= address and address + size <= virtual + length:
            begin = start + address - virtual
            if begin + size > len(elf):
                raise ValueError("truncated IOS segment")
            return elf[begin:begin+size]
    raise ValueError("requested range is not contained in one file-backed segment")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    parser.add_argument("address", type=lambda s: int(s, 0))
    parser.add_argument("size", type=lambda s: int(s, 0))
    parser.add_argument("--key")
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve() or args.size <= 0:
        parser.error("a distinct output and positive size are required")
    result = extract(args.source.read_bytes(), args.address, args.size, args.key)
    with args.output.open("xb") as target:
        target.write(result)
