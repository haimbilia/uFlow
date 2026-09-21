# Native installer release workflow

Every shipped uFlow fix must survive a fresh installation. A one-off SD edit is
not a completed release: keep the native payload, defaults, tests and packaging
recipe current. Preserve existing user settings, ROMs and saves on reinstall.

WiiFlow requires all three matching `out/bins` files alongside its executable:
`app_booter.bin` (Nintendont/homebrew), `ext_loader.bin` and `ext_booter.bin`
(Wii games). Payload sync installs them under `apps/wiiflow/bins` in replace
mode for either Wii or GameCube selection (mask 6). Shipping `boot.dol` alone
passes the game's preflight but fails the subsequent loader handoff.

1. Build and test each changed component with its appropriate Wii U builder.
2. Run `tools/build-native-installer.ps1` from PowerShell. This synchronizes the
   latest app, launchers, raw loader/kernel, patched Region Free plugin, emulator
   backends and font, updates the manifest, and builds the Wii U installer.
3. Run `python tests/test_native_payload_sync.py` and the native installer host
   test with the actual content bundle. Cover clean install, replacement of stock
   dependencies, component selection, preserved settings and idempotent rerun.
4. Copy the resulting `native-installer/uFlow-Setup.wuhb` to the verified SD card
   at `wiiu/apps/uFlow-Setup.wuhb`, flush and compare complete SHA-256 readback.
   Updating the installer is not permission to erase the card or run console setup.

`native-installer/Makefile` refuses stale payloads. Direct Docker builds require
the project mounted at `/project`, workspace `tooling/sources` at `/sources`
(read-only), and `UFLOW_SOURCES_ROOT=/sources`. Run payload synchronization first;
the guard intentionally verifies, rather than silently changing release inputs.

The Region Free patch is selected alongside Wii U or any VC system (mask1017).
It replaces stock `regionfree.wps` but never installs or overwrites its JSON
configuration. Wii/GC/NooDS-only installs leave the plugin alone. Auto-detection
is enabled by default in stock Aroma; intentionally disabled user settings stay
disabled. Bad/missing game metadata still produces a region prompt.

Region Free's tested binary hash is pinned in `sync-native-payload.py`; update
that pin only when a replacement plugin build has been tested. Every new external
patched dependency must be added to the same source-to-payload map and tests.
Original Nintendo VC bases are integrity-checked, not silently refreshed by sync.

N64 injection selects the base's configuration by normalized cartridge ID,
region and revision, not the donor ROM filename. A user ROM.ini takes precedence;
unknown games get generic options without donor-specific cheats/patches. Selected
profile/version participates in the build key; old saved N64 generations without
the small profile-version marker require a one-time rebuild. Normal launches
still do not hash the ROM or entire injection. Run tools/test-vc-formats.sh with
UFLOW_VC_SYSTEMS=n64 and UFLOW_TEST_N64_ROM pointing to a real cartridge for the
cross-game profile regression. Preserve the ROM/save namespace when repairing an
existing package, and validate every new generation before changing game.cfg.

Return-to-uFlow uses a one-shot SD request written only when uFlow launches a
game/emulator. The loader consumes it from Wii U Menu's foreground ProcUI pump,
after the previous title has closed. uFlow clears it at entry; launch errors
cancel it. Missing/malformed requests or a missing app leave the normal Menu.
This does not enable start-on-boot or change NAND/default system titles. Wii/GC
backends retain their Wii U return route; the request survives that transition.
RPXLoadingModule, its kernel dependency and Mocha now ship as Core, including
Wii/GC/NooDS-only installs. Test `tests/return_to_uflow_test.cpp` as well as the
actual installer bundle. Hardware game-close and explicit uFlow-exit tests remain
required after any return-hook change.

