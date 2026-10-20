"""Remove idle poll allocations from the pinned FTPiiU source, preserving exit2."""
import argparse
from pathlib import Path
import subprocess

REVISION = "c2fc7310526124a9d63a8c28feaaca9356d504a6"
BEFORE = "\tauto const pfd = std::make_unique<pollfd[]> (count_);"
AFTER = """\t// The listening socket is polled even when FTP is idle. Avoid taking
\t// Aroma's shared mapped-heap lock on every background poll. Keep a
\t// bounded stack buffer and retain heap storage for larger session sets.
\tpollfd local[16];
\tstd::unique_ptr<pollfd[]> allocated;
\tif (count_ > 16)
\t\tallocated = std::make_unique<pollfd[]> (count_);
\tauto *const pfd = allocated ? allocated.get () : local;"""


def prepare(root, apply=False):
    revision = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    if revision != REVISION:
        raise ValueError("Use the pinned FTPiiU 0.4.5 revision")
    name = "source/socket.cpp"
    original = subprocess.check_output(["git", "-C", str(root), "show", f"{REVISION}:{name}"], text=True)
    if original.count(BEFORE) != 1 or original.count("::poll (pfd.get (),") != 1:
        raise ValueError("Unexpected FTPiiU polling implementation")
    expected = original.replace(BEFORE, AFTER).replace("::poll (pfd.get (),", "::poll (pfd,")
    path = root / name
    actual = path.read_text()
    if actual != expected:
        if not apply or actual != original:
            raise ValueError("Unexpected FTPiiU socket edits; refusing to overwrite")
        path.write_text(expected, newline="\n")
    print("PASS: FTPiiU idle polling uses bounded stack storage; larger session sets retain dynamic storage")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    prepare(args.source, args.apply)
