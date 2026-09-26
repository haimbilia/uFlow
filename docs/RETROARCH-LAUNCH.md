# One-press RetroArch launching

uFlow starts a RetroArch game directly: A on a title boots that game, with no
stop in RetroArch's menus. Hardware-verified 2026-09-24 with Crash Bandicoot
and the Aroma PCSX ReARMed core.

| System | Module | Launcher id | Core | Extensions |
|---|---|---|---|---|
| PlayStation | `retroarch-ps1.ufmod` | `ps1` | PCSX ReARMed | `.cue,.chd,.pbp` |
| SNES | `retroarch-snes.ufmod` | `snes-retroarch` | Snes9x 1.61 | `.sfc,.smc` |
| NES | `retroarch-nes.ufmod` | `nes-retroarch` | Nestopia UE 1.51.1 | `.nes,.unf,.unif,.zip` |
| Game Boy Advance | `retroarch-gba.ufmod` | `gba-retroarch` | mGBA 0.10-dev | `.gba` |

All cores come from the same community Aroma archive. SNES, NES and GBA need no
separate BIOS (mGBA falls back to its built-in GBA BIOS); PlayStation BIOS setup
is described below. SNES, NES and GBA are alternatives to the
Virtual Console injectors for the same content types; the Launch Options popup
(Minus) offers both. Only PS1 has been tested on hardware so far.

## How RetroArch receives a game on Aroma

The Aroma port of RetroArch does not read a command line or the old Homebrew
Launcher block at `0x00801380` (on Aroma that address holds the running
program's code). It passes arguments natively:

1. `frontend_wiiu_exec()` builds a buffer of NUL-separated argument strings.
2. It prepares the core with `Mocha_PrepareRPXLaunch()`.
3. It puts the buffer in `SYSStandardArgsIn` and calls
   `Mocha_LaunchHomebrewWrapperEx()`.
4. The new process calls `SYSDeserializeSysArgs()`, handles `sys:pack`, and
   `get_arguments()` rebuilds argc/argv for `rarch_main()`.

uFlow sends the same two strings through the same call.

## Core and content provider

A RetroArch "core" on the Wii U is a complete, separately linked executable in
`retroarch/cores/*_libretro.rpx`. RetroArch's menus, fonts and other assets live
in `wiiu/apps/retroarch.wuhb`, which the loader mounts as `/vol/content`.

So a launch combines two files:

| Role | File | Form |
|---|---|---|
| Executable | `retroarch/cores/pcsx_rearmed_libretro.rpx` | a bare RPX |
| Content provider | `wiiu/apps/retroarch.wuhb` | its `/content` becomes `/vol/content` |

Preparing a bare RPX normally inherits whichever bundle is mounted, which from
uFlow is uFlow's own, and RetroArch would start without its assets. The loader
export below prepares the core and then points the **pending** mount at
RetroArch's bundle; the live mount keeps serving uFlow until it exits.

## Module manifest

`launcher-modules/retroarch-ps1.ufmod`, installed to
`wiiu/apps/uFlow/launchers/retroarch-ps1.ufmod`:

```ini
UFLOW-MODULE-1
id=ps1
name=PlayStation (RetroArch)
version=1.0.0
system=PlayStation
extensions=.cue,.chd,.pbp
executable=retroarch/cores/pcsx_rearmed_libretro.rpx
protocol=retroarch-sysargs-v1
content_bundle=wiiu/apps/retroarch.wuhb
```

Rules for `retroarch-sysargs-v1`:

- `executable` must be an SD-relative `.rpx` under `retroarch/cores/`.
- `content_bundle` must be an SD-relative `.wuhb` under `wiiu/apps/`, and is
  accepted for this protocol only.
- `request`, `dependency` and `capability` must be empty: the game travels as a
  native argument, so there is no request file to consume.
- The launcher stays `LauncherKind::Emulator`; dispatch is by protocol.

The manifest only supplies defaults. Nothing is attached silently beyond the
one documented migration below, and every choice is visible in Settings >
Launchers, whose edit page lists settings only:

| Row | Meaning |
|---|---|
| ROM EXTENSIONS | editable; file types this launcher accepts |
| CORE (.RPX) | A opens the SD file picker; the core can live anywhere on the SD |
| LAUNCH MODULE | left/right picks NONE or a compatible module; NONE sticks |
| RETROARCH (.WUHB) | A opens the picker for RetroArch's bundle |

Saved values win over the module on every load: the core path, the bundle
(an optional ninth field in `launchers.cfg`, written only where used) and the
extensions. LAUNCH MODULE set to NONE is stored as `-` in the module field and
is never re-attached automatically; for a RetroArch launcher the executable then
becomes RetroArch's `.wuhb`, so it opens RetroArch's menu. RetroArch's protocol
can be picked for any emulator entry (for example a second launcher pointed at
the SNES9x core); a `rom-request-v1` module only for its own emulator. Each
launcher's readiness ("READY - ONE-PRESS LAUNCH", or the missing file) is shown
in the launcher list. The loader accepts any SD `.rpx` core and any SD `.wuhb`
bundle; a core switch inside RetroArch is adopted only for cores in the
launched core's folder or in `retroarch/cores/`.

Adding a file type takes two places: the launcher's ROM EXTENSIONS (so it is
offered for that file) and the content type's EXTENSIONS (so the library scan
finds it).