The loader shares `app/source/return_to_uflow.hpp`. With its existing Docker
dependency paths, mount `module` at `/project` and `app` read-only at `/app`, then
run `make -j4` in `/project` using `wiiu-loose-title-module-builder:0.1.0`.
Run `tools/test-wii-launch.sh` in the host test image for return/startup regressions.
`tools/test-return-hook.ps1` additionally compiles the actual production hook
against host stubs and checks its process restriction and foreground/exit behavior.

The old Windows-installer preparation scripts are historical, not the release
entry point. The current personal native bundle embeds user-supplied Nintendo VC
bases and must not be publicly redistributed. No PC-installed user ROMs or save
files belong in the release payload.

Default sleeves live in `native-installer/assets/default-covers/{system}.png`.
`sync-native-payload.py` copies them unchanged into each selected system's
`wiiu/apps/uFlow/media/{system}/fullcovers/Default.png`; these entries use keep
mode, not replace. DS art is included for either DS VC or NooDS. Add further
supplied systems to `DEFAULT_COVERS` with their component mask, then rebuild.
Generated media settings select the supplied default; existing settings stay
untouched. Full-cover sources with no explicit default now discover Default.png
(case-insensitive) during scanning, so copying the file and rescanning is enough.
An explicit different default retains precedence; disabling a source disables its
fallback too. No automatic fallback is introduced for video/audio/metadata.

Source DS icons and GameCube disc banners are decoded only when real artwork is
missing, fitted without distortion and cached under `cache/rom-art`. No injected
donor artwork is used. Wii banner extraction and older cartridge screenshot
generation are not implemented. The library cache version is bumped once for
the new artwork precedence. Test `rom_art`, `media` and the installer payload.

Separate cover faces: setup now creates `front`, `back`, `spine`, `fullcovers`
under each selected system. Existing `covers` sources remain supported as front
art; saved config tokens `front` and `full cover` are aliases. Existing configs
are kept; existing sibling face directories of registered full-sleeve sources
are discovered without duplicate entries or re-enabling explicit disabled rows.
`case_profiles.hpp` is shared by app and installer so dimensions agree. Legacy
DVD-size defaults upgrade on load; custom sizes and versioned saved sizes stay.
Keep `tests/cover_faces_test.cpp`, the actual native bundle tests and the
`coverfacechecks` host preview green when changing these paths.

All ten default PNGs have companion `.layout` authoring metadata, shipped in keep
mode as `Default.png.layout`. It records original byte length and normalized
back/spine/front cuts; wrong-size or invalid metadata is ignored. Never overwrite
a user's custom PNG to apply a layout. The scan reads each unique layout once,
caches the resolved cuts with media paths, and render does no metadata I/O.
Separate faces and sleeve regions are fitted to their case dimensions; aspect
differences over 8% are letterboxed rather than distorted. Full sleeves take
priority over separate faces; absent/failed faces fall back independently.

The bundled N64/SNES defaults are now recomposed as landscape sleeves, GBA as
a square-face sleeve, TurboGrafx-16 as a jewel-case sleeve, and MSX with its wider
spine. PNGs are the edited artwork, not padded portrait images. The authoring
cuts match the final files; `DefaultCoverGeometryTest` in
`tests/test_native_payload_sync.py` checks all ten real PNGs and all thirty face
regions against the renderer's fit tolerance, including byte-length guards.
Built-in image edit prompts are recorded in
`native-installer/assets/default-covers/format-edit-prompts.md`.

NES dimensions now follow the user's 3366x2100 sleeve reference, normalized to
130:178:25 (front width : height : spine), rather than the earlier assumed
124:178:25. These are image-derived ratios, not verified millimetres. `.nes`,
`.unf`, `.unif` and the NES VC launcher all select this preset. Existing artwork
already fits, so its PNG and authored cut metadata are unchanged.

