"""Reproduce/check the diagnostic-only patch against pinned upstream WUPS 0.4.1."""
import argparse
from pathlib import Path
import subprocess

REVISION = "e3e67f8a8ebee77b351a43ff79561ecb6955dea6"
PATCHES = {
    "source/hooks.cpp": [
        ('            if (func_ptr != nullptr) {\n',
         '            if (func_ptr != nullptr) {\n'
         '                const bool traceExit = hook_type == WUPS_LOADER_HOOK_APPLICATION_ENDS || hook_type == WUPS_LOADER_HOOK_FINI_WUT_SOCKETS || hook_type == WUPS_LOADER_HOOK_FINI_WUT_DEVOPTAB;\n'
         '                if (traceExit) OSReport("uFlow plugin-exit v1: enter hook=%d plugin=%s\\n", hook_type, plugin.getMetaInformation().getName().c_str());\n'),
        ('            } else {\n',
         '                if (traceExit) OSReport("uFlow plugin-exit v1: return hook=%d plugin=%s\\n", hook_type, plugin.getMetaInformation().getName().c_str());\n'
         '            } else {\n'),
    ],
    "source/main.cpp": [
        ('    CallHook(gLoadedPlugins, WUPS_LOADER_HOOK_APPLICATION_ENDS);\n',
         '    OSReport("uFlow plugin-exit v1: backend-end-enter\\n");\n'
         '    CallHook(gLoadedPlugins, WUPS_LOADER_HOOK_APPLICATION_ENDS);\n'),
        ('        OSDynLoad_Release(val);\n',
         '        OSReport("uFlow plugin-exit v1: release-rpl-enter handle=%p\\n", val);\n'
         '        OSDynLoad_Release(val);\n'
         '        OSReport("uFlow plugin-exit v1: release-rpl-return handle=%p\\n", val);\n'),
        ('    gUsedRPLs.clear();\n\n    deinitLogging();\n',
         '    gUsedRPLs.clear();\n\n    deinitLogging();\n'
         '    OSReport("uFlow plugin-exit v1: backend-end-return\\n");\n'),
    ],
}


def prepare(root, apply=False):
    revision = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    if revision != REVISION:
        raise ValueError("Use the pinned upstream WUPS 0.4.1 revision")
    for name, edits in PATCHES.items():
        original = subprocess.check_output(["git", "-C", str(root), "show", f"{REVISION}:{name}"], text=True)
        expected = original
        for before, after in edits:
            if expected.count(before) != 1:
                raise ValueError(f"Ambiguous patch anchor: {name}")
            expected = expected.replace(before, after)
        expected = expected.rstrip() + "\n"
        path = root / name
        actual = path.read_text()
        if actual.rstrip() + "\n" == expected:
            continue
        if not apply or actual.rstrip() != original.rstrip():
            raise ValueError(f"Unexpected source changes: {name}; refusing to overwrite")
        path.write_text(expected, newline="\n")
    print("PASS: plugin exit patch only adds boundary logs; upstream calls and dispatch unchanged")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    prepare(args.source, args.apply)
