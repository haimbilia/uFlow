"""Deploy uFlow + its WiiFlow entry patch over FTP with backups and readback checks."""
import argparse
import ftplib
import hashlib
import io
import json
import re
import subprocess
from datetime import datetime
from pathlib import Path

SD = '/fs/vol/external01'
MARKER = b'UFLOW_WII_PATH_BACKEND_V1'

def fetch(ftp, path):
    out = io.BytesIO()
    ftp.retrbinary('RETR ' + path, out.write, 65536)
    return out.getvalue()

def digest(data):
    return hashlib.sha256(data).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True)
    parser.add_argument('--backend', type=Path, required=True)
    parser.add_argument('--nintendont-source', type=Path, help='Install official files from the pinned Nintendont checkout')
    parser.add_argument('--usbloader-backend', type=Path, help='Install the uFlow exact-path USB Loader GX build')
    parser.add_argument('--startup-module', type=Path, help='Install opt-in uFlow Aroma startup module; does not enable startup')
    args = parser.parse_args()
    project = Path(__file__).resolve().parents[1]
    backend = args.backend.read_bytes()
    app = (project / 'app/uFlow.wuhb').read_bytes()
    if MARKER not in backend or len(backend) < 1000000 or not app.startswith(b'WUHB'):
        raise RuntimeError('Not the expected WiiFlow backend/uFlow WUHB')
    nin_files = []
    extras=[]
    if args.usbloader_backend:
        data=args.usbloader_backend.read_bytes()
        if b'UFLOW_USBLOADER_PATH_V1' not in data or b'UFLOW_LAUNCHER_SELECT_V1' not in backend:
            raise RuntimeError('USB Loader GX adapter/bridge marker missing')
        extras.append((SD+'/apps/usbloader_gx/boot.dol',data,'usbloader-boot.dol'))
    if args.startup_module:
        data=args.startup_module.read_bytes()
        if not data.startswith(b'\x7fELF') or len(data)<10000:
            raise RuntimeError('Unexpected startup RPX')
        extras.append((SD+'/wiiu/environments/aroma/modules/setup/99_autoboot.rpx',data,'99_autoboot.rpx'))
    if args.nintendont_source:
        pin = '0f69235b99099ef2851a5f6b7b0e349c572237e8'
        revision = subprocess.check_output(['git','-C',str(args.nintendont_source),'rev-parse','HEAD'],text=True).strip()
        if revision != pin or b'UFLOW_GC_PATH_BACKEND_V1' not in backend:
            raise RuntimeError('Unexpected Nintendont revision or missing GameCube adapter')
        for relative, name in [('loader/loader.dol','boot.dol'), ('nintendont/meta.xml','meta.xml'), ('nintendont/icon.png','icon.png')]:
            data = subprocess.check_output(['git','-C',str(args.nintendont_source),'show',pin+':'+relative])
            if (args.nintendont_source/relative).read_bytes() != data:
                raise RuntimeError('Nintendont file differs from official commit: '+relative)
            nin_files.append((SD+'/apps/nintendont/'+name,data,'nintendont-'+name))
    backup = project / 'dist' / ('wii-launch-' + datetime.now().strftime('%Y%m%d-%H%M%S'))
    backup.mkdir(parents=True, exist_ok=False)
    with ftplib.FTP(args.host, timeout=30) as ftp:
        ftp.login()
        tmd = fetch(ftp, '/slccmpt01/title/00010001/44574641/content/title.tmd')
        if len(tmd) < 0x194 or tmd[0x18c:0x194] != bytes.fromhex('0001000144574641'):
            raise RuntimeError('DWFA forwarder TMD missing or unexpected')
        (backup / 'forwarder-title.tmd').write_bytes(tmd)
        return_tmd = fetch(ftp, '/slccmpt01/title/00010002/48435641/content/title.tmd')
        if len(return_tmd) < 0x194 or return_tmd[0x18c:0x194] != bytes.fromhex('0001000248435641'):
            raise RuntimeError('Built-in Wii U Menu return channel missing or unexpected')
        (backup / 'return-title.tmd').write_bytes(return_tmd)
        config_path = SD + '/apps/wiiflow/wiiflow_lite.ini'
        config = fetch(ftp, config_path)
        # Change only DEBUG.sd_write_log, retain every other byte/setting.
        active = False
        lines = []
        for line in config.splitlines(keepends=True):
            stripped = line.strip()
            if stripped.startswith(b'['): active = stripped == b'[DEBUG]'
            if active and re.match(rb'sd_write_log\s*=', stripped):
                line = re.sub(rb'(sd_write_log\s*=\s*)yes\b', rb'\g<1>no', line)
            lines.append(line)
        quiet_config = b''.join(lines)
        targets = [
            (SD + '/apps/wiiflow/boot.dol', backend, 'wiiflow-boot.dol'),
            (config_path, quiet_config, 'wiiflow_lite.ini'),
            (SD + '/wiiu/apps/uFlow/uFlow.wuhb', app, 'uFlow.wuhb'),
        ]
        targets=extras+targets
        missing_allowed = set()
        if args.usbloader_backend:
            names={p.rstrip('/').rsplit('/',1)[-1].lower() for p in ftp.nlst(SD+'/apps')}
            if 'usbloader_gx' not in names: ftp.mkd(SD+'/apps/usbloader_gx')
            names={p.rstrip('/').rsplit('/',1)[-1].lower() for p in ftp.nlst(SD+'/apps/usbloader_gx')}
            if 'boot.dol' not in names: missing_allowed.add(SD+'/apps/usbloader_gx/boot.dol')
        if nin_files:
            names = {p.rstrip('/').rsplit('/',1)[-1].lower() for p in ftp.nlst(SD+'/apps')}
            if 'nintendont' not in names:
                ftp.mkd(SD+'/apps/nintendont')
            existing = {p.rstrip('/').rsplit('/',1)[-1].lower() for p in ftp.nlst(SD+'/apps/nintendont')}
            missing_allowed.update(p for p,_,_ in nin_files if p.rsplit('/',1)[-1].lower() not in existing)
            targets = nin_files + targets
        originals = {}
        for path, data, name in targets:
            for suffix in ('.uflow-new', '.uflow-old'):
                try:
                    ftp.size(path + suffix)
                except ftplib.error_perm as e:
                    if not str(e).startswith('550'): raise
                else:
                    raise RuntimeError('Unfinished deployment exists: ' + path + suffix)
            originals[path] = None if path in missing_allowed else fetch(ftp, path)
            if originals[path] is not None:
                (backup / ('before-' + name)).write_bytes(originals[path])
            (backup / ('new-' + name)).write_bytes(data)
        report = []
        # Backend first: vanilla WiiFlow behavior is retained until uFlow sends a request.
        for path, data, name in targets:
            if data == originals[path]: continue
            if originals[path] is not None and fetch(ftp, path) != originals[path]:
                raise RuntimeError('Console file changed during deployment: ' + path)
            ftp.storbinary('STOR ' + path + '.uflow-new', io.BytesIO(data), 65536)
            if fetch(ftp, path + '.uflow-new') != data:
                raise RuntimeError('Staged readback mismatch: ' + path)
            if originals[path] is not None:
                ftp.rename(path, path + '.uflow-old')
            try:
                ftp.rename(path + '.uflow-new', path)
            except Exception:
                if originals[path] is not None:
                    ftp.rename(path + '.uflow-old', path)
                raise
            check = fetch(ftp, path)
            if check != data:
                raise RuntimeError('Published readback mismatch; .uflow-old retained: ' + path)
            (backup / ('readback-' + name)).write_bytes(check)
            report.append({'path': path, 'bytes': len(data), 'sha256': digest(data),
                           'previous_sha256': digest(originals[path]) if originals[path] is not None else None})
            # Only remove our exact rollback sibling once PC backup/readback are verified.
            if originals[path] is not None:
                ftp.delete(path + '.uflow-old')
            print('Verified:', path, digest(data), flush=True)
        (backup / 'verified.json').write_text(json.dumps(report, indent=2) + '\n')
    print('PASS. Backups and readbacks:', backup, flush=True)

if __name__ == '__main__':
    main()
