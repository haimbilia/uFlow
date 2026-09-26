"""Read-only copy of installed vWii IOS/cIOS contents for host analysis.

Reads title.tmd and every content (per-title or /shared1) over FTPiiU, checks
each against the TMD SHA-1 and writes them below an ignored artifacts folder.
Never writes to the console. Output is analysis input, never installable.
"""
import argparse
import ftplib
import hashlib
import io
import json
import struct
from pathlib import Path

NAND = "/slccmpt01"


def fetch(ftp, path):
    data = io.BytesIO()
    ftp.retrbinary("RETR " + path, data.write, 65536)
    return data.getvalue()


def parse_tmd(tmd):
    title, = struct.unpack_from(">Q", tmd, 0x18C)
    version, count = struct.unpack_from(">HH", tmd, 0x1DC)
    contents = []
    for i in range(count):
        cid, index, kind, size = struct.unpack_from(">IHHQ", tmd, 0x1E4 + 0x24 * i)
        sha1 = tmd[0x1E4 + 0x24 * i + 16:0x1E4 + 0x24 * i + 36].hex()
        contents.append(dict(cid=cid, index=index, type=kind, size=size, sha1=sha1))
    return title, version, contents


def shared_map(ftp):
    raw = fetch(ftp, f"{NAND}/shared1/content.map")
    return {raw[i + 8:i + 28].hex(): raw[i:i + 8].decode() for i in range(0, len(raw) - 27, 28)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("ios", nargs="+", help="IOS numbers, e.g. 58 249")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    report = {}
    with ftplib.FTP(args.host, timeout=45) as ftp:
        ftp.login()
        shared = None
        for number in map(int, args.ios):
            base = f"{NAND}/title/00000001/{number:08x}/content"
            tmd = fetch(ftp, base + "/title.tmd")
            title, version, contents = parse_tmd(tmd)
            if title != 0x0000000100000000 | number:
                raise RuntimeError(f"TMD title {title:016x} does not match IOS{number}")
            folder = args.out / f"ios{number}-v{version}"
            folder.mkdir(exist_ok=True)
            (folder / "title.tmd").write_bytes(tmd)
            for content in contents:
                if content["type"] & 0x8000:
                    shared = shared or shared_map(ftp)
                    remote = f"{NAND}/shared1/{shared[content['sha1']]}.app"
                else:
                    remote = f"{base}/{content['cid']:08x}.app"
                data = fetch(ftp, remote)
                if len(data) != content["size"] or hashlib.sha1(data).hexdigest() != content["sha1"]:
                    raise RuntimeError(f"IOS{number} content {content['index']} fails TMD check: {remote}")
                (folder / f"{content['index']:02x}-{content['cid']:08x}.app").write_bytes(data)
                content["remote"] = remote
            report[f"IOS{number}"] = dict(version=version, contents=contents)
            print(f"IOS{number} v{version}: {len(contents)} contents verified against TMD SHA-1")
    (args.out / "fetch-report.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
