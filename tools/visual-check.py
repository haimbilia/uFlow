"""Request a uFlow frame and fetch it via FTP. Requires opt-in visual-check.key.
No console reboot, title launch, file deletion, or settings changes.
"""
import argparse
import ftplib
import pathlib
import socket

SCREENS = ('capture', 'library', 'details', 'details25', 'details50', 'details75',
           'settings', 'sources', 'sourceedit', 'media', 'mediaedit', 'mediaguide', 'audio',
           'directories', 'missing', 'live', 'stats')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', default='192.168.1.138')
    parser.add_argument('--key', required=True, type=pathlib.Path)
    parser.add_argument('--screen', choices=SCREENS, default='capture')
    parser.add_argument('--output', type=pathlib.Path)
    args = parser.parse_args()
    if args.screen != 'stats' and args.output is None:
        parser.error('--output is required for screenshots')
    token = args.key.read_text().strip()
    if len(token) != 32 or any(c not in '0123456789abcdef' for c in token):
        parser.error('key must contain 32 lowercase hexadecimal characters')
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as connection:
        connection.settimeout(20)
        connection.connect((args.host, 32180))
        connection.send(f'{token} {args.screen}'.encode())
        reply = connection.recv(1024)
    if args.screen == 'stats':
        import json
        result = json.loads(reply)
        print(json.dumps(result, indent=2))
        return
    if reply != b'OK visual.png':
        raise RuntimeError(f'Capture failed: {reply!r}')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with ftplib.FTP(args.host, timeout=20) as ftp:
        ftp.login()
        with args.output.open('wb') as output:
            ftp.retrbinary('RETR /fs/vol/external01/wiiu/apps/uFlow/visual.png', output.write)
    print(f'Captured {args.screen}: {args.output}')

if __name__ == '__main__':
    main()
