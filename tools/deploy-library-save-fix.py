"""Deploy only the library-save fix; preserve the user's settings and ROMs."""
import ftplib
import hashlib
import io
import json
from pathlib import Path

project = Path(__file__).resolve().parents[1]
output = project / 'artifacts/pce-source-save-20260913'
output.mkdir(parents=True, exist_ok=True)
remote = '/fs/vol/external01/wiiu/apps/uFlow/'
app = (project / 'app/uFlow.wuhb').read_bytes()
if not app.startswith(b'WUHB'):
    raise RuntimeError('Invalid app build')

with ftplib.FTP('192.168.1.116', timeout=45) as ftp:
    ftp.login()

    def fetch(name):
        data = io.BytesIO()
        ftp.retrbinary('RETR ' + remote + name, data.write, 65536)
        return data.getvalue()

    previous = fetch('uFlow.wuhb')
    for name, data in [('uFlow.wuhb', previous),
                       ('content_sources.cfg', fetch('content_sources.cfg')),
                       ('content_sources.cfg.tmp', fetch('content_sources.cfg.tmp'))]:
        backup = output / ('before-' + name)
        if not backup.exists():
            backup.write_bytes(data)
    if fetch('uFlow.wuhb') != previous:
        raise RuntimeError('Remote app changed during preflight')
    print('Uploading app only; library settings are unchanged.', flush=True)
    try:
        ftp.storbinary('STOR ' + remote + 'uFlow.wuhb', io.BytesIO(app), 65536)
    except Exception:
        print('Previous app intact:', fetch('uFlow.wuhb') == previous, flush=True)
        raise
    if fetch('uFlow.wuhb') != app:
        raise RuntimeError('App readback mismatch; do not launch')
    report = {'sha256': hashlib.sha256(app).hexdigest(), 'bytes': len(app),
              'remote': remote + 'uFlow.wuhb', 'settings_changed': False}
    (output / 'deployment.json').write_text(json.dumps(report, indent=2) + '\n')
    print('VERIFIED:', report, flush=True)
