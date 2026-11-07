# Controllers inside Settings

The controller icon beside About and Support opens the controller panel in the existing
Settings window. It lists connected Wii controllers and saved Bloopair records,
with player slots and battery levels. Offline native Wii Remote/Pro pairings
are not exposed by the API used here. The Wii U GamePad remains separate.

- Select a card with Left/Right; A opens its raw-input test. Hold a button or
  move a stick to inspect its values. B returns. Reports are sampled at 4 Hz.
- Pair new controller starts the Wii U Bluetooth sync procedure. Put the
  controller in its pairing mode; Cancel or leaving the panel stops the search. Sync completion uses the system STARTED/FINISHED
  callbacks, not an instantaneous busy flag; requests time out after 60 seconds.
- Disconnect selected retains the controller's saved pairing.
- Unpair all requires explicit confirmation, defaults to Cancel, and clears
  Wii Remote/Pro/Bloopair pairings through the system API. It does not remove
  the Wii U GamePad pairing. Individual saved-pair removal is not supported.

The 8BitDo Mod Kit for Original N64 Controller should be tested in S (Switch)
mode. Power it on and hold its pairing button while uFlow searches. Compatibility
with this particular kit is not established until hardware testing succeeds.
See the [official manual](https://download.8bitdo.com/Manual/Mod-Kit/Mod-kit-for-N64-controller%2BJoystick.pdf).

## Installation and runtime

Official Bloopair v1.1.0 is included in the app and native installer's Core
selection. Fresh installs place `30_bloopair.rpx` in Aroma's setup modules.
Existing/custom modules and all pairing/configuration files are preserved.
For an existing app installation with no module, choose Install controller
support in the panel, then restart Wii U. This never hot-loads the module.

The worker owns Bloopair IPC and performs discovery, pairing commands and SD
reads outside the UI thread. It runs only while the panel or an outstanding
operation is active. Saved records are scanned at most once per two seconds;
only their public six-byte headers are read, never pairing keys. Disconnect
rechecks the Bluetooth address to avoid acting on a reassigned player slot.
Optional padscore exports are resolved at runtime to retain the pinned builder.

Upstream: https://github.com/GaryOderNichts/Bloopair/tree/v1.1.0
License: GPL-2.0-or-later; included alongside the vendored library and module.
Module SHA256: `5d777a1d487e7911bd9a9424f7efbb417ceea385eec060239f7d336ad67ac95b`.

The icon is a scalable vector based on the owner's supplied silhouette, rendered
to a transparent PNG and tinted by the current skin. No per-frame rasterization.

Hardware pairing, disconnect, unpair, restart and reconnection must be tested on
console. Desktop previews use fixtures and do not prove Bluetooth functionality.
