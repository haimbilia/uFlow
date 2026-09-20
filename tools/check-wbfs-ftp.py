"""Inspect WBFS headers on the test SD; optionally normalize filenames, no recopy."""
import argparse
import ftplib
import json
import re
import struct


def connect(host):
    ftp = ftplib.FTP(timeout=15)
    ftp.connect(host)
    ftp.login()
    ftp.voidcmd('TYPE I')
    return ftp


def header(host, path):
    # Bounded prefix read; close this FTP session after aborting the remaining data.
    ftp = connect(host)
    try:
        size = ftp.size(path)
        with ftp.transfercmd('RETR ' + path) as data:
            prefix = bytearray()
            while len(prefix) < 4096:
                chunk = data.recv(4096 - len(prefix))
                if not chunk:
                    break
                prefix.extend(chunk)
        if prefix[:4] != b'WBFS' or len(prefix) < 13:
            raise ValueError('Not a WBFS header: ' + path)
        shift = prefix[8]
        if not 9 <= shift <= 12 or prefix[12] == 0:
            raise ValueError('Unsupported WBFS sector/table: ' + path)
        offset = prefix[12] * (1 << shift)
        if offset + 96 > len(prefix):
            raise ValueError('Disc header outside bounded prefix: ' + path)
        disc = prefix[offset:offset + 96]
        game_id = bytes(disc[:6]).decode('ascii')
        if not re.fullmatch(r'[A-Z0-9]{6}', game_id):
            raise ValueError('Invalid disc ID: ' + path)
        if struct.unpack_from('>I', disc, 24)[0] != 0x5D1C9EA3:
            raise ValueError('Invalid Wii disc magic: ' + path)
        title = bytes(disc[32:96]).split(b'\0', 1)[0].decode('utf-8', 'replace')
        return {'id': game_id, 'title': title, 'size': size}
    finally:
        ftp.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    root = '/fs/vol/external01/wbfs'
    with connect(args.host) as ftp:
        names = {p.rsplit('/', 1)[-1] for p in ftp.nlst(root)}
    plan = []
    for name in sorted(names):
        if not name.lower().endswith('.wbfs'):
            continue
        if '/' in name or '\\' in name or '\r' in name or '\n' in name:
            raise ValueError('Invalid filename')
        info = header(args.host, root + '/' + name)
        stem = name[:-5]
        target = name if stem.endswith('[' + info['id'] + ']') else stem + ' [' + info['id'] + '].wbfs'
        if target != name and target in names:
            raise ValueError('Destination exists: ' + target)
        if any(n.lower().startswith(stem.lower() + '.wbf') and n != name for n in names):
            raise ValueError('Split WBFS needs coordinated rename: ' + name)
        plan.append(dict(info, source=name, target=target))
    if len({p['id'] for p in plan}) != len(plan):
        raise ValueError('Duplicate disc IDs; inspect before renaming')
    print(json.dumps(plan, indent=2))
    if args.apply:
        with connect(args.host) as ftp:
            for item in plan:
                if item['source'] == item['target']:
                    continue
                source, target = root + '/' + item['source'], root + '/' + item['target']
                if ftp.size(source) != item['size']:
                    raise ValueError('Source changed before rename')
                ftp.rename(source, target)
                if ftp.size(target) != item['size']:
                    raise ValueError('Size mismatch after rename')
                print('RENAMED ' + item['target'])


if __name__ == '__main__':
    main()
