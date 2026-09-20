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
