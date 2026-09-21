"""Synchronize built release components, or fail if native packaging is stale.

This mechanically updates the existing personal payload/manifest, never SD files.
Build changed components first. Nintendo bases and user configuration are not inputs.
"""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil

ENTRY = re.compile(r'    \{"([^"\n]+)","([0-9a-f]{64})",(\d+)ULL,(\d+)u,\'([rkb])\'\},')
RAW = 1017  # Wii U and DS/NES/SNES/GBA/N64/PCE/MSX VC selections.
REGION = "wiiu/environments/aroma/plugins/regionfree.wps"
FTPIIU = "wiiu/environments/aroma/plugins/ftpiiu.wps"
FTPIIU_PATCH_HASH = "5a00356ca94f03537c45913bab5a1ad18170d0360be9934a1d624398da36cca3"
PLUGIN_BACKEND = "wiiu/environments/aroma/modules/PluginBackend.wms"
PLUGIN_BACKEND_HASH = "aa564fa55f3e82d8c69df07e5e85b8072949c5fefe3e45a74b7a3064ceae0c5d"
WIIFLOW_BINS = ("app_booter.bin", "ext_loader.bin", "ext_booter.bin")
WIIFLOW_BIN_PATHS = {f"apps/wiiflow/bins/{name}" for name in WIIFLOW_BINS}
WIIFLOW_FORWARDER = "wiiu/apps/WiiFlow.wuhb"
RETURN_RUNTIME = {
    PLUGIN_BACKEND,
    "wiiu/environments/aroma/modules/RPXLoadingModule.wms",
    "wiiu/environments/aroma/modules/uFlowRawKernel.wms",
    "wiiu/environments/aroma/modules/setup/00_mocha.rpx",
}
# WPS payload is compressed: validate the tested patched artifact, not strings.
# Update this pin only after testing a replacement Region Free build.
REGION_PATCH_HASH = "30560de32d190999faddd37a6ac88b430b7bc9f3e7a026adbeac7d439aaca49f"
# User-provided sleeves are release assets, not machine-specific Media paths.
# DS also applies when only the alternative NooDS backend is selected.
DEFAULT_COVERS = {"wiiu": 1 << 0, "ds": (1 << 3) | (1 << 10), "wii": 1 << 1, "gamecube": 1 << 2, "nes": 1 << 4, "snes": 1 << 5, "gba": 1 << 6, "n64": 1 << 7, "pce": 1 << 8, "msx": 1 << 9}


def cover_paths():
    return {f"wiiu/apps/uFlow/media/{system}/fullcovers/Default.png{suffix}": mask
            for system, mask in DEFAULT_COVERS.items() for suffix in ("", ".layout")}


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def inputs(project, sources):
    result = {
        "wiiu/apps/uFlow/uFlow.wuhb": project / "app/uFlow.wuhb",
        "wiiu/environments/aroma/modules/RPXLoadingModule.wms": project / "module/RPXLoadingModule.wms",
        "wiiu/environments/aroma/modules/uFlowRawKernel.wms": project / "kernel/uFlowRawKernel.wms",
        "wiiu/environments/aroma/modules/setup/00_mocha.rpx": project / "payload/00_mocha.rpx",
        REGION: sources / "region_free_plugin/regionfree.wps",
        FTPIIU: sources / "ftpiiu_plugin/ftpiiu.wps",
        PLUGIN_BACKEND: sources / "WiiUPluginLoaderBackend/PluginBackend.wms",
        "wiiu/environments/aroma/modules/setup/99_autoboot.rpx": sources / "AutobootModule/99_autoboot.rpx",
        "apps/wiiflow/boot.dol": sources / "wiiflow-lite/out/boot.dol",
        WIIFLOW_FORWARDER: sources / "Boot2vWii/uFlow-WiiFlow.wuhb",
        "apps/usbloader_gx/boot.dol": sources / "usbloadergx/boot.dol",
        "apps/nintendont/boot.dol": sources / "nintendont/loader/loader.dol",
        "wiiu/apps/noods/noods.wuhb": project / "emulators/NooDS/noods.wuhb",
        "wiiu/apps/noods/uflow-api.txt": project / "launcher-modules/noods-capability.txt",
    }
    for name in ("ds-vc", "nes-vc", "snes-vc", "gba-vc", "n64-vc", "pce-vc", "msx-vc", "noods"):
        result[f"wiiu/apps/uFlow/launchers/{name}.ufmod"] = project / f"launcher-modules/{name}.ufmod"
    for name in WIIFLOW_BINS:
        result[f"apps/wiiflow/bins/{name}"] = sources / f"wiiflow-lite/out/bins/{name}"
    for system in DEFAULT_COVERS:
        result[f"wiiu/apps/uFlow/media/{system}/fullcovers/Default.png"] = project / f"native-installer/assets/default-covers/{system}.png"
        result[f"wiiu/apps/uFlow/media/{system}/fullcovers/Default.png.layout"] = project / f"native-installer/assets/default-covers/{system}.layout"
    return result


