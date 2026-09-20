# Wii / GameCube launching (development)

uFlow writes a one-use request, launches the installed DWFA vWii channel with
CafeOS CMPT, and a small WiiFlow v5.6.2 entry patch starts the exact SD WBFS path.
It uses the existing WiiFlow game-loading code, cIOS and per-game options.
Without a request, WiiFlow opens normally. No per-game channels are installed.

Each Wii/GameCube library directory can select **WiiFlow** (default) or
**USB Loader GX**. Save the path and scan to apply. Both currently support SD only.
USB Loader GX uses the same installed DWFA bridge, then runs the patched
`SD:/apps/usbloader_gx/boot.dol` with a consumed-once exact-path request. It does not
require an additional installed channel or change saved content-directory settings.
USB Loader GX integration is pinned to `e25c4f3501ed957b7db73f79c51fdf00715ab2e2`;
prepare with `tools/prepare-usbloader-backend.py` and build with upstream's
`devkitpro/devkitppc:20250527` image (`make release`). New route needs console tests.
GameCube still uses Nintendont under either launcher. Wii U launching is unchanged.

Requirements:

- Aroma, working vWii/cIOS setup, and FIX94's vWii-compatible WiiFlow forwarder.
- d2x cIOS and the built-in HCVA Wii U Menu return channel.
- WiiFlow v5.6.2 with this entry patch at `SD:/apps/wiiflow/boot.dol`.
- Writable `.wbfs` files on SD, with a valid six-character game ID matching the
  disc header. uFlow reports read-only files before switching modes.
- A Wii-compatible controller; GamePad video does not provide Wii controls.

This first adapter supports SD only, including custom content directories.
USB Wii/GameCube games and automatic return to uFlow are not part
of this adapter. Returning to WiiFlow cannot repeat the consumed launch request.

## GameCube

Install the official Nintendont `loader.dol` as `SD:/apps/nintendont/boot.dol`,
with its `meta.xml` and `icon.png`. This integration pins upstream commit
`0f69235b99099ef2851a5f6b7b0e349c572237e8`; the old version text in upstream
meta.xml does not identify the current loader binary.

Put a plain ISO at `SD:/games/Game Name/game.iso`. For two discs, add `disc2.iso`
beside it; uFlow starts disc 1 and leaves in-game disc swapping to Nintendont.
Custom SD directories work via Library settings with content type GameCube.
RVZ, CISO, GCM and extracted FST are not supported by this first uFlow adapter.
Convert RVZ to plain ISO in Dolphin rather than renaming the extension.

uFlow reads ID/name from the ISO header, validates DOL/FST bounds, and publishes
`UFLOW-GC-1` in the same consumed-once request file. The backend requires marker
`UFLOW_GC_PATH_BACKEND_V1`, selects Nintendont without changing saved loader choice,
passes exact path through upstream WiiFlow's NIN_CFG arguments, and reloads IOS58.
Nintendont upgrades WiiFlow's version-9 config to version 10 on receipt. No new
cIOS install is needed specifically for GameCube. Wii U memory-card emulation is
enabled by upstream WiiFlow; existing per-game options are retained.

Use a Nintendont-supported controller (e.g. Wii U Pro or Wii Remote + Classic
Controller). GamePad video through this forwarder does not supply GamePad controls.
Wind Waker launch/gameplay has been confirmed with a Wii U Pro Controller.
Header validation is not a full ISO integrity check.

For uFlow-launched GameCube games, HOME exits through Nintendont's return stub
to the built-in Wii U Menu return channel. This uses a mode-switch reboot, not
an instant return or automatic reopening of uFlow. This new exit path still needs
hardware confirmation. Only the in-memory WiiFlow stub destination is changed;
standalone WiiFlow and Nintendont's installed files remain unchanged. The build
pins the original stub hash, and unknown runtime instructions stop launch.

## Returning to Wii U Menu

For a Wii game launched from uFlow, press Wii Remote HOME and choose **Wii Menu**.
The backend redirects that System Menu request to the built-in Wii U Menu return
channel (`00010002-48435641`). The HOME button label itself is unchanged.
This is not automatic return to uFlow.

The full 64-bit return target is applied to the running d2x cIOS after the game's
IOS reload. It does not install channels, write NAND, or change standalone
WiiFlow's saved return setting. Failure stops launch and records `return_patch`
in `wii-launch.log`. Hardware return behavior still needs confirmation.

## Wii Remote in uFlow

Use a synced remote held upright: D-pad navigates (hold to accelerate through
covers), A selects/launches, B goes back, + opens settings, - opens title details,
and **1 / 2 switch tabs left / right**. HOME remains handled by the system.
B does not exit uFlow and - does not scan. GamePad controls/touch remain available.
Remote buttons work without a GamePad input sample; up to four remote channels
are read. Nunchuk stick navigation includes a deadzone and accelerated cover
scrolling; C maps to Y (details), Z to X (favorites). MotionPlus + Nunchuk is also
supported. Extension buttons follow the corresponding X/Y actions in settings.
On resuming from HOME, release buttons and center the stick before navigating.
IR pointer and Classic/Pro Controller extension controls are not implemented.

## Build

Use the exact upstream checkout at
`66d12730f6d77fda4f659f2da2828303e074944d` (v5.6.2). Run
`tools/prepare-wiiflow-backend.py <checkout>` to apply the narrow entry patch.
The script refuses unrelated edits and other revisions. Build with `wii/Dockerfile`
and run `make -j4` in that checkout. The SDK versions match upstream CI.
Keep upstream license/source with any distributed backend binary; this repository
contains the small integration source, not the full upstream code.

Build the Wii U app using its existing builder. Run `tools/test-wii-launch.sh`
in the host-test image with this project mounted at `/project`.

## Diagnostics / removal

- uFlow: `SD:/wiiu/apps/uFlow/uflow.log`.
- One-shot backend result: `SD:/wiiu/apps/uFlow/wii-launch.log`.
- Pending request: `SD:/wiiu/apps/uFlow/wii-launch.req`; consumed before launch.
- Starting uFlow again clears an abandoned request from an interrupted transition.
- Full WiiFlow SD debug logging is unnecessary for ordinary use.

Restore the original WiiFlow `boot.dol` and prior uFlow WUHB to undo the adapter.
Delete only the pending `.req`/`.tmp` files if a transition was interrupted.
Games, saves, cIOS and normal WiiFlow settings are not replaced by this adapter.

References: [WiiFlow source](https://github.com/Fledge68/WiiFlow_Lite/tree/v5.6.2),
[Boot2vWii](https://github.com/WiiDatabase/Boot2vWii),
[forwarder installation](https://sites.google.com/view/wiiflow-wiki/forwarder-channels).
