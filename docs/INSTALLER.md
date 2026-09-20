# Windows SD installer

Double-click `dist/uFlow-Setup/Install-uFlow.cmd`. A checklist appears before
the target-card prompt or any write. The core app is always included; all other
choices are optional, including individual content types, NooDS, opt-in startup
support, and fresh-console vWii tools. Select all / Clear all are available.

Selecting a VC system includes the raw Wii U backend and only that system's
base import. Wii/GameCube share the patched WiiFlow/GX bridge; GameCube adds
Nintendont. NooDS alone creates a DS library assigned to NooDS without a VC base.
New library/media/settings files reflect the selection. Unchecked pre-existing
files and existing user configurations are retained, not removed or rewritten.

The setup checks a writable, non-system FAT32 removable/USB volume, Aroma
prerequisites, path containment, package SHA-256, base title IDs, base structure,
destination conflicts and free space. The user confirms the exact drive before
installation, and its identity is rechecked. All changes stage and verify before
publishing. Original replaced files stay in `.uflow-install/<run>/backup`, outside
Aroma's module discovery paths. Failures stop with a report; no automatic rollback.

The installer does not format, change NAND, install Aroma, change minute/bootloader
settings, download games/BIOS/keys/IOS bases, or convert the media backlog.
Base imports are read-only on the PC. Selected files copy to
`wiiu/vc-bases/<system>/default`; no original ROMs or saves are moved.

For a fresh Wii U, see the bundled `FIRST-RUN.txt`: back up that console, install
Homebrew Channel and vWii cIOS, and install the DWFA WiiFlow forwarder. The SD
installer can stage the tools, not certify that those console steps succeeded.
Compatibility/performance of individual ROMs still needs hardware testing.

## Build and test

`tools/build-installer.ps1` packages the current local binaries, pins/checks the
first-time console tools and Nintendont, and checks the WiiFlow/GX adapter markers.
It refuses an existing output directory instead of deleting/restoring old builds.
It never accesses a physical card. Bundles exclude personal VC bases; the selected
bases are imported from `D:\WiiU` or the user's folder at install time.

Run `tools/test-installer.ps1 -PackageRoot <bundle>` under Windows PowerShell 5.1.
`-RealBases` additionally copies/verifies all seven local bases in a disposable
PC folder. Tests cover preservation, backups, rerun idempotence, traversal/ADS/
junction rejection, base/package conflicts, failed staging and optional selections.
`tools/test-installer-checklist.ps1` exercises select/clear and renders the actual
WinForms checklist off-screen, without launching an installer or touching a card.

These are personal development bundles, not signed public releases. Before
redistribution, supply complete corresponding source and license material for
the bundled open-source components and patches. See each bundle's `SOURCES.txt`.