A launcher entry named `ps1` that pointed at the RetroArch bundle for the same
extensions (the setup made before this module existed) is attached to the
module automatically when loaded. Its name, enabled state and content type are
untouched. Customised entries that don't match that shape are left alone.

Another system needs only another manifest with its own verified Aroma core
using the same protocol; SNES, NES and GBA were added that way (table above),
each with an installer component and a pinned core.

## Arguments

`app/source/retroarch_args.hpp` packs exactly:

```
fs:/vol/external01/retroarch/cores/pcsx_rearmed_libretro.rpx\0
fs:/vol/external01/wiiu/roms/ps1/Crash Bandicoot/Crash Bandicoot.cue\0
```

- Both terminating NULs are counted in `SYSStandardArgsIn.size`.
- No quoting: spaces, apostrophes and UTF-8 stay as they are.
- No `-L`: the chosen RPX already is the core.
- Paths use the `fs:/vol/external01/` form inside argv, SD-relative form for
  loader preparation, and `/vol/external01/` for the bundle mount.
- uFlow caps the block at 2048 bytes (its own policy, not a system limit).
- For CUE games the `.cue` is passed; its track files stay beside it.
- Only SD content is supported. uFlow's USB namespace does not exist inside
  RetroArch.

## Loader export

`module/src/RetroArchLaunch.cpp` exports:

```cpp
RPXLoaderStatus RL_LaunchHomebrewWithArgsAndContent(
    const char *coreRelativePath,
    const char *contentBundleRelativePath,
    const char *argumentBytes,
    uint32_t argumentByteCount);
```

In order, it:

1. Validates the core path, the provider path and the argument block (exactly
   two strings, the first naming the core).
2. Confirms the provider is a WUHB and copies its metadata and icon into
   temporary storage, closing that mount again.
3. Snapshots the pending loader state it may change.
4. Prepares the core with `RL_PrepareLaunchFromSD()`.
5. Points the pending mount at `/vol/external01/` plus the provider and applies
   its metadata.
6. Calls `Mocha_LaunchHomebrewWrapperEx()` with the caller's buffer.
7. On any synchronous failure, reverts the prepared launch
   (`LOAD_RPX_TARGET_EXTRA_REVERT_PREPARE`) and restores the snapshot.

The app resolves it through `OSDynLoad` from `homebrew_rpx_loader`. If the
export is missing (an older module), uFlow shows "UPDATE THE UFLOW LOADER
MODULE FOR RETROARCH LAUNCHING" and stays open; it never falls back to
RetroArch's browser.

### Core switches inside RetroArch

When RetroArch itself switches cores it prepares the new RPX and relaunches the
wrapper with native arguments. uFlow's same-wrapper relaunch hook would
otherwise re-prepare the previous executable over that choice. The module keeps
a narrow session for launches it started, and inside
`_SYSLaunchTitleWithStdArgsInNoSplash` adopts the switch only when that session
is active, RetroArch's bundle is the mounted one, and the argument buffer names
another `.rpx` in the launched core's folder or in `retroarch/cores/`. Anything
else takes the original path.
This branch has not yet been exercised on hardware.

## Troubleshooting by stage

uFlow writes these to `sd:/wiiu/apps/uFlow/uflow.log`, and the module writes its
own lines to `sd:/wiiu/raw-launcher.log`.

| Symptom / log line | Stage | What to check |
|---|---|---|
| `retroarch-export-missing-at-startup` | App start | The loader module is older than the app. Install the current `RPXLoadingModule.wms` and restart the console fully. |
| `launcher-resolved id=ps1 module=` (empty) | Launcher | The manifest isn't in `wiiu/apps/uFlow/launchers/`, or the saved `ps1` entry is a customised one. Module discovery is cached, so restart uFlow after adding a manifest. |
| `launch-adapter-error …` | Readiness | The message names what's missing: core, bundle, or a ROM outside the SD. |
| `retroarch-plan core=… rom=…` then no launch | Arguments built | Look for `retroarch-launch-result` for the loader's status. |
| Module `refused: …` | Loader validation | Core, provider or arguments failed validation; the text says which. |
| Module `launch failed: …` | System launch | The wrapper refused the launch; uFlow restores its state and stays open. |
| RetroArch opens its menu instead of the game | Core | The arguments arrived but the core didn't use them. Check the core is the Aroma build, not a buildbot core. |
| "no compatible core found" inside RetroArch | Core info | `retroarch/cores/info/<core>.info` is missing; delete `core_info.cache` after adding it. |
| "no PlayStation BIOS" | BIOS | BIOS files belong in `retroarch/cores/system/`, not `retroarch/system/`. |

## Known limits

- Exiting RetroArch can hang the Wii U Menu. That predates this work and is a
  known unresolved symptom; the available traces do not establish that the
  fault is exclusively in RetroArch rather than the loader/Aroma interaction.
- Buildbot and Homebrew App Store RetroArch builds do not run under Aroma.
  Don't use RetroArch's built-in core updater.
- Launching from a USB library isn't supported.
