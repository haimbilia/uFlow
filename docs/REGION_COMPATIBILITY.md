# Region compatibility audit — 2026.10.20.2

Scope: library, media/UI, installed and raw Wii U launching, VC injection,
Wii/GameCube bridges, native installer, bundled region-free dependency, and
signed app updater. Source/payload review and host tests are not EUR/JPN
hardware certification.

## Fixed

- Metadata prefers `cafe.language` instead of English. All 12 Cafe metadata
  languages have deterministic nonempty fallbacks.
- Cache version 10 records the language. Changes refresh available Wii U
  metadata while preserving offline entries, artwork, launch identity and
  saves. Versions 7–9 remain readable. Offline titles retain their saved names
  until their source is scanned again.
- RPXLoadingModule fills long name, short name and publisher in every HOME-menu
  language instead of clearing every language and filling English only.
- Release sequence: 2026102002.

## Reviewed

| Area | Finding |
| --- | --- |
| Installed discovery/launch | MCP enumerates actual title IDs. Base/update/DLC grouping retains the regional title ID; different regional releases are not combined. |
| Raw launch/saves | Reads selected app/cos metadata and actual title ID. No region conversion or save migration. The `000500101000400A` fallback is an OS title, not a USA Menu title. |
| IOS payload | Menu matching includes JPN `10040000`, USA `10040100`, EUR `10040200`. Existing One Piece and Mass Effect exceptions include all three regional title IDs. |
| VC injection | Retains the base's region/resources. Six bundled bases are USA; MSX is JPN. Cross-region use requires the bundled region-free/raw-loader stack. Relabeling XML would not supply missing language resources. Other supported bases remain selectable. |
| Region Free | Reads real console region/country/language; recognizes all three Menu/Health & Safety variants; raw detection checks mounted metadata against the actual title ID. User overrides remain authoritative. Missing/ambiguous metadata may open plugin configuration. |
| Wii/GameCube | CMPT and the DWFA forwarder are not USA-title substitutions. Bundled WiiFlow config does not force language/video mode. Existing WiiFlow/Nintendont region handling is retained. GameCube SRAM language numbering has no distinct Japanese value; Cafe language numbers must not be substituted. |
| N64 VC | Profile selection matches cartridge ID, region and revision, otherwise uses the existing generic fallback. This does not guarantee every ROM works in Nintendo's emulator. |
| Text | Existing shared-system-font fallback supports Japanese/CJK. UI translation is separate from console compatibility. |
| Installer | No console region changes or USA system-menu installation. Existing vWii prerequisite plan remains restricted. Region Free ships with Wii U/VC selections; settings, ROMs and saves are preserved. |
| Updates | Same HTTPS endpoint, signature verification and app destination across regions. No regional network/certificate selection. |

## Validation and deployment

`tools/test-region-compatibility.sh` exercises 12 locales, USA/EUR/JPN raw
title IDs, language cache migration, missing-language fallback, offline
preservation and HOME-menu metadata. Library/installed-title, VC injection,
native installer and signed-update checks accompany the release.

Rebuild app and RPX module, sync native payload, rebuild Setup, and run
Core/Core+WiiU fresh/reinstall checks.

**OTA updates the app only.** Run the new native installer and restart to
receive the module's HOME-menu metadata fix. Release notes must say so.

EUR/JPN hardware testing remains required for fresh installation, installed/raw
games, cross-region VC bases, HOME-menu names, Wii/GameCube launch and update
handoff. No universal game/ROM/firmware or region-changed-console compatibility
claim is made.
