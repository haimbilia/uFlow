# On-demand Virtual Console cartridges

Install each `<system>-vc.ufmod` in `wiiu/apps/uFlow/launchers` and configure its
matching `.cfg` to point at a decrypted Wii U VC `code/content/meta` base.
The base must be for the correct original console, not merely the same game name.
Goemon MSX Japan is title `0005000010195400`; `0005000010158c00` is Famicom.

| System | ROM inputs | Transformation / limits |
| --- | --- | --- |
| DS | `.nds`, `.srl` | Existing single-ROM ZIP backend; DS only, not DSi |
| NES | `.nes` | iNES into RPX ROM slot; no NES2/trainer, cannot exceed base slot |
| SNES | `.sfc`, `.smc` | Strip optional 512-byte copier header, patch RPX ROM slot; cannot exceed base slot |
| GBA | `.gba` | Validate header, append compressed MDF ROM, update PSB file record; at most 32 MiB |
| N64 | `.z64`, `.v64`, `.n64` | Detect/normalize byte order from magic, replace cartridge; at most 64 MiB |
| PC Engine | `.pce` | Replace HuCard package entry and sizes; no CD or SuperGrafx-specific support |
| MSX | `.rom`, `.mx1`, `.mx2` | Replace `MEGAROM.ROM` package entry, preserve BIOS/config entries; not disk images |

The injection action appears only for ROM files with a matching enabled injection
module. Installed/raw Wii U games and Wii/GameCube discs have no injection row.
Assign the matching VC launcher to the content type, directory, or game, then
press Play. If no saved injection exists, a “Not injected yet” popup asks whether
to create it. No/B returns without injecting; Yes starts preparation in the same
popup, showing the current phase and byte progress, followed by immediate launch.
Later launches use the saved output without asking, re-injecting, or hashing all
content again. Only quick record/completion/executable/structure checks run on Play.
B or the popup's Cancel button cancels preparation without launching an incomplete result.
Details > Game > Inject/Re-inject remains available for manual preparation or
rebuild; rebuilding keeps the save location for the same ROM bytes. Missing
output is recreated privately when the original ROM is available.

New profiles show only relevant settings: base, cache budget, and help. DS retains
ZIP compression and display settings. Non-DS profiles do not expose DS-only knobs.

N64 optionally uses a sidecar with the complete ROM filename plus `.ini`, e.g.
`Example.z64.ini`. It is copied to the filename expected by the base emulator.
Without a sidecar, the base configuration remains intact. Compatibility and
special hardware behavior still depend on the emulator/base and the game.

## Safety and compatibility

Completed injections live beside the original, e.g. `pce/Bonk.pce.uflow/`.
The extension is retained to distinguish same-named files from different systems.
The package contains `game.cfg` (identity and active-generation record), an
ownership marker, and `game-<hash>/code`, `content`, and `meta`. Immutable generations
allow a new injection to commit without overwriting the previous working version.
Do not rename or edit the files inside the package manually.

The normal ROM library discovers both ROMs and these packages, merges them into
one entry, and keeps the original identity for artwork, favorites, and launchers.
Deleting the original ROM after testing no longer removes the game on rescan.
The source directory/type must still be enabled and match the ROM extension.
Re-injection requires the original ROM again; ordinary saved Play does not.

Legacy records under `wiiu/apps/uFlow/injections/<system>` are still discovered,
even without the original ROM. First Play copies/verifies that old injection into
its sibling package, with progress and cancellation. This is a one-time migration,
not re-injection, and needs enough free SD space for another copy. The old cache is
retained as a safety copy; later launches use the package without copying again.
No original ROMs, older generations, legacy caches, or saves are automatically deleted.

Only private `wiiu/apps/uFlow/vc-cache/<system>/...` directories are transformed.
Base files, source ROMs, installed titles, tickets and NAND are never patched.
Saves use `wiiu/vc-saves/<system>/<ROM SHA256>/<base title ID>`; changing bases with
different title IDs can require save migration. New saved references live in each
package's `game.cfg`. Existing DS record/cache formats remain readable.
Preparation/cache reuse and migration verify file manifests. Normal saved launches
do not detect arbitrary content corruption; re-inject from a good original/base
if content becomes damaged. Cancellation removes only
the current uncommitted work directory. Cache limits do not automatically delete
older injections or saves.

Preparation hashes the base while copying and verifies unchanged output files
during the final manifest pass. Files that will be transformed are also read back
before modification; source changes and bad copies are still rejected. This
removes two redundant whole-base reads. The supplied PCE base test reads
596,453,058 bytes through the copy/hash paths versus the previous base-only lower
bound of 988,845,210 bytes (about 40% less). This measures I/O, not console elapsed
time. Large bases still take time on SD; popup percentages describe the current
step, not the entire injection. SHA-256/cache keys and save namespaces are unchanged.

Host tests validate container transformations and preservation, not hardware
emulation compatibility. First test each baseline game, saving/reloading, exiting
to uFlow/Wii U Menu, then a separate compatible ROM before adding a large library.

### Validated base formats (2026-09-13)

| System | Supplied base | Title ID |
| --- | --- | --- |
| NES | EarthBound Beginnings (USA) | `0005000010133100` |
| SNES | Kirby's Dream Land 3 (USA) | `0005000010115300` |
| GBA | The Legend of Zelda: The Minish Cap (USA) | `000500001015e400` |
| N64 | Donkey Kong 64 (USA) | `0005000010199200` |
| PC Engine | Bomberman '94 (USA) | `0005000010166d00` |
| MSX | Goemon (Japan) | `0005000010195400` |

Real-base host tests cover cartridge readback, unchanged source bases and unrelated
container entries, cache reuse/rebuild, save isolation, cancellation, and wrong
system rejection. Test cartridges are extracted from these same local bases;
passing those tests does not establish compatibility with other cartridges.

`tools/deploy-vc-systems.py` verifies uploaded assets by complete FTP readback.
Use `--systems msx` to upload just that system, then `--activate-only` to install
the tested app/modules and append library rows after all six asset uploads are
recorded. Existing app/configs are backed up; different existing base files are
not overwritten. Restart uFlow and run Scan Now after activation.

Format research: [Retroinject_C](https://github.com/Morilli/Retroinject_C),
[inject_gba_C](https://github.com/Morilli/inject_gba_C),
[Wii U VC Extractor](https://github.com/AwakeEchidna/wiiu-vc-extractor), and
[UWUVCI](https://github.com/stuff-by-3-random-dudes/UWUVCI-AIO-WPF).
Implementations here use bounded parsing and private staged files rather than
running those desktop tools on the console. No base game data is distributed.