def synchronize(project, sources, sync=False):
    native = project / "native-installer"
    manifest = native / "source/generated_files.hpp"
    text = manifest.read_text(encoding="utf-8")
    entries = {m[1]: m for m in ENTRY.finditer(text)}
    if len(entries) != len(list(ENTRY.finditer(text))):
        raise ValueError("Duplicate native manifest entries")
    replacements, copies, stale = {}, [], []
    for path, source in inputs(project, sources).items():
        if not source.is_file():
            raise ValueError(f"Build input missing: {source}; build components and mount sources first")
        hashed, size = digest(source), source.stat().st_size
        if path == REGION and hashed != REGION_PATCH_HASH:
            raise ValueError("Region Free input lacks the uFlow raw-metadata fix")
        if path == PLUGIN_BACKEND and hashed != PLUGIN_BACKEND_HASH:
            raise ValueError("Plugin backend input differs from tested exit-trace build")
        old = entries.get(path)
        if path == FTPIIU and hashed != FTPIIU_PATCH_HASH:
            raise ValueError("FTPiiU input differs from tested early-exit build")
        if old is None and path not in (REGION, FTPIIU, PLUGIN_BACKEND, WIIFLOW_FORWARDER) and path not in cover_paths() and path not in WIIFLOW_BIN_PATHS:
            raise ValueError(f"Required component missing from manifest: {path}")
        if path in WIIFLOW_BIN_PATHS or path == WIIFLOW_FORWARDER:
            mask, mode = 6, "r"  # Both Wii and GameCube need WiiFlow's external booters.
        elif path in cover_paths():
            mask, mode = cover_paths()[path], "k"  # Never replace a user's sleeve.
        elif path in (PLUGIN_BACKEND, FTPIIU):
            mask, mode = 1 << 31, "r"
        else:
            mask, mode = (RAW, "r") if path == REGION else (int(old[4]), old[5])
        if path in RETURN_RUNTIME:
            mask, mode = 1 << 31, "r"  # Core: game return also serves Wii/GC/NooDS.
        line = f'    {{"{path}","{hashed}",{size}ULL,{mask}u,\'{mode}\'}},'
        replacements[path] = line
        dest = native / "content/payload" / path
        if not dest.is_file() or digest(dest) != hashed:
            stale.append(path)
            copies.append((source, dest))
        if old is None or old[0] != line:
            stale.append(f"manifest: {path}")
    font = native / "content/font.ttf"
    if not font.is_file() or digest(font) != digest(project / "app/data/glass.ttf"):
        stale.append("font.ttf")
        copies.append((project / "app/data/glass.ttf", font))
    # Check every unmanaged bundled asset too; do not silently bless altered bases.
    for path, old in entries.items():
        if path in replacements:
            continue
        dest = native / "content/payload" / path
        if not dest.is_file() or dest.stat().st_size != int(old[3]) or digest(dest) != old[2]:
            raise ValueError(f"Unmanaged bundled asset differs from manifest: {path}")
    if stale and not sync:
        raise ValueError("Stale native payload; run tools/sync-native-payload.py --sync:\n" + "\n".join(stale))
    if sync:
        # Inputs and existing assets all checked before changing the payload.
        for source, dest in copies:
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, dest)
            if digest(source) != digest(dest):
                raise ValueError(f"Payload copy readback failed: {dest}")
        updated = ENTRY.sub(lambda m: replacements.get(m[1], m[0]), text)
        added = [line for path, line in replacements.items() if path not in entries]
        if added:
            updated = updated.replace("return {\n", "return {\n" + "\n".join(added) + "\n", 1)
        if updated != text:
            manifest.write_text(updated, encoding="utf-8", newline="\n")
    print(f"PASS native release payload: {len(replacements)} current components, all bundled assets verified")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sync", action="store_true")
    parser.add_argument("--sources-root", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    sources = args.sources_root or Path(os.environ.get("UFLOW_SOURCES_ROOT", root.parent.parent / "tooling/sources"))
    synchronize(root, sources, args.sync)
