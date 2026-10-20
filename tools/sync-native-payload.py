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
TITLE_INSTALLER = "wiiu/apps/uFlow/modules/title-installer.ufmod"
SIGPATCHES = "wiiu/environments/aroma/modules/setup/01_sigpatches.rpx"
# Upstream SigpatchesModuleWiiU release 1.2; loaded after 00_mocha on reboot.
SIGPATCHES_HASH = "41f9409f0c615cf3276e647bcad44a4723389388342f0e2b8994527651a38776"
# User-provided system menu tracks, restored from the SD music folder.
MUSIC_INPUTS = {'GameCube Menu.ogg': '4648ca65fad2285c88828f1ca94d1984fdaef4ee6ada9aaad4de50e2f5aa2993', 'Wii Menu.ogg': 'd35cae2250f40ec95deddc3745d0b7cb3fbe3ab3b1136dbf472ea7465f660536', 'Wii Shop Channel.ogg': '6411fd422c301c1958d4c77cb99eb50a4bfaca1e94f734778f1adb25dbc3c083', 'Wii U Menu.ogg': '3f833c34f3f0066b1aa69472471ae96be418035565d41439c332dee6072bd0c5'}
RAW = 1017  # Wii U and DS/NES/SNES/GBA/N64/PCE/MSX VC selections.
REGION = "wiiu/environments/aroma/plugins/regionfree.wps"
FTPIIU = "wiiu/environments/aroma/plugins/ftpiiu.wps"
FTPIIU_PATCH_HASH = "123495ccb090f76fae6065dc0687f48ebe9dd10c99d13078ecfe82738160ab28"
PLUGIN_BACKEND = "wiiu/environments/aroma/modules/PluginBackend.wms"
PLUGIN_BACKEND_HASH = "aa564fa55f3e82d8c69df07e5e85b8072949c5fefe3e45a74b7a3064ceae0c5d"
MEMORY_MAPPING = "wiiu/environments/aroma/modules/MemoryMappingModule.wms"
MEMORY_MAPPING_HASH = "10f4183b237cbc5211794df9f909866db6ce7fb0832f00d6727350cb4917f73d"
CONTENT_REDIRECTION = "wiiu/environments/aroma/modules/ContentRedirectionModule.wms"
CONTENT_REDIRECTION_HASH = "5944f0394e16d09227843abe2ba27044d653441502bb0b58467e4c2c8065017c"
# Statically linked into the Core app by tools/video-ffmpeg.Dockerfile.
# Rebuild the video builder and app when changing this tested adapter.
H264_ADAPTER = "tools/ffmpeg/h264_wiiu.c"
H264_ADAPTER_HASH = "fb50285dfa6ee75dbe4a45c387b47d11ba8519f90715358469a120ada957be33"
# Statically linked SDL BC3 backend and encoder, included in Core. Legacy bridge is unused.
COVER_CACHE_INPUTS = {'tools/patch-sdl-cover-format.py': 'ce05409a76120e777a662c0bdedbff63ecbfcb7450532f8eff81c04aae37dcde', 'tools/bc3-upload-layout.h': '45c0bb4945912183bbd6b98da6a6f4a66456cf6174a751b7c8b336c19b62d650', 'tools/cover-cache.Dockerfile': 'eccf083768ce4747074e40788901b4d478e4a577333fe986eafa9bbeb65c2a79', 'app/vendor/stb/stb_dxt.h': '807667ef98e0fd749cdb65cca0c2d980bc148109d2fed6f1873c81ae0f449933'}
# Statically linked preview GPU colour shaders and renderer patch.
VIDEO_COLOUR_INPUTS = {'tools/patch-sdl-video-colour.py': 'b9a7689c954f4e0a4c2a1faefd6fe79f3e40eafb03bccd1db2aacbe3ba5d25ab', 'tools/video-colour.Dockerfile': 'b6ee630ca4f53d870d98cc311ddb8a6f169ca11100bf9885dfa12cb62858a157', 'tools/video-shaders/video1.inc': 'd6476c6c722026a3daf4630e3c35f8ca4da664203423f25af07ef043fdce9c00', 'tools/video-shaders/video2.inc': '122c9ff56e9e0c4961bccb8d23e90e0e7241545384af965ae0b555fc9664c311', 'tools/video-shaders/video3.inc': '0abc69c338209e3a35a485ecaca177949d6ca0bf1a37fbac71128521ee9c02d7', 'tools/video-shaders/video4.inc': '1fe6a67cb66c4c74de3dbd405d5a1945cc1c4c138f7ecd2195c8a81b2c21d9bf'}
VIDEO_NV12_INPUTS = {'tools/patch-sdl-video-nv12.py': '69354e8f8147d4030083c3fbd5dc5da0845c6908b2493762d505a40f73b35749', 'tools/video-nv12-backend.c': 'e929d917cee4b32721add2d37b640fb904f89c4b18056c2231f7e2701e8d8965', 'tools/video-nv12.Dockerfile': 'b09be3bf3d994c9701f0fe9e6a9e58d44b89bea9278ef3749e81b7136f926579', 'tools/video-shaders/nv1.inc': 'e3dbcf475559e1a5821e3fa577448c1bad8e7c97c22ff7753a940a0e1a7519ce', 'tools/video-shaders/nv2.inc': '5af9f2b5788a119d68ac27268fcd92f5f53753c6afdcb92e0714d8abb780fcb8', 'tools/video-shaders/nv3.inc': '24117a511f36f0a6c72ce68ea1e32662228fe2232fff605308b6dc0f3337b5ec', 'tools/video-shaders/nv4.inc': '17cc512b6de5d9577439cd9bcb1cfad35b62831c6c38cc38a042d6383a330c0f', 'tools/patch-ffmpeg-buffer-pool.py': '37c86f045704ec8cc5bc30cb3ecadff11baade9b8cab32be48dc4c836c76d942', 'tools/ffmpeg/wiiu-buffer-pool.h': 'a1614dbf0ce39ea2ff7f3c19f0885388707e46f7d5473bab68075c4bc1207340'}
WIIFLOW_BINS = ("app_booter.bin", "ext_loader.bin", "ext_booter.bin")
WIIFLOW_BIN_PATHS = {f"apps/wiiflow/bins/{name}" for name in WIIFLOW_BINS}
WIIFLOW_FORWARDER = "wiiu/apps/WiiFlow.wuhb"
RETURN_RUNTIME = {
    CONTENT_REDIRECTION,
    PLUGIN_BACKEND,
    "wiiu/environments/aroma/modules/RPXLoadingModule.wms",
    "wiiu/environments/aroma/modules/uFlowRawKernel.wms",
    "wiiu/environments/aroma/modules/setup/00_mocha.rpx",
}
# WPS payload is compressed: validate the tested patched artifact, not strings.
# Update this pin only after testing a replacement Region Free build.
REGION_PATCH_HASH = "30560de32d190999faddd37a6ac88b430b7bc9f3e7a026adbeac7d439aaca49f"
# Optional RetroArch components: the Aroma RetroArch build that was
# hardware-verified with one-press launching (PlayStation, bit 13), and the
# Snes9x / Nestopia UE / mGBA cores from the same archive (bits 14-16). The
# frontend ships with any of them. Pinned so a different emulator build can
# never slip into a release unnoticed. Kept ('k') on the SD so an existing
# RetroArch install is never overwritten.
PLAYSTATION, SNES_RETROARCH, NES_RETROARCH, GBA_RETROARCH = 1 << 13, 1 << 14, 1 << 15, 1 << 16
RETROARCH_SYSTEMS = PLAYSTATION | SNES_RETROARCH | NES_RETROARCH | GBA_RETROARCH
RETROARCH_INPUTS = {
    "wiiu/apps/retroarch.wuhb": ("retroarch.wuhb", "487c77a863348c44fa87df34594962cf0390d905594430d09a05e45c67ddc630", RETROARCH_SYSTEMS),
    "retroarch/cores/pcsx_rearmed_libretro.rpx": ("pcsx_rearmed_libretro.rpx", "1bdcae8ef63ce250c31c75468f53be8095de6e5626c32a2a93f9af6825ca3ac4", PLAYSTATION),
    "retroarch/cores/info/pcsx_rearmed_libretro.info": ("pcsx_rearmed_libretro.info", "19e8859d2c4b6b09ef2ccf8b0ef8e4ad2bcdbeede063da33a664719e605b8e75", PLAYSTATION),
    "retroarch/cores/snes9x_libretro.rpx": ("snes9x_libretro.rpx", "33ed3fdad36b59a02e03b0a34b57322b7b4b276cdc422ad742544cdd0349ed95", SNES_RETROARCH),
    "retroarch/cores/info/snes9x_libretro.info": ("snes9x_libretro.info", "fa62b78d58bc4c30f4e0a4a581f0cb3252447b7f198b55bd6ebe2e4b9bde5cc2", SNES_RETROARCH),
    "retroarch/cores/nestopia_libretro.rpx": ("nestopia_libretro.rpx", "9f2fd9b96d3ab614e5c1dbabceecfad82f473a0103a6406a0a5cb11cbc73ab3c", NES_RETROARCH),
    "retroarch/cores/info/nestopia_libretro.info": ("nestopia_libretro.info", "3aedf30570ef2f751eee31d06022243ec5ae13205b1ab17b880a077651c27e87", NES_RETROARCH),
    "retroarch/cores/mgba_libretro.rpx": ("mgba_libretro.rpx", "fc801cb92ff8c48a82d463a4efa522521d1264d27c7450e87b09a5c5b6517125", GBA_RETROARCH),
    "retroarch/cores/info/mgba_libretro.info": ("mgba_libretro.info", "64444beb8268d3a57d53a45564c55049f39ff621315401fef86e82f3849042d6", GBA_RETROARCH),
}
RETROARCH_MODULES = {
    f"wiiu/apps/uFlow/launchers/retroarch-{system}.ufmod": (f"retroarch-{system}.ufmod", mask)
    for system, mask in (("ps1", PLAYSTATION), ("snes", SNES_RETROARCH), ("nes", NES_RETROARCH), ("gba", GBA_RETROARCH))
}
# User-provided sleeves are release assets, not machine-specific Media paths.
# DS also applies when only the alternative NooDS backend is selected, and
# NES/SNES/GBA when only their RetroArch component is.
DEFAULT_COVERS = {"wiiu": 1 << 0, "ds": (1 << 3) | (1 << 10), "wii": 1 << 1, "gamecube": 1 << 2, "nes": (1 << 4) | NES_RETROARCH, "snes": (1 << 5) | SNES_RETROARCH, "gba": (1 << 6) | GBA_RETROARCH, "n64": 1 << 7, "pce": 1 << 8, "msx": 1 << 9}


