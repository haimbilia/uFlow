"""Upload only uFlow.wuhb, backing up the old app and verifying full readback."""
import argparse
import ftplib
import hashlib
import io
import json
import re
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--host', default='192.168.1.116')
parser.add_argument('--artifact-tag', required=True)
args = parser.parse_args()
if not re.fullmatch(r'[a-z0-9-]+', args.artifact_tag):
    raise ValueError('Invalid artifact tag')
project = Path(__file__).resolve().parents[1]
output = project / 'artifacts' / args.artifact_tag
output.mkdir(parents=True, exist_ok=True)
remote = '/fs/vol/external01/wiiu/apps/uFlow/uFlow.wuhb'
app = (project / 'app/uFlow.wuhb').read_bytes()
if not app.startswith(b'WUHB'):
    raise RuntimeError('Invalid app build')

with ftplib.FTP(args.host, timeout=45) as ftp:
    ftp.login()

    def fetch():
        data = io.BytesIO()
        ftp.retrbinary('RETR ' + remote, data.write, 65536)
        return data.getvalue()

    previous = fetch()
    old_hash = hashlib.sha256(previous).hexdigest()
    backup = output / ('before-' + old_hash + '.wuhb')
    if not backup.exists():
        backup.write_bytes(previous)
    if fetch() != previous:
        raise RuntimeError('Remote app changed during preflight')
    print('Uploading app only; settings and ROMs unchanged.', flush=True)
    try:
        ftp.storbinary('STOR ' + remote, io.BytesIO(app), 65536)
    except Exception:
        print('Previous app intact:', fetch() == previous, flush=True)
        raise
    if fetch() != app:
        raise RuntimeError('App readback mismatch; do not launch')
    report = {'sha256': hashlib.sha256(app).hexdigest(), 'bytes': len(app),
              'previous_sha256': old_hash, 'remote': remote, 'settings_changed': False}
    (output / 'deployment.json').write_text(json.dumps(report, indent=2) + '\n')
    print('VERIFIED:', report, flush=True)
