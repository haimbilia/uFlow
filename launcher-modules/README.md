# uFlow launcher modules, interface v1

Install a single `.ufmod` in `sd:/wiiu/apps/uFlow/launchers/`, then restart
uFlow. It appears in Settings > Launchers and compatible content-type launcher
choices. Disable it in settings; remove the `.ufmod` to uninstall it. User
preferences, emulator executable, BIOS, ROMs and saves are separate.

## First module: NooDS 1.0.0

`noods.ufmod` is the complete module. It requires the accompanying handoff-enabled
NooDS build installed as `sd:/wiiu/apps/noods/noods.wuhb`. Its emulator-side
capability descriptor is `sd:/wiiu/apps/noods/uflow-api.txt`, containing
`UFLOW-ROM-1`. The descriptor is shipped with the patched emulator, NOT a way
to make a stock emulator support handoff. Do not retain it if reverting to stock.

Existing DS content types assigned to `noods` need no changes. Select a scanned
`.nds` game and launch. The module sends its path, and NooDS calls its own
`ConsoleUI::setPath` instead of opening the browser. Settings > Open Emulator
still opens the browser and explicitly clears any pending request first.

GamePad HOME exits NooDS gameplay/settings/browser and launches uFlow. The audio
callback is stopped and the emulation/save threads are joined before departure;
in-game saves and emulator settings are flushed. This does not create a save
state. L or R remains NooDS's pause/settings menu; Plus/Minus are DS Start/Select.
If uFlow is missing or its launch API fails, exit falls back to the Wii U Menu.

The patches preserve NooDS's save/config directory. Core creation errors still
use NooDS's existing error handling. Only SD ROMs are supported by this module.
Uppercase `.NDS` and names with spaces work. No full-ROM copy or per-frame
logging is used. Console performance/compatibility still needs hardware testing.

## Format

UTF-8 text, maximum 8192 bytes. First line exactly `UFLOW-MODULE-1`. Remaining
lines are unique `key=value` pairs; blank lines and `#` comments are allowed.
See `noods.ufmod` for all required fields. Duplicate/unknown keys, unsupported
versions/protocols and unsafe paths are rejected. Conflicting IDs are blocked.

Paths are relative to the SD, without `..`, absolute device paths or shell syntax.
The executable must be RPX/WUHB under `wiiu/apps/`. The request and dependency
must be `uflow-launch.req` and `uflow-api.txt` alongside that executable. Module
identity, system, formats, executable and handoff are defined in the module;
enabled state and display-name changes remain in user configuration.

Interface v1 implements a declarative `rom-request-v1` protocol, not arbitrary
native code, Lua, dynamic RPLs or a generic command line. Another emulator that
implements this protocol can be added with just another `.ufmod`; no frontend
code changes are needed. An emulator without such an interface still needs an
emulator-side adapter. New kinds of protocols require a versioned host extension.
Do not describe v1 as an unrestricted binary plugin loader.

## Emulator integration / SDK

Use `sdk/rom_request.hpp` (C++11). After mounting the SD and initializing the
emulator UI, call `UFlowRom::Consume(requestPath, sdRoot, romPath)`; if true,
pass `romPath` to the emulator's normal ROM-loading function. Otherwise show
its normal browser. Consume removes the request before any game code runs.

Wire format: `UFLOW-ROM-1\n<Unix timestamp>\n<SD-relative ROM path>\n`.
Reject requests over 120 seconds old, future timestamps, malformed data,
unreadable ROMs and unsafe paths. Frontend writes a temporary file then renames
it; launch failures clear the request. The consumer never edits the ROM.
The capability descriptor checks installed protocol version, not binary integrity.

NooDS fork is at `../emulators/NooDS`, upstream commit
`4cb1439c330248b26cdf5f8dbcfa4e5ac53dcda0`, GPL-3.0-or-later, author Hydr8gon.
Retain upstream licensing/attribution when redistributing the patched emulator.
Changes: shared request SDK include, startup handoff, case-insensitive setPath,
and a newlib include-order fix for the current Wii U toolchain.

Build from repository root with existing Docker image:

```powershell
docker run --rm -v "${PWD}/projects/uFlow:/project" -w /project/emulators/NooDS uflow-builder:dev make -f Makefile.wiiu -j4
docker run --rm -v "${PWD}/projects/uFlow/app:/project" -v "${PWD}/projects/uFlow/launcher-modules:/launcher-modules:ro" uflow-builder:dev make -C /project -j4
```

Tests: `tests/launcher_module_test.cpp` runs in a disposable directory with a
manifest argument, using host GCC and `-Iapp/source`. It covers discovery,
parsing, dependency checks, valid/invalid/expired/replayed requests, spaces,
uppercase extensions, missing ROMs, duplicate IDs and size limits.