def cover_paths():
    return {f"wiiu/apps/uFlow/media/{system}/fullcovers/Default.png{suffix}": mask
            for system, mask in DEFAULT_COVERS.items() for suffix in ("", ".layout")}


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def inputs(project, sources):
    result = {
        SIGPATCHES: sources / "SigpatchesModuleWiiU/01_sigpatches.rpx",
        TITLE_INSTALLER: project / "launcher-modules/title-installer.ufmod",
        "wiiu/apps/uFlow/uFlow.wuhb": project / "app/uFlow.wuhb",
        "wiiu/environments/aroma/modules/RPXLoadingModule.wms": project / "module/RPXLoadingModule.wms",
        "wiiu/environments/aroma/modules/uFlowRawKernel.wms": project / "kernel/uFlowRawKernel.wms",
        "wiiu/environments/aroma/modules/setup/00_mocha.rpx": project / "payload/00_mocha.rpx",
        REGION: sources / "region_free_plugin/regionfree.wps",
        FTPIIU: sources / "ftpiiu_plugin/ftpiiu.wps",
        PLUGIN_BACKEND: sources / "WiiUPluginLoaderBackend/PluginBackend.wms",
        MEMORY_MAPPING: sources / "MemoryMappingModule/MemoryMappingModule.wms",
        CONTENT_REDIRECTION: sources / "ContentRedirectionModule/ContentRedirectionModule.wms",
        "wiiu/environments/aroma/modules/setup/99_autoboot.rpx": sources / "AutobootModule/99_autoboot.rpx",
        "apps/wiiflow/boot.dol": sources / "wiiflow-lite/out/boot.dol",
        WIIFLOW_FORWARDER: sources / "Boot2vWii/uFlow-WiiFlow.wuhb",
        "apps/usbloader_gx/boot.dol": sources / "usbloadergx/boot.dol",
        "apps/nintendont/boot.dol": sources / "nintendont/loader/loader.dol",
        "wiiu/apps/noods/noods.wuhb": project / "emulators/NooDS/noods.wuhb",
        "wiiu/apps/noods/uflow-api.txt": project / "launcher-modules/noods-capability.txt",
    }
    for skin in ("pixel", "glass", "switch-light", "switch-dark"):
        result[f"wiiu/apps/uFlow/skins/{skin}/skin.ini"] = project / f"app/content/skins/{skin}/skin.ini"
    for skin in ("switch-light", "switch-dark"):
        for index in range(6):
            result[f"wiiu/apps/uFlow/skins/{skin}/icon-{index}.png"] = project / f"app/content/skins/{skin}/icon-{index}.png"
    for name in MUSIC_INPUTS:
        result[f"wiiu/apps/uFlow/music/{name}"] = project / "native-installer/assets/music" / name
    for name in ("ds-vc", "nes-vc", "snes-vc", "gba-vc", "n64-vc", "pce-vc", "msx-vc", "noods"):
        result[f"wiiu/apps/uFlow/launchers/{name}.ufmod"] = project / f"launcher-modules/{name}.ufmod"
    for name in WIIFLOW_BINS:
        result[f"apps/wiiflow/bins/{name}"] = sources / f"wiiflow-lite/out/bins/{name}"
    for path, (name, _) in RETROARCH_MODULES.items():
        result[path] = project / "launcher-modules" / name
    for path, (name, _, _) in RETROARCH_INPUTS.items():
        result[path] = sources / "retroarch-aroma" / name
    for system in DEFAULT_COVERS:
        result[f"wiiu/apps/uFlow/media/{system}/fullcovers/Default.png"] = project / f"native-installer/assets/default-covers/{system}.png"
        result[f"wiiu/apps/uFlow/media/{system}/fullcovers/Default.png.layout"] = project / f"native-installer/assets/default-covers/{system}.layout"
    return result


