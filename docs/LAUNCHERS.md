# Launchers

## Single-file modules (new)

Place `.ufmod` files in `sd:/wiiu/apps/uFlow/launchers/` and restart uFlow.
The NooDS module now supports direct DS ROM launch with the handoff-enabled
NooDS build. Its settings status reads **DIRECT ROM LAUNCH / v1.0.0**.
The emulator and its configuration/saves stay separate from the module.
Existing `noods` content-type assignments automatically use the module.
Other manually added emulator entries still have the browser-only behavior
described below. See `launcher-modules/README.md` in the project for the SDK.

Open **Settings > Launchers**. This is inside the existing settings window.
The list scrolls; the counter shows your position. D-pad/stick navigates, A
edits, B returns. Select a choice with A before changing it with left/right.
Touch supports the sidebar, rows, executable picker, keyboard and footer.

## Included adapters and presets

- Wii U / Raw: existing installed-title and raw-directory backend.
- WiiFlow and USB Loader GX: existing Wii / GameCube forwarding adapters.
  Their existing prerequisites still apply; the registry doesn't install cIOS.
- Nintendont via WiiFlow: named GameCube-only preset using the existing forwarder
  and `sd:/apps/nintendont/boot.dol`, not a new direct GamePad-enabled bridge.
- NooDS: emulator entry with a configurable SD executable. It is not bundled
  or downloaded by adding this entry. Status indicates whether its file exists.
- Prepared VC/injection presets: NES, SNES, Nintendo 64, Game Boy Advance,
  Nintendo DS, TurboGrafx-16, MSX, Wii and GameCube. Wii/GC entries refer to
  prepared Wii U injection/forwarder titles, not native VC emulators.

These presets follow the systems listed by UWUVCI:
https://github.com/stuff-by-3-random-dudes/UWUVCI-AIO-WPF
Game compatibility still depends on the chosen injection base and settings.

## Add an emulator

1. Install its Wii U `.rpx` or `.wuhb` on the SD using the emulator's instructions.
2. Select **Add Launcher / Emulator**. Set name, method, system and comma-separated
   ROM extensions (for example `.nds`). Select **Executable**, browse and select
   the RPX/WUHB. Save the launcher.
3. Under **Library > Manage Content Types**, choose/create your type, select
   **Files** as its scanner, set its extensions, and select your launcher. Save.
4. Add a content directory for that type and scan.

**Generic entries without a launch module open the emulator's own ROM browser.** Select the
ROM inside that emulator. uFlow does not currently pass the chosen ROM to arbitrary
emulators. Upstream NooDS's Wii U frontend opens `ConsoleUI::fileBrowser()` and
doesn't implement an argv-based ROM launch. A dedicated adapter is needed for
one-click ROM handoff. The supplied NooDS module and patched emulator now provide
that adapter. Merely adding a generic entry does not implement an adapter.

The executable picker is SD-only. Windows EXEs (including UWUVCI AIO), Wii DOLs,
and arbitrary shell commands cannot be used as Wii U emulator executables.
The system field is a descriptive label; it does not add emulation capability.

## Virtual Console / prepared injections

uFlow does **not** create injections on the console, download bases/keys, or
replace a ROM inside a shared VC base. Prepare a compatible injection on the PC
first using your own files. UWUVCI is a PC preparation tool, not a runtime module.

There are two ways to launch a prepared injection:

- Scan its raw Wii U directory (code/content/meta), or enable installed-title
  discovery after installing it. Launch it normally using the Wii U backend.
  A custom type with a Wii U scanner can use a VC preset for raw injection folders.
- To keep the original ROM in a system-specific **Files** library, assign the
  matching VC preset to that content type. Then open **Launchers > that preset >
  Map ROM to Injection**, select the ROM and select its corresponding prepared
  Wii U title from the scanned library. Both must already be in the library.
  The picker lists Wii U titles, not verified injections: choose the matching
  injection you prepared, not an unrelated game.

Mapping writes only a tiny `<ROM filename>.uflow-title` sidecar beside the ROM,
containing the 16-digit Wii U game title ID. Selecting the ROM resolves that ID
to the existing scanned installed/raw title and uses the normal Wii U launcher.
No game payload, NAND file, ticket or base title is modified. Missing/malformed
mappings and unavailable targets produce a message without attempting launch.

Example: `Example.nds.uflow-title` containing the actual title ID of your own
Example injection. Repeat the mapping action to change the association.

## Persistence and defaults

`sd:/wiiu/apps/uFlow/launchers.cfg` stores tab-separated fields:
`id, name, method (0..4), system, extensions, SD-relative executable, enabled`.
The eighth field of `libraries.cfg` stores the assigned launcher ID.
Existing seven-field library configurations remain compatible. Empty launcher
assignment preserves the existing per-directory WiiFlow/USB Loader GX choice.
Explicit assignments override that directory default and apply without a rescan.

Renaming a launcher preserves its ID. Referenced entries cannot be removed
until their content types are reassigned. Removing an entry never removes the
emulator, ROMs or saves. Built-in core adapters can be disabled, not removed.
Save operations use a temporary configuration followed by rename.

Hardware testing is still required for emulator handoff and injection mapping.
The presence of an executable is not proof of its compatibility or performance.
