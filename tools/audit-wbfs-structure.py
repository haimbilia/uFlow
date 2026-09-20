"""Read-only bounded FTP checks of WBFS block maps and Wii partition metadata."""
import argparse
import json
import struct
import ftplib


def read(host, path, offset, length):
    ftp = ftplib.FTP(host, timeout=15)
    try:
        ftp.login()
        ftp.voidcmd('TYPE I')
        with ftp.transfercmd('RETR ' + path, rest=offset if offset else None) as sock:
            result = bytearray()
            while len(result) < length:
                chunk = sock.recv(length - len(result))
                if not chunk:
                    break
                result.extend(chunk)
        if len(result) != length:
            raise ValueError(f'Short read at {offset}: {len(result)}/{length}')
        return bytes(result)
    finally:
        ftp.close()


def audit(host, path, size):
    head = read(host, path, 0, 512)
    if head[:4] != b'WBFS' or not 9 <= head[8] <= 12 or not 21 <= head[9] <= 26:
        raise ValueError('Unsupported WBFS header')
    sector, block = 1 << head[8], 1 << head[9]
    if not head[12] or any(head[13:512]):
        raise ValueError('Not a supported single-disc WBFS')
    count = (143432 * 2) >> (head[9] - 15)
    info = read(host, path, head[12] * sector, 256 + 2 * count)
    mapping = struct.unpack_from('>' + str(count) + 'H', info, 256)
    allocated = [n for n in mapping if n]
    outside = [n for n in allocated if (n + 1) * block > size]

    def disc(offset, length):
        output = bytearray()
        while length:
            index, within = divmod(offset, block)
            if index >= len(mapping) or not mapping[index]:
                raise ValueError(f'Unmapped required disc block {index}')
            take = min(length, block - within)
            output.extend(read(host, path, mapping[index] * block + within, take))
            length -= take
            offset += take
        return bytes(output)

    table = disc(0x40000, 32)
    parts = []
    for group in range(4):
        n, word_offset = struct.unpack_from('>II', table, group * 8)
        if n > 32:
            raise ValueError('Invalid partition count')
        if not n:
            continue
        entries = disc(word_offset * 4, n * 8)
        for i in range(n):
            part_words, kind = struct.unpack_from('>II', entries, i * 8)
            part_offset = part_words * 4
            item = {'type': kind, 'offset': part_offset}
            if kind == 0:
                p = disc(part_offset, 0x2C0)
                tmd_size, tmd_words = struct.unpack_from('>II', p, 0x2A4)
                if not 0x1E4 <= tmd_size <= 65536:
                    raise ValueError('Invalid partition TMD size')
                tmd = disc(part_offset + tmd_words * 4, tmd_size)
                item.update(tmd_signature_type=hex(struct.unpack_from('>I', tmd)[0]),
                            requested_ios=struct.unpack_from('>Q', tmd, 0x184)[0] & 0xFFFFFFFF,
                            tmd_title=tmd[0x18C:0x194].hex())
            parts.append(item)
    return {'file': path.rsplit('/', 1)[-1], 'size': size,
            'id': info[:6].decode('ascii'), 'block_bytes': block,
            'allocated_blocks': len(allocated), 'out_of_file_blocks': len(outside),
            'duplicate_blocks': len(allocated) - len(set(allocated)), 'partitions': parts}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True)
    args = parser.parse_args()
    with ftplib.FTP(args.host, timeout=15) as ftp:
        ftp.login()
        ftp.voidcmd('TYPE I')
        files = [(p, ftp.size(p)) for p in ftp.nlst('/fs/vol/external01/wbfs')
                 if p.lower().endswith('.wbfs')]
    for path, size in files:
        try:
            result = audit(args.host, path, size)
        except Exception as error:
            result = {'file': path, 'error': str(error)}
        print(json.dumps(result), flush=True)


if __name__ == '__main__':
    main()
