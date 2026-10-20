"""Read-only inventory of GamePad transport evidence in local IOS ELF files.

This reports evidence, not a conclusion that an unrecognized IOS cannot ever
receive GamePad input. It never decrypts, patches, or writes firmware.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

MARKERS = (b'OHCI1: Failed to initialize Hai I2C!!',
           b'I2CM: i2cmCtrlDrvRead()', b'i2csCtrlDrvPoll',
           b'OHCI1 : DRC controller on')
LITERALS = (0x0D000570, 0x0D800570, 0x0D000584, 0x0D800584)

def inspect(data):
    offset = data.find(b'\x7fELF', 0, 4096)
    if offset < 0:
        raise ValueError('no plaintext ELF header in first 4096 bytes')
    elf = data[offset:]
    if len(elf) < 52 or elf[:6] != b'\x7fELF\x01\x02':
        raise ValueError('expected big-endian ELF32')
    phoff, = struct.unpack_from('>I', elf, 28)
    entsize, count = struct.unpack_from('>HH', elf, 42)
    if entsize != 32 or count > 256 or phoff + count * entsize > len(elf):
        raise ValueError('invalid program header table')
    segments = []
    for i in range(count):
        kind, start, address, _, size, memsize, flags, _ = struct.unpack_from('>8I', elf, phoff + i * entsize)
        if kind != 1:
            continue
        if size > memsize or start + size > len(elf) or address + memsize > 0x100000000:
            raise ValueError('invalid load segment')
        content = elf[start:start+size]
        markers = [m.decode() for m in MARKERS if m in content]
        literals = {f'{value:08x}': [f'{address+j:08x}' for j in range(0, len(content)-3, 4)
                    if struct.unpack_from('>I', content, j)[0] == value] for value in LITERALS}
        literals = {key: value for key, value in literals.items() if value}
        segments.append(dict(address=f'{address:08x}', file_bytes=size, memory_bytes=memsize,
                             flags=flags, markers=markers, register_literals=literals))
    return dict(sha256=hashlib.sha256(data).hexdigest(), elf_offset=offset, segments=segments)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('inputs', nargs='+', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    paths = []
    for path in args.inputs:
        paths.extend(sorted(path.rglob('*.app')) if path.is_dir() else [path])
    if args.output.resolve() in {p.resolve() for p in paths}:
        parser.error('output must be separate from input firmware')
    report = []
    for path in paths:
        try:
            row = dict(path=str(path), **inspect(path.read_bytes()))
        except ValueError as error:
            row = dict(path=str(path), skipped=str(error))
        report.append(row)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2)+'\n')
    print(f'Inspected {len(report)} files; report: {args.output}')
    for row in report:
        for segment in row.get('segments', []):
            if segment['markers'] or segment['register_literals']:
                print(row['path'], segment['address'], segment['markers'], segment['register_literals'])

if __name__ == '__main__':
    main()
