"""Reproduce/check the early-exit patch against pinned FTPiiU 0.4.5."""
import argparse
from pathlib import Path
import subprocess

REVISION = "c2fc7310526124a9d63a8c28feaaca9356d504a6"
EARLY_EXIT = '''ON_APPLICATION_REQUESTS_EXIT ()
{
	// Join the FTP worker and close its files while filesystem workers and
	// the game's runtime are still alive. APPLICATION_ENDS is too late for
	// titles whose exit path has already stopped those services.
	OSReport ("uFlow ftpiiu exit1: early-shutdown-enter\\n");
	stop_server ();
	OSReport ("uFlow ftpiiu exit1: early-shutdown-return\\n");
}

'''
EDITS = [
    ('#define VERSION "v0.4.5"', '#define VERSION "v0.4.5-uflow-exit1"'),
    ('void stop_server ()\n{\n\tserver.reset ();',
     'void stop_server ()\n{\n\tOSReport ("uFlow ftpiiu exit1: server-stop-enter\\n");\n\tserver.reset ();\n\tOSReport ("uFlow ftpiiu exit1: server-joined\\n");'),
    ('\tIOAbstraction::clear ();\n}\n\nstatic void gFTPServerRunningChanged',
     '\tIOAbstraction::clear ();\n\tOSReport ("uFlow ftpiiu exit1: server-stop-return\\n");\n}\n\nstatic void gFTPServerRunningChanged'),
    ('ON_APPLICATION_ENDS ()', EARLY_EXIT + 'ON_APPLICATION_ENDS ()'),
]


def prepare(root, apply=False):
    revision = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    if revision != REVISION:
        raise ValueError("Use the pinned FTPiiU 0.4.5 revision")
    name = "source/wiiu/platform.cpp"
    original = subprocess.check_output(["git", "-C", str(root), "show", f"{REVISION}:{name}"], text=True)
    expected = original
    for before, after in EDITS:
        if expected.count(before) != 1:
            raise ValueError("Ambiguous FTPiiU patch anchor")
        expected = expected.replace(before, after)
    expected = expected.rstrip() + "\n"
    path = root / name
    actual = path.read_text()
    if actual.rstrip() + "\n" != expected:
        if not apply or actual.rstrip() != original.rstrip():
            raise ValueError("Unexpected FTPiiU source changes; refusing to overwrite")
        path.write_text(expected, newline="\n")
    print("PASS: FTPiiU early-exit patch matches pinned upstream; configuration and server behavior otherwise unchanged")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    prepare(args.source, args.apply)