def synchronize(project, sources, sync=False):
    adapter = project / H264_ADAPTER
    if not adapter.is_file() or hashlib.sha256(adapter.read_text(encoding="utf-8").encode("utf-8")).hexdigest() != H264_ADAPTER_HASH:
        raise ValueError("H264 adapter differs from the tested stream-sized workspace build")
    for name, expected in (COVER_CACHE_INPUTS | VIDEO_COLOUR_INPUTS | VIDEO_NV12_INPUTS).items():
        source = project / name
        if not source.is_file() or hashlib.sha256(source.read_text(encoding="utf-8").encode("utf-8")).hexdigest() != expected:
            raise ValueError("Renderer dependency differs from the tested build: " + name)
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
        music_name = path.removeprefix("wiiu/apps/uFlow/music/")
        is_music = path.startswith("wiiu/apps/uFlow/music/") and music_name in MUSIC_INPUTS
        if is_music and (hashed != MUSIC_INPUTS[music_name] or not 0 < size <= 32 * 1024 * 1024):
            raise ValueError("Bundled menu music differs from the verified SD track: " + path)
        if path == REGION and hashed != REGION_PATCH_HASH:
            raise ValueError("Region Free input lacks the uFlow raw-metadata fix")
        if path == PLUGIN_BACKEND and hashed != PLUGIN_BACKEND_HASH:
            raise ValueError("Plugin backend input differs from tested exit-trace build")
        if path == MEMORY_MAPPING and hashed != MEMORY_MAPPING_HASH:
            raise ValueError("MemoryMapping input differs from tested heap-gate build")
        if path == CONTENT_REDIRECTION and hashed != CONTENT_REDIRECTION_HASH:
            raise ValueError("ContentRedirection input differs from tested missing-file removal build")
        if path == SIGPATCHES and hashed != SIGPATCHES_HASH:
            raise ValueError("Signature module differs from pinned upstream release 1.2")
        old = entries.get(path)
        if path == FTPIIU and hashed != FTPIIU_PATCH_HASH:
            raise ValueError("FTPiiU input differs from tested early-exit and idle-poll build")
        if path in RETROARCH_INPUTS and hashed != RETROARCH_INPUTS[path][1]:
            raise ValueError(f"RetroArch input differs from the hardware-verified build: {path}")
        new_component = path.startswith("wiiu/apps/uFlow/skins/") or is_music or path in (TITLE_INSTALLER, SIGPATCHES) or path in RETROARCH_INPUTS or path in RETROARCH_MODULES
        if old is None and not new_component and path not in (REGION, FTPIIU, PLUGIN_BACKEND, CONTENT_REDIRECTION, MEMORY_MAPPING, WIIFLOW_FORWARDER) and path not in cover_paths() and path not in WIIFLOW_BIN_PATHS:
            raise ValueError(f"Required component missing from manifest: {path}")
        if path.startswith("wiiu/apps/uFlow/skins/"):
            mask, mode = 1 << 31, "k"
        elif is_music:
            mask, mode = 1 << 31, "k"  # Core music; preserve user replacements and preferences.
        elif path == SIGPATCHES:
            mask, mode = 1 << 17, "k"  # Optional installer; preserve an existing module.
        elif path == TITLE_INSTALLER:
            mask, mode = 1 << 17, "r"
        elif path in RETROARCH_INPUTS:
            mask, mode = RETROARCH_INPUTS[path][2], "k"  # never replace a user's own RetroArch
        elif path in RETROARCH_MODULES:
            mask, mode = RETROARCH_MODULES[path][1], "r"
        elif path in WIIFLOW_BIN_PATHS or path == WIIFLOW_FORWARDER:
            mask, mode = 6, "r"  # Both Wii and GameCube need WiiFlow's external booters.
        elif path in cover_paths():
            mask, mode = cover_paths()[path], "k"  # Never replace a user's sleeve.
        elif path in (PLUGIN_BACKEND, FTPIIU, CONTENT_REDIRECTION, MEMORY_MAPPING):
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
