"""Install only the experimental Cafe launcher after checking the existing SD app."""
import argparse
import ftplib
import hashlib
import io
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True)
    args = parser.parse_args()
    project = Path(__file__).resolve().parents[1]
    expected_app = (project / 'app/uFlow.wuhb').read_bytes()
    candidate = (project / 'artifacts/gamepad-probe/uFlow-GamePad-Probe.wuhb').read_bytes()
    if not candidate.startswith(b'WUHB') or not expected_app.startswith(b'WUHB'):
        raise RuntimeError('Invalid local WUHB')
    remote = '/fs/vol/external01/wiiu/apps/uFlow-GamePad-Probe.wuhb'
    with ftplib.FTP(args.host, timeout=15) as ftp:
        ftp.login()

        def fetch(path):
            data = io.BytesIO()
            ftp.retrbinary('RETR ' + path, data.write, 65536)
            return data.getvalue()

        # Use the external SD mount and fingerprint the installed production
        # app, rather than assume a PC drive letter or write another volume.
        if fetch('/fs/vol/external01/wiiu/apps/uFlow/uFlow.wuhb') != expected_app:
            raise RuntimeError('Existing SD app does not match the verified local build; stopping')
        ftp.storbinary('STOR ' + remote, io.BytesIO(candidate), 65536)
        if fetch(remote) != candidate:
            raise RuntimeError('Diagnostic readback mismatch; do not open the probe')
    report = dict(host=args.host, path=remote, bytes=len(candidate),
                  sha256=hashlib.sha256(candidate).hexdigest(),
                  sd_app_sha256=hashlib.sha256(expected_app).hexdigest())
    (project / 'artifacts/gamepad-probe/launcher-deployment.json').write_text(
        json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('Verified diagnostic launcher:', report)


if __name__ == '__main__':
    main()
