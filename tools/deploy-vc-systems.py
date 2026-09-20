"""Upload validated VC bases/test cartridges; never overwrite different base data.

Use --activate only after building/testing the matching app. Activation backs up
and readback-verifies app/config writes, and preserves existing library rows.
"""
import argparse
import ftplib
import hashlib
import io
import json
from pathlib import Path

SD = '/fs/vol/external01'
APP = SD + '/wiiu/apps/uFlow'
SYSTEMS = {
    'nes': ('EarthBound Beginnings', 'NES', '.nes'),
    'snes': ('Kirbys Dream Land 3', 'SNES', '.sfc,.smc'),
    'gba': ('The Legend of Zelda The Minish Cap', 'GBA', '.gba'),
    'n64': ('Donkey Kong 64', 'N64', '.z64,.v64,.n64'),
    'pce': ('BOMBERMAN 94', 'PC Engine', '.pce'),
    'msx': ('Goemon', 'MSX', '.rom,.mx1,.mx2'),
}

def sha(data):
    return hashlib.sha256(data).hexdigest()

def fetch(ftp, path):
    out = io.BytesIO()
    ftp.retrbinary('RETR ' + path, out.write, 65536)
    return out.getvalue()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', default='192.168.1.116')
    parser.add_argument('--bases', type=Path, default=Path('D:/WiiU'))
    parser.add_argument('--activate', action='store_true')
    parser.add_argument('--activate-only', action='store_true', help='Use the completed asset verification report; upload only app/configs')
    parser.add_argument('--systems', nargs='+', choices=SYSTEMS, default=list(SYSTEMS))
    args = parser.parse_args()
    project = Path(__file__).resolve().parents[1]
    artifacts = project / 'artifacts/vc-systems-20260913'
    backup = artifacts / 'deployment'
    backup.mkdir(exist_ok=True)
    report_path = backup / 'verified.json'
    report = json.loads(report_path.read_text()) if report_path.exists() else []
    with ftplib.FTP(args.host, timeout=45) as ftp:
        ftp.login()
        known = {}

        def directory(path):
            if path in known:
                return known[path]
            parent, name = path.rsplit('/', 1)
            if path != SD and name not in directory(parent):
                ftp.mkd(path)
                known[parent].add(name)
            names = {p.rstrip('/').rsplit('/', 1)[-1] for p in ftp.nlst(path)}
            known[path] = names
            return names

        def upload(path, data, replace=False):
            if not path.startswith(SD + '/wiiu/') or '\n' in path or '\r' in path:
                raise RuntimeError('Unsafe upload path')
            parent, name = path.rsplit('/', 1)
            exists = name in directory(parent)
            old = fetch(ftp, path) if exists else None
            if old == data:
                report.append({'path': path, 'sha256': sha(data), 'unchanged': True})
                return
            if exists and not replace:
                raise RuntimeError('Different existing file; refusing to overwrite: ' + path)
            key = sha(path.encode())[:16] + '-' + name
            if old is not None:
                original = backup / ('before-' + key)
                if not original.exists():
                    original.write_bytes(old)
                # Recheck immediately before replacing user-owned live state.
                if fetch(ftp, path) != old:
                    raise RuntimeError('Remote file changed during deployment: ' + path)
            print('Writing', path, '(' + str(len(data)) + ' bytes)', flush=True)
            ftp.storbinary('STOR ' + path, io.BytesIO(data), 65536)
            actual = fetch(ftp, path)
            if actual != data:
                raise RuntimeError('Readback mismatch: ' + path)
            known[parent].add(name)
            report.append({'path': path, 'sha256': sha(actual), 'bytes': len(actual)})

        directory(SD)
        for system in ([] if args.activate_only else args.systems):
            prefix, label, extensions = SYSTEMS[system]
            matches = [p for p in args.bases.iterdir() if p.is_dir() and p.name.startswith(prefix + ' [')]
            if len(matches) != 1:
                raise RuntimeError('Ambiguous/missing base: ' + prefix)
            base = matches[0]
            exported = artifacts / 'roms' / system / ('baseline' + extensions.split(',')[0])
            if not exported.is_file():
                raise RuntimeError('Run real-base tests/export first: ' + system)
            files = sorted(p for part in ('code', 'content', 'meta') for p in (base / part).rglob('*') if p.is_file())
            if not files:
                raise RuntimeError('Empty base: ' + str(base))
            print('Uploading/readback verifying', system, len(files), 'base files', flush=True)
            for i, path in enumerate(files):
                if path.is_symlink():
                    raise RuntimeError('Base contains a link')
                data = path.read_bytes()
                upload(SD + '/wiiu/vc-bases/' + system + '/default/' + path.relative_to(base).as_posix(), data)
                if sha(path.read_bytes()) != sha(data):
                    raise RuntimeError('Source base changed: ' + str(path))
                if (i + 1) % 50 == 0:
                    print(system, i + 1, '/', len(files), 'verified', flush=True)
            upload(SD + '/wiiu/roms/' + system + '/' + prefix + extensions.split(',')[0], exported.read_bytes())
            print('VERIFIED BASE + BASELINE ROM:', system, flush=True)
            report_path.write_text(json.dumps(report, indent=2) + '\n')

        if args.activate or args.activate_only:
            verified = {entry['path'] for entry in report}
            for system, (prefix, _, extensions) in SYSTEMS.items():
                expected = SD + '/wiiu/roms/' + system + '/' + prefix + extensions.split(',')[0]
                if expected not in verified:
                    raise RuntimeError('Complete asset upload first: ' + system)
            app = (project / 'app/uFlow.wuhb').read_bytes()
            if not app.startswith(b'WUHB'):
                raise RuntimeError('Invalid WUHB')
            upload(APP + '/uFlow.wuhb', app, replace=True)
            for system in SYSTEMS:
                upload(APP + '/launchers/' + system + '-vc.ufmod', (project / 'launcher-modules' / (system + '-vc.ufmod')).read_bytes())
                upload(APP + '/launchers/' + system + '-vc.cfg', (project / 'launcher-modules' / (system + '-vc-example.cfg')).read_bytes())
            for filename in ('libraries.cfg', 'content_sources.cfg'):
                original = fetch(ftp, APP + '/' + filename)
                text = original.decode('utf-8')
                lines = text.splitlines()
                additions = []
                order = max((int(row.split('\t')[6]) for row in lines if filename == 'libraries.cfg' and len(row.split('\t')) >= 7), default=3) + 1
                for system, (_, label, extensions) in SYSTEMS.items():
                    if filename == 'libraries.cfg':
                        if any(row.split('\t')[0] == system for row in lines):
                            continue
                        additions.append('\t'.join((system, label, 'files', 'wiiflow', extensions, '1', str(order), system + '-vc', '135', '190', '14')))
                        order += 1
                    else:
                        path = 'fs:/vol/external01/wiiu/roms/' + system
                        if any(path in row.split('\t') for row in lines):
                            continue
                        additions.append('\t'.join(('files', '1', '3', path, 'wiiflow', system, system + '-vc')))
                if additions:
                    new = original + (b'' if original.endswith(b'\n') else b'\n') + ('\n'.join(additions) + '\n').encode()
                    if fetch(ftp, APP + '/' + filename) != original:
                        raise RuntimeError('Configuration changed; retry after closing uFlow')
                    upload(APP + '/' + filename, new, replace=True)
            print('APP AND SYSTEM CONFIGURATION VERIFIED. Restart uFlow and Scan Now.', flush=True)
    report_path.write_text(json.dumps(report, indent=2) + '\n')
    print('PASS:', len(report), 'verified files; report:', report_path, flush=True)

if __name__ == '__main__':
    main()
