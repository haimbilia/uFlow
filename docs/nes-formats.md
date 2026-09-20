# NES input compatibility

uFlow normalizes inputs **in memory** when creating a private VC injection.
The original ROM/archive is never rewritten. Saved injections still launch
without rehashing the original, and remain discoverable if it is removed.

Supported input paths:

- `.nes`: iNES, plus compatible NES 2.0 layouts converted to legacy iNES.
- Headerless `.nes`: exact SHA-1 **and length** must match the embedded board
  database. No guessing from the name, extension, or file size alone.
- `.unf` / `.unif`: bounded PRG/CHR chunk parsing and exact database identification.
- `.zip`: exactly one `.nes` / `.unf` / `.unif` member, stored or deflated.
  ZIP payload CRC, sizes, names and ranges are validated. Decompression is
  memory-only, capped at 32 MiB. Encrypted, multi-disk/ZIP64, ambiguous multi-ROM
  archives and unsafe paths are rejected. Other systems' ZIPs are not NES games.

Legacy DiskDude mapper corruption and reserved-byte signatures are removed.
iNES PRG/CHR counts bound the ROM; extra annotation/overdump tails are excluded.
Truncated declared PRG/CHR data is rejected, not padded with invented bytes.

NES 2.0 conversion rejects unsupported extended mappers/submappers, additional
RAM/ROM and Dendy timing instead of discarding required cartridge behavior.
MMC1 submapper5 with fixed32K PRG is expressible using legacy MMC1. Trainers,
VS/PlayChoice hardware and unmatched headerless/UNIF boards need a suitable
emulator; they are not claimed to work through this VC conversion.

Format support is **not a guarantee of Nintendo VC emulator compatibility**.
The selected base must have enough reserved ROM space and support the game's
mapper/hardware. Capacity is checked before copying the base. Existing hacks,
translations and files labeled `[b1]` may have game-data defects that repairing
a header cannot fix. IPS files are patches, not games; unrelated patch filenames
are not automatically applied to a guessed ROM. No FDS/7z/RAR support added here.

## Database provenance

NewRisingSun's NES 2.0 XML database, dated 2021-12-25, retrieved from the
[puNES source tree](https://github.com/punesemu/puNES/blob/master/misc/nes20db.xml).
Its [author's description](https://forums.nesdev.org/viewtopic.php?t=19940)
explains the headerless-ROM lookup use case. The XML is retained at
`tooling/nes-db/nes20db.xml`, SHA-256:
`a60757b92f2f3df9447aa50e004aabe9f4cb0845bb037afca118ff89b5913ae3`.

`tools/generate-nes-db.py` reproducibly generates 5,902 legacy-representable
SHA-1/header records in `app/source/nes_database.hpp`. No ROM bytes or titles
are embedded. Unknown/ambiguous or non-representable records are excluded.
The implementation is original uFlow GPLv3 code; database attribution is kept
in the generated file. NES header semantics were cross-checked with the
[Mesen loader](https://github.com/SourMesen/Mesen2/blob/master/Core/NES/Loaders/iNesLoader.cpp)
and [NESdev NES 2.0 specification](https://www.nesdev.org/wiki/NES_2.0).

## Verification

`tests/nes_formats_test.cpp`: synthetic normalization, ZIP integrity/security,
old launcher manifest compatibility, and read-only audit/injection roundtrip
of the supplied NES folder. On 2026-09-14: 34 loose ROMs plus one archived ROM
all normalize and fit the local EarthBound Beginnings base; two IPS patches
are excluded. These are host-side format/injection results, not console playtests.

Library tests cover old `.nes` filters upgrading, UNIF discovery, non-ROM archive
exclusion, and published ZIP packages surviving deletion of their source ZIP.
