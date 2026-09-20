# uFlow Setup - native Wii U installer

This is an Aroma **WUHB app**, not the earlier Windows installer.
Copy `uFlow-Setup.wuhb` to `SD:/wiiu/apps/uFlow-Setup.wuhb`, boot Aroma,
and open **uFlow Setup** from Wii U Menu. No Windows program is run.

The installer menu icon uses the project-root `Installer Icon.png`, independently
of uFlow's app icon. WUHB packaging scales it to 128x128 and tracks it as a build
dependency; the original artwork is not modified.

The private test build contains the homebrew payload and the user's seven
decrypted VC bases in WUHB content. It is roughly 425 MiB. **Do not redistribute
this private build or its content folder.** A public version without game bases
and with an SD base picker is not implemented yet.

Use GamePad D-pad to select an item, A or touch to toggle, X for all, Y for none.
Plus starts read-only preflight; a separate A confirmation starts the selected
SD and console installation. Selecting Wii/GameCube automatically includes vWii
prerequisites. Their exact console titles/slots are listed before confirmation.
Only selected components and shared dependencies are installed. Core-only and
NooDS-only selections do not install any VC bases. Existing user settings,
games/saves and unchecked components remain untouched.

The installer hash-checks selected bundled files, detects existing base conflicts,
checks space, stages/copies/verifies, rechecks destinations, backs up originals,
publishes and verifies. B cancels during preflight/staging, not during publication.
Never remove the card or interrupt power while publishing. Reports and retained
backups/staging are under `SD:/.uflow-setup/<run>/`; no automatic rollback or
deletion occurs. If publishing fails, complete setup before booting games.

After success, fully reboot into Aroma: replaced modules only take effect after
reboot. Startup support is opt-in and remains off on a fresh installation.

## Integrated vWii setup (new; hardware testing required)

One WUHB now performs the console steps directly; it does not launch separate
installers. It prepares the bundled OHBC and official vWii DWFA forwarder plus
d2x-v11-beta3-vWii from THIS console's verified base IOS:
38 -> 248, 56 -> 249, 57 -> 250, 58 -> 251. No Nintendo IOS is bundled/downloaded.
Unexpected/missing base revisions, altered contents or patch mismatches stop
preflight without console writes. This is setup for a healthy vWii, not repair.

The hash-pinned FIX94 DWFA package has legacy ticket common-key index 0x49.
For this forwarder only, setup maps it to standard Wii key 0 for decryption,
matching Compat Installer. Other unknown indices remain rejected; the original
ticket is preserved and decrypted content must still match every TMD hash.

An explicit confirmation precedes console writes. Only these six title IDs,
their tickets/data directories and required UID allocations are in the write
allowlist. The original base IOS/shared content map are read-only; cIOS contents
are private copies. Region, System Menu, IOS80 and game saves are not rewritten.
All titles are prepared before confirmation and staged/readback-verified before
publication. HOME is disabled during the console transaction. Original replaced
content/tickets/uid.sys are retained under sibling `uflow-old` folders; staging
uses `uflow-new` folders. File basenames stay unchanged (ISFS cannot rename a
file's basename), and all components fit the 12-byte limit. No automatic restore.
An interrupted stage stops preflight for diagnosis, with one narrow exception:
the old build's failure to rename `uid.sys` to `uid.ufo`. Preflight can recognize
and byte-verify its complete `.ufn` staging and expected UID table while the
original UID table remains intact. It then offers explicit confirmation to
continue, copying to the corrected staging paths and retaining all legacy files.
Mismatches, partial staging, moved originals or unexpected UID state stop setup.
Completion requires installed-content and UID readback success.

This new path is host-tested and cross-built, not yet validated on a real console.
Test a fresh installation and launch/save/exit for Wii and GameCube before release.
ROM compatibility is not universal. The private bundled VC bases remain private;
a public ROM-only release still needs a legal base-discovery/emulator strategy.

Sources: https://github.com/Xpl0itU/vwii-compat-installer (narrow in-memory patcher,
GPL-2.0-or-later), libmocha (LGPL-3.0), official FIX94 vWii forwarder archived at
https://storage.googleapis.com/google-code-archive-downloads/v2/code.google.com/wiiflow/Default-FIX94v14b-vWii-Forwarder-DWFA.wad
The latter's published SHA-1 is a704f15be2270a0d2983e673021fa4a16ffde4d4;
all bundled console assets have pinned SHA-256 checks in source/vwii.cpp.

## Build and verification

`tools/prepare-native-installer.ps1 -IncludeLocalBases` imports the verified local
payload and bases into `content/`, generates SHA-256/selection metadata, and embeds
no private data in tracked source. It refuses to replace an existing content tree.
Build with `uflow-builder:video`, running `make -C /project/native-installer -j4`.
The Makefile also builds the imported libmocha source. Run
`tools/prepare-native-vwii.ps1` after a new payload import to add console assets.
SDL exclusively owns ProcUI, matching the main app's proven exit lifecycle.

`tests/native_installer_test.cpp` exercises the same portable installer core,
including SHA vectors, selection, no-base NooDS, backups, preservation, idempotence,
unsafe paths/links, conflicting bases, cancellation and concurrent changes.
Passing a content directory additionally tests the full real payload in disposable
host storage. The UI can be rendered with `UFLOW_SETUP_PREVIEW` and host SDL/TTF.
Host tests/builds are not proof that this new app has run successfully on hardware.
`tests/vwii_plan_test.cpp` tests title allowlists, hashes, UID safety, low-space
failure, staging/readback, repeat installs and injected write failures.
`tests/vwii_cios_test.cpp` runs the actual patcher against the bundled ciosmaps and
modules using synthetic base contents, including strict failure/bounds cases.
