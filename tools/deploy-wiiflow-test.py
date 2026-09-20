"""Install a fresh WiiFlow test package to SD over FTP; verify every file."""
import argparse
import ftplib
import hashlib
import json
from pathlib import Path, PurePosixPath


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True)
    parser.add_argument('--stage', type=Path, required=True)
    args = parser.parse_args()
    stage = args.stage.resolve(strict=True)
    files = sorted(p for p in stage.rglob('*') if p.is_file())
    assert files and (stage / 'apps/wiiflow/boot.dol').is_file()
    assert (stage / 'apps/wiiflow/wiiflow_lite.ini').is_file()
    for path in files:
        assert not path.is_symlink()
        relative = path.relative_to(stage).as_posix()
        assert relative.startswith(('apps/wiiflow/', 'wiiflow/')), relative
    # Publish the app executable and metadata only after supporting assets.
    files.sort(key=lambda p: (p.name in ('boot.dol', 'meta.xml'), p.as_posix()))
    root = '/fs/vol/external01'
    manifest = []
    with ftplib.FTP(args.host, timeout=30) as ftp:
        ftp.login()
        for parent, child in ((root, 'wiiflow'), (root + '/apps', 'wiiflow')):
            names = {PurePosixPath(n.rstrip('/')).name.lower() for n in ftp.nlst(parent)}
            if child in names:
                raise RuntimeError(f'Refusing to replace existing {parent}/{child}')
        made = {root, root + '/apps'}
        for index, path in enumerate(files, 1):
            relative = path.relative_to(stage).as_posix()
            remote = root + '/' + relative
            parent = PurePosixPath(remote).parent
            for directory in reversed((parent, *parent.parents)):
                directory = str(directory)
                if directory == root or directory.startswith(root + '/'):
                    if directory not in made:
                        ftp.mkd(directory)
                        made.add(directory)
            expected = hashlib.sha256(path.read_bytes()).hexdigest()
            with path.open('rb') as source:
                ftp.storbinary('STOR ' + remote, source, blocksize=65536)
            actual = hashlib.sha256()
            ftp.retrbinary('RETR ' + remote, actual.update, blocksize=65536)
            if actual.hexdigest() != expected:
                raise RuntimeError('Readback mismatch: ' + remote)
            manifest.append({'path': relative, 'bytes': path.stat().st_size,
                             'sha256': expected})
            if index % 20 == 0 or path.name in ('boot.dol', 'meta.xml'):
                print(f'Verified {index}/{len(files)}: {relative}', flush=True)
    report = stage.parent / 'wiiflow-deploy-verified.json'
    report.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print(f'PASS: all {len(files)} files uploaded and SHA256 readback verified. {report}', flush=True)


if __name__ == '__main__':
    main()