Raw Wii U games with local RPL imports (e.g. Mario Kart 8's `mvplayer.rpl`)
need IOSU-side code-directory lookup before APPLICATION_STARTS/PPC redirection.
Mocha derives a one-launch scope from the prepared loose RPX and its matching
raw-profile executable. It serves only foreground process-code RPL requests;
CafeOS and explicit Aroma replacements retain precedence. The scope survives
the last RPX chunk but is cleared by a new title, preparation, cancellation or
profile change. MCP retains only the profile's executable token to stay within
its existing BSS allocation. Run `tools/test-raw-rpl.ps1` (production dispatch
and lifecycle under ASan/UBSan), rebuild `payload/00_mocha.rpx`, then follow the
installer workflow above. After SD deployment, a full reboot into Aroma is
required to activate this payload; relaunching uFlow alone is insufficient.

Rotating cover faces use `cover_projection.hpp`: interpolate planar samples
with their original clip W, then project a bounded, adaptive mesh for SDL's
affine triangle renderer. A single projected quad bends artwork along its
diagonal during yaw. `tests/cover_projection_test.cpp` checks 18,150 panel poses,
all system shapes, centre stability, cropped UVs and subpixel interpolation.
The host `casechecks` uses a ruled sleeve to expose bending, in both skins;
also run `coverfacechecks` for the fallback/spine layout. Rotation poses, zoom,
case geometry and source images are unchanged. Rebuild the app and installer.

The signed app updater is embedded in uFlow, including the public trust key and
TLS root certificates. Increment `app/source/update_model.hpp` for releases and
rebuild both app and native installer; payload sync carries the new executable
into fresh installs. Run `tools/test-updater.sh`, `tests/test_update_release.py`
and the host `updatechecks` UI suite before packaging. Version 1 intentionally
updates the app only; Aroma/backends still require the installer workflow.
The initial test SD uses sequence 2026092000 so it can accept server release
2026092001; the installer contains the latter. See `updates/README.md` for the
recovery entry and remaining hardware checks. Never ship the private signing key.

Updater correction: active WUHB files cannot be renamed while Aroma's content
mount holds them open. `app/Makefile` now builds `update-helper` first and embeds
its bare RPX. The app verifies/stages a signed request without renaming itself;
the helper installs after the app exits and reopens uFlow. Keep the helper build
dependency and handoff tests when changing packaging. The corrected test pair is
helper-enabled sequence 2026092001 on SD and release 2026092002 on the server;
the refreshed native installer contains 2026092002. Both the SD and installer
must include the embedded helper. Do not ship the previous no-helper bootstrap.

Test2 exposed Aroma's intentional inherited content remount for bare RPX hot
reloads. The helper must explicitly call RPXLoader_UnmountCurrentRunningBundle
before recovery/commit and stop on failure. Its font/code are self-contained.
Test3 checks this ordering and failure gating on host; the SD bootstrap now has
sequence 2026092002 and the server/installer have 2026092003. A host pass does not
establish that console bundle handles have been released; hardware retest is required.

The user confirmed test3 installs and reopens successfully on hardware. Test4
(sequence 2026092004) adds scoped white outer glow to GlassFlow update, injection
and welcome popup text. The glyph mask is cached and reused, with the crisp dark
face rendered last; Pixel Deck and non-popup styling are unchanged. Run the host
`updatechecks` preview (including Pixel Deck and scope-restoration checks), rebuild
the app and native installer, and publish the signed software-only OTA. Do not
replace the installed app on SD when the requested test is the OTA path itself.

Test5 (sequence 2026092005) adds explicit ../ entries to library/media browsers.
Back traverses to fs:/ and stays there; X/Cancel exits. Container roots cannot be
saved as library folders. Default-file selection remains independent of the
parent row. Run browser_path and browserchecks plus mediadefaultchecks before
release. This is navigation only, not new USB filesystem or mounting support.

Test6 adds a virtual vol entry at fs:/ even when namespace enumeration fails.
At fs:/vol, known external volume paths are probed with opendir and merged with
enumerated entries, without listing inaccessible drives or mounting anything.
Library and default-media pickers share this behavior. browser_path tests cover
unlistable parents, absent volumes and deduplication; browserchecks enters vol
from the real listing instead of injecting a synthetic root fixture.

Wii/GameCube selections also install wiiu/apps/WiiFlow.wuhb (mask 6).
tools/build-wiiflow-forwarder.ps1 builds Boot2vWii from tooling/sources/Boot2vWii
(upstream https://github.com/WiiDatabase/Boot2vWii, source commit
5c4f712dd1bfc66c833d27d9f1af64117feba936, Unlicense). It targets DWFA
0001000144574641 with BOTH screens, no forced resolution, and isolated objects.
This Wii U Menu shortcut needs the existing vWii DWFA channel and SD WiiFlow;
it skips Wii Menu/Wiimote prompts but does not emulate GamePad controls. Keep
normal vWii mode for the USB diagnostic, not Wii VC injection mode. No NAND
installation is performed when deploying this WUHB to an already configured console.

USB access test7 embeds a read-only FatFs driver in uFlow. It reads one FAT32
USB disk via libmocha raw-sector reads and exposes usb1:/ at the browser root.
It does not mount FAT over a system volume, write sectors, alter the SD mount,
or extend the game launchers yet. 512-byte sector disks are the initial target.
All filesystem calls are serialized; aligned sector buffers handle arbitrary
caller alignment. usb-storage.log on SD records mount/read failures. Use
tools/test-usb-storage.sh and browserchecks; test physical USB browsing before
claiming support. The embedded code follows the app into fresh native installs.

Direct raw USB test8 adds a module-owned read-only FatFs device (`ufusb:/`) for
retail content/code/meta redirection. Close its process-owned FSA handles on
APPLICATION_ENDS, then reopen and check the original disk on the target process.
The app's separate `usb1:/` browsing device must not own runtime callbacks.
Before launching, the module passes bounded RPX/RPL FAT allocation metadata to
MCP (64 code files, 4096 extents maximum). MCP reads executable bytes directly
from those USB sectors, including fragmented files; no game data is staged on SD.
Read-only maps are scoped to the selected raw launch and cleared on cancellation,
profile replacement, or the next title. MBR/boot fingerprints detect swapped disks.
Game saves continue in the existing SD raw-saves namespace.
Run tools/test-usb-raw.ps1, tools/build-usb-raw.ps1, rebuild the app, then the native
installer and actual-bundle host tests. The module build needs sibling /app and
/payload mounts; the script provides them. Deploy app, RPXLoadingModule.wms and
setup/00_mocha.rpx together and fully reboot into Aroma. Host tests do not prove
retail USB launch compatibility; test executable startup, content reads, HOME,
return-to-uFlow, and an SD raw launch on hardware. Do not publish an app-only OTA
as a complete direct-USB upgrade.

The first hardware USB launch reached the game but exposed an SD-only native
read bypass incorrectly prepending /vol/external01/ to usb1:/ content paths.
RawNativeReadPath now accepts only SD-relative roots; USB opens retain their
virtual paths for the module-owned ContentRedirection device. Native SD save
opens remain unchanged. tools/test-usb-raw.ps1 includes raw_native_path_test.cpp
with the captured alldata.psb.m/code paths and USB-title SD save regression.
This correction needs a rebuilt module and native installer; existing test8 app
and Mocha binaries remain compatible. Fully reboot Aroma after replacing the module.

Menu-exit investigation: hardware traces show completed application/USB cleanup
but a stall before Menu's app-entry callback, including with USB disconnected
and the Menu return hook disabled. Raw filesystem/save/frame hooks now target
FP_TARGET_PROCESS_GAME, not every system process. Their active-title guards
remain in place within that process. The Menu-only return hook is enabled again.
Run tests/test_raw_hook_scope.py and tools/test-return-hook.ps1 with USB/SD tests.
Retain startup/shutdown trace checkpoints until hardware confirms Menu entry and
game return; host tests alone do not establish that this fixes the observed hang.

The game-only hook candidate still hung on hardware. The user confirmed an older
install on another SD exits normally on the same console. The current capture
contains MCP Menu-load reset/lookup messages (some malformed console log bytes
made ordinary text search miss them); it stops before the replacement path is
reported. MCP now constructs environment/MLC/SD paths with bounded MCPJoinPath,
without the fixed-address snprintf import. Missing optional Menu overrides must
fall through to the installed Menu. Unterminated/oversized paths fail before I/O,
not after silently truncating to a different file. Diagnostics no longer use the
unsupported printf string-precision format. This is a hardware candidate, not a
confirmed resolution. tools/test-mcp-replacements.ps1 runs the actual replacement
loop and path/I/O routing with only firmware entrypoints/environment memory
mocked, including Menu bootstrap, missing overrides, all eleven lookup records,
MLC regional backups and malformed fields under ASan/UBSan. It is part of the USB
raw suite. Rebuild Mocha and the native installer; fully reboot for the retest.

Hardware confirmation (2026-09-20): after deploying the bounded-path payload
(SHA-256 816804F55F9C78E1E4B627546C9EEE23C120E95B21445A7B29DC28C4BAA81CAC)
and fully rebooting, the user confirmed both USB-game exit and explicit uFlow
exit work. This resolves the tested Menu-exit regression. The refreshed native
installer already embeds this exact payload and passed the actual-bundle install
and idempotent-rerun tests; do not revert it to the earlier formatter-based build.

Cover-browsing test9 (sequence 2026092009) moves artwork reads/decompression to
a low-priority CPU worker. Each frame replaces its bounded queue with currently
wanted covers; obsolete results are discarded. Only the UI thread uploads textures,
at most one completed image per frame. Neighbour prefetch no longer decodes their
unused backgrounds/logos. Cache reset invalidates in-flight results, and shutdown
joins the worker before renderer/storage teardown. Run tools/test-artwork-worker.ps1
(production worker under ASan/UBSan) and GPU host previews asyncartworkchecks,
artworkchecks, casechecks, coverfacechecks. Rebuild the app and native installer;
the hardware-confirmed USB payload/module must remain unchanged. V-sync/flip waits
and cover geometry are unchanged. Hardware checks still required: fast scrolling,
direction reversals, cover inspection, video transitions, and app/game exit.

Large-folder browser correction (sequence 2026092101): directory and default-file
pickers enumerate on a cancellable CPU worker, not the render/input thread.
Parent, Cancel and Use Folder remain usable during enumeration; obsolete listings
cannot replace a newer path. devkitPPC's readdir stat/type metadata is reused to
avoid a second FAT name lookup for every entry; unknown types retain stat fallback.
Show loading and read-error status. Cancel/join workers before storage teardown.
Run tools/test-browser-worker.ps1 (10,000-file fixture and ASan/UBSan lifecycle),
browser_path/library/media tests, and asyncbrowserchecks/browserchecks/
mediadefaultchecks host previews. Rebuild app and native installer; keep the tested
USB module and Mocha unchanged. Hardware retest target: usb1:/Decrypted; host tests
do not establish responsiveness of a failing physical disk or firmware I/O call.

Scan progress (sequence 2026092102): Scan Now and uncached startup run the scanner
on a worker. The modal reports stage, current path, games found and elapsed time;
unknown discovery totals use an animated bar, artwork matching uses completed /
total games. Do not fabricate a whole-scan percentage. Input/settings changes and
launches are blocked while scanning, but rendering and system events continue.
B requests cooperative cancellation before the cache commit; interrupted/failed
scans never replace the displayed library or save partial scan results. System
background/exit joins the worker before storage teardown. Final cache-save failure
is reported separately from successful in-memory discovery (including the existing
8192-entry cache bound). Directory handles are RAII-owned across observer unwinds.
Run tools/test-scan-worker.ps1, library/media/browser regressions and scanchecks
GPU preview in both skins; rebuild and test the native installer. The USB module
and Mocha are unchanged. Physical USB scan timing/progress requires console testing.

USB scan cache (sequence 2026092103): the app's read-only FatFs sector reader now
keeps a bounded 2 MiB, four-way cache. Absolute metadata paths previously reread
the large parent directory for each lookup. Cache only requested sectors, preserve
bounded multi-sector reads and aligned firmware buffers, and invalidate on failed
reads or raw-device close/remount. All access remains under the filesystem mutex.
This is app-only: do not change the hardware-confirmed game module or Mocha.
Scan-start/end diagnostics record cumulative cache hits and physical reads in
usb-storage.log. Run tools/test-usb-read-cache.ps1 (ASan/UBSan plus production
FatFs against 3,000 long-name folders), USB storage and scan-worker/library tests.
The synthetic fixture reduced physical read calls from 27,075,002 to 10,138 with
identical metadata; this is not a physical-console elapsed-time claim. Rebuild
the app and native installer, run actual-bundle install/rerun tests, and measure
the user's usb1:/Decrypted scan on hardware before claiming a timing improvement.

Wii VC cancellation (sequence 2026092104): raw Wii VC preparation shares the
controller/touch/system-event pump with cartridge injection. Poll at every progress
checkpoint, independently of the 80 ms rendering throttle; accept B held or newly
pressed, and re-arm the neutral-input gate after preparation. Check cancellation
during source inspection and each 256 KiB conversion/readback slice. Cancel/error
closes and removes only the current conversion's partial staging files, never the
source, saves or a published cache. Run tools/test-wii-vc-cancel.ps1 against the
actual preparer with synthetic encrypted NFS under ASan/UBSan, plus injectionchecks
in both skins. Rebuild the app and native installer and test the actual payload.
GamePad/Wii Remote/Pro B and touch cancel require console retesting; an in-flight
firmware read/write must still return before cooperative cancellation can run.
This does not change vWii launch, GamePad support inside Wii games or return hooks.

Freedom Planet Event Preview exit investigation (2026-09-21): the reproduced
USB-game hang reaches every raw-loader cleanup checkpoint, including USB unmount,
but never reaches Menu application-start or consumes the return request. Loader
raw-port11 adds raw-session-latched OSReport checkpoints around ProcUIShutdown,
coreinit _Exit, module-end completion and the all-modules exit boundaries. The
hooks are game-process-only and forward calls, arguments and statuses unchanged.
Do not force termination, skip save flushing or alter USB/MCP routing based only
on the generic Menu icon. These diagnostics are NOT a confirmed hang fix.
Run tools/test-raw-exit-trace.ps1, tools/test-return-hook.ps1 and the USB raw suite;
rebuild/test the native installer. Deploy the module and installer to the verified
SD, then fully reboot Aroma and capture live syslog while reproducing the exit.
Leave the hardware-confirmed bounded-path Mocha payload unchanged. Once the
blocking boundary is identified, implement and hardware-test the actual fix.

Port11 hardware result: ProcUIShutdown returns twice, coreinit _Exit is entered,
and module-end-return is logged. all-module-end-callbacks-done is never reached.
The loaded module order puts WiiUPluginLoaderBackend after RPXLoadingModule.
The next diagnostic build adds per-plugin enter/return logs for APPLICATION_ENDS,
socket/devoptab finalization, and acquired-RPL release in PluginBackend 0.4.1.
It does not disable plugins, skip callbacks, change their order or force exit.
Reproduce the patch with tools/prepare-plugin-exit-trace.py against upstream
revision e3e67f8a8ebee77b351a43ff79561ecb6955dea6 (v0.4.1); --apply refuses any
non-stock/non-patched source. Without --apply it checks exact patch equivalence.
Build using that revision's Dockerfile and make -j4. The tested diagnostic binary
is hash-pinned by sync-native-payload.py, installed as Core in replace mode, and
covered by the native path allowlist and actual-bundle replacement/rerun tests.
Keep the port11 RPX loader and current Mocha unchanged for this isolated test.
This is still diagnostic: obtain the plugin-specific last checkpoint on hardware
before claiming the underlying exit hang is resolved.
