# Wii / GameCube launching (development)

uFlow writes a one-use request, launches the installed DWFA vWii channel with
CafeOS CMPT, and a small WiiFlow v5.6.2 entry patch starts the exact SD or FAT32 USB WBFS path.
It uses the existing WiiFlow game-loading code, cIOS and per-game options.
Without a request, WiiFlow opens normally. No per-game channels are installed.

Each Wii/GameCube library directory can select **WiiFlow** (default) or
**USB Loader GX**. Save the path and scan to apply. Wii WBFS supports SD and FAT32
USB; the GameCube adapter remains SD-only.
USB Loader GX uses the same installed DWFA bridge, then runs the patched
`SD:/apps/usbloader_gx/boot.dol` with a consumed-once exact-path request. It does not
require an additional installed channel or change saved content-directory settings.
USB Loader GX integration is pinned to `e25c4f3501ed957b7db73f79c51fdf00715ab2e2`;
prepare with `tools/prepare-usbloader-backend.py` and build with upstream's
`devkitpro/devkitppc:20250527` image (`make release`). Console results are below.
GameCube still uses Nintendont under either launcher. Wii U launching is unchanged.

Requirements:

- Aroma, working vWii/cIOS setup, and FIX94's vWii-compatible WiiFlow forwarder.
- d2x cIOS and the built-in HCVA Wii U Menu return channel.
- WiiFlow v5.6.2 with this entry patch at `SD:/apps/wiiflow/boot.dol`.
- Valid `.wbfs` files on SD or FAT32 USB. uFlow reads the six-character ID from
  the disc header. SD writable checks run before transition; USB is read-only in
  CafeOS and the vWii backend checks write access after mounting the FAT32 drive.
- A Wii-compatible controller; GamePad video does not provide Wii controls.

Both patched backends must include `UFLOW_WII_USB_STORAGE_V1` for USB launch.
The bridge resolves the same relative path plus disc ID on mounted FAT partitions,
since CafeOS `usb1:` need not be vWii's first partition. Missing or ambiguous
matches stop launch. The requested partition is used without changing saved
library settings. No WFS writes or formatting are performed by this adapter.
USB access still depends on the existing vWii USB port/cIOS setup. The tested
arrangement used rear FAT32 and front WFS storage; see the console results below.
Returning to WiiFlow cannot repeat the consumed launch request.

The WiiFlow bridge logs `UFLOW_USB_PROBE_V2`, the active IOS/USB port,
mounted partitions, and exact-path stat/header results in `wii-launch.log`.
If no FAT partition is mounted, it stops the USB keep-alive thread and
reinitializes USB once using the existing IOS and port. Missing files, invalid
headers, wrong disc IDs and ambiguous copies produce separate errors without
retrying or falling back to SD. Console recovery still needs hardware validation.

For older cached entries named only by ID (such as `RSPE01`), use **Add new games**.
It repairs an unidentified Wii entry at the same path without dropping offline
games or favorites. WBFS supplies ID/internal name; a tagged parent folder
supplies the friendlier title, and the ID supplies region. Artwork, descriptions,
publisher and player counts still come from separately configured media files.

## Console validation (2026-10-21)

The user confirmed Wii Sports (RSPE01) launched from FAT32 USB through both
WiiFlow and USB Loader GX. WiiFlow used the UFLOW_USB_PROBE_V2 bridge;
USB Loader GX required no additional deployment for its test. These results
confirm this title/setup, not universal USB device or title compatibility.
The successful runs have not been inspected to determine whether the USB
remount retry ran. Evidence: artifacts/usb-probe-deployment.json and
artifacts/usb-loader-gx-console-test.json.

Front-port follow-up: front-left failed for both launcher selections, and
front-right failed for WiiFlow. Saved logs show all three attempts stopped
in the shared WiiFlow bridge under IOS249/USB port 0: no FAT32 partition
mounted, including after the single retry. GX was not reached for its front
attempt; its saved passing log belongs to the earlier rear-port success.
These are current-setup results, not universal front-port compatibility claims.
Evidence: artifacts/front-usb-logs-20261021-014256/findings.json.

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
