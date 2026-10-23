#!/usr/bin/env python3
"""Check built IOSU/loader objects, then pin-check installer payload inputs."""
from pathlib import Path
import subprocess
import importlib.util

root = Path(__file__).resolve().parents[1]
def nm(path, arch="ARM", undefined=False):
    prefix = "arm-none-eabi" if arch == "ARM" else "powerpc-eabi"
    cmd = ["docker", "run", "--rm", "--mount", f"type=bind,source={root.as_posix()},target=/project,readonly",
           "uflow-payload-builder:0.1.0", f"/opt/devkitpro/devkit{arch}/bin/{prefix}-nm"]
    if undefined: cmd.append("-u")
    cmd.append("/project/"+path)
    output = subprocess.check_output(cmd, text=True)
    return {line.split()[-1] for line in output.splitlines() if line.split()}

symbols = nm("payload/source/ios_mcp/ios_mcp.elf")
for name in ["TCPSyslogStartThread", "sTCPSyslog_ProcessIOSUCommands", "wupserver_init", "dkPCHARActivateSmd",
             "socket", "socketInit", "bind", "connect", "listen", "accept", "recv", "recvfrom", "send"]:
    assert name not in symbols, "Forbidden IOSU network symbol: " + name
assert "_Syslog_RouteOutputPatch" in symbols and "_Syslog_FlushHistoryToOutputForUSB" in symbols
calls = nm("module/build-native/main.o", "PPC", True)
assert "Mocha_InitLibrary" in calls
assert "Mocha_StartTCPSyslogLogging" not in calls
assert b"WIIU_TCP_SYSLOG_BEACON_V1" not in (root/"payload/source/ios_mcp/ios_mcp.bin").read_bytes()
spec = importlib.util.spec_from_file_location("sync", root/"tools/sync-native-payload.py")
sync = importlib.util.module_from_spec(spec); spec.loader.exec_module(sync)
for path, expected in sync.NETWORK_DISABLED_INPUTS.items():
    local = root/("payload/00_mocha.rpx" if path.endswith("00_mocha.rpx") else "module/RPXLoadingModule.wms")
    assert sync.digest(local) == expected
    assert sync.digest(root/"native-installer/content/payload"/path) == expected
print("PASS: built IOSU has no TCP syslog, WUP server, devkit network bridge or socket symbols; loader has no startup call; bundled modules match security pins.")
