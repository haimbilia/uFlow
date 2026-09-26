# Wii VC direct USB launch investigation

The requested outcome is launching the existing `code/content/meta` Wii VC
title with its HAI wrapper, without extracting or reinjecting the embedded game.
Play never converts it. The HAI USB bridge below is implemented; the historical
notes record DKCR and Xenoblade booting on hardware. GamePad remapping and return
still need confirmation. The 2026-09-26 review blocks the broken WBFS conversion
option, including old cache entries, and adds complete-NFS-set validation and
handoff lifecycle fixes. These new changes have not been tested on a console.

## Implemented bridge (2026-09-21, awaiting hardware test)

- PPC (`module/src/RawHai.cpp`): for a USB raw title with `nn_hai_user.rpl` and
  `fw.img`, `USBStorage::BuildHaiCompanion` turns the FAT32 chains of
  `content/hif_%06d.nfs` into Nintendo's companion (`count`, `0x00090009`, then
  512-byte start/count pairs in file order). In the game process the loader
  resolves `nn_hai_user` / `CreateCompanionFile__Q3_2nn3hai4userFPvUi` and
  patches it by address; the replacement passes that table to
  `MCP_CompatHaiCreateCompanionFile` (ioctl 14, written to
  `/vol/compat_slc/shared2/sys/hai/imginf.bin`). FunctionPatcher's by-name
  executable patches are unusable here: they `OSFatal` without MCP title info.
- IOS (`payload/source/ios_mcp/source/raw_hai.c`): MCP's HAI prepare handler
  (0x050081BE) gets the caller's `MCPTitleListType` at 0x0500835A. A one-shot
  arm (`UFLOW_HAI_ARM_COMMAND`, cleared with the raw-title profile) sets its
  path to the staged SD directory, `indexedDevice` to `usb` and the index byte to
  the RawUsbMap device. Nintendo's own code then reads `fw.tmd`, copies
  `fw.img`/`htk.bin`/`rvlt.tik`/`rvlt.tmd` to `/vol/system_ram/es`, reads the
  USB devId from LBA 0 of `/dev/usb%02x`, and emits
  `usb://shared2/sys/hai/imginf.bin`.
- Only those five small code files are copied unchanged, hash-verified, to
  `sd:/wiiu/apps/uFlow/hai/code/`; IOS cannot read FAT32 USB paths. The disc
  images stay on USB.
- Hardware, 2026-09-21: DKCR and Xenoblade boot from FAT32 USB through this
  bridge. The GamePad then showed "Wii software cannot be controlled with the
  Wii U GamePad": the GamePad controller mode is not a HAI param. The Wii U Menu
  asks "Select a display option" and stores it with `CMPTAcctSetScreenType` /
  `CMPTAcctSetDrcCtrlEnabled` (see Lynx64/WiiVCLaunch); nn_cmpt writes that into
  the vWii's conf.bin/settings.sav. uFlow bypassed the Menu, so it now makes the
  Menu's "Use GamePad as controller" calls when `meta.xml` `drc_use` is 65537 or
  a GamePad remap is applied, otherwise "TV and GamePad" (awaiting hardware test).
- Stock HAI presents the GamePad as a Wii Remote with a Classic Controller. For
  games without Classic Controller support (DKCR), `app/source/hai_gamepad.hpp`
  applies nfs2iso2nfs's `-wiimote`/`-horizontal` edits to the staged fw.img only;
  r569 needed its own verified offsets. `vwii_gamepad.cfg` overrides per title.
- DI2SD (title `fw.img`, segment VA 0x20200000) accepts the companion when both
  exponents are at least the device's physical block exponent (9 for this
  drive). Its LBA handling above INT32_MAX (this title starts at 2,258,768,864)
  is untested on hardware.

## Reproduced failure

Donkey Kong Country Returns, title `000500001019cf00`, was selected from
`usb1:/Decrypted`. The direct launch reached `frisbiiU` and `nn_hai_user.rpl`.
The capture `artifacts/20260921-wii-vc-direct-live.log` recorded a block-address
query for `/vol/content/hif_000000.nfs` returning -6, then
`Hai image files are not found` and fatal 160-0791. Normal file reads do not
implement the required HAI handoff. Restoring conversion to WBFS avoided that
path but discarded the wrapper, so it was not a solution.

## Evidence for a possible USB route

Primary reverse-engineering references:

- https://wiiubrew.org/wiki/HAI-IOS
- https://github.com/StroopwafelCFW/stroopwafel/blob/main/wafel_core/source/rednand/hai.c
- https://github.com/StroopwafelCFW/wafel_usb_partition/blob/main/source/main.c

HAI-IOS accepts a physical extent table and USB device identity. The companion
table describes byte ranges using an address-unit exponent. The partition plugin
adjusts this table and substitutes a USB identity taken from the disk's first
16 bytes. This is evidence of a mechanism, not proof that our FAT32 path works.
Our USB code already computes FAT file extents for raw executable loading.

## Original integration checklist (historical, before the bridge above)

Inspect this game's `code/nn_hai_user.rpl`, `code/frisbiiU.rpx`, `code/fw.img`,
`code/app.xml`, `code/cos.xml`, and `meta/meta.xml` to establish the exact call
ABI and firmware version. The user connected the drive to the PC; see the
read-only audit below. This checklist predates implementation of the direct backend.

Required before enabling a candidate: map every NFS file in order (including
fragmented FAT chains), validate sizes and disk identity, provide the expected
block query and companion format, select the correct USB device, and ensure
the original firmware and metadata reach the HAI transition. Bounds, partial
files and mismatched devices must fail before handoff. Preserve GamePad state.
Do not install a partitioning plugin or change disk partitions as a substitute.
Do not patch firmware at guessed offsets or return fabricated block addresses.

After implementing the bridge, test the original title on hardware, including
gameplay, GamePad input and return. Package verified dependencies in the native
installer. Keep that installer local as requested.

## Read-only audit, 2026-09-21

The drive is E:, FAT32, 512-byte sectors, 64 sectors/cluster, partition start
LBA 2048. `tools/inspect-fat32-hai.py` walked its FAT chains and compared both
ends of each NFS file through the Windows file API against raw volume reads.
All fourteen numbered files are present, total 3,574,595,584 bytes. The first
thirteen are 262,144,000 bytes each; the last is 166,723,584. All fourteen are
physically adjacent: disk LBA 2,258,768,864, length 6,981,632 sectors. These LBAs
exceed INT32_MAX. This is not a complete content-integrity check. The audit map
is diagnostic only: never persist/hardcode it for launch, as file moves change it.
No original USB files or sectors were written.

The title contains `frisbiiU.rpx` (61,312 bytes), `nn_hai_user.rpl` (7,616),
`fw.img` (1,789,485), `fw.tmd`, `rvlt.tmd`, `rvlt.tik`, and `htk.bin`.
app_type is 8000002E, SDK 21204, drc_use=1. That metadata alone does not prove
that this particular game implements GamePad gameplay input.

The exact RPL exports/imports and disassembly establish:

- `CreateCompanionFile__Q3_2nn3hai4userFPvUi` is at text+0x3F4. The returned
  five-word error structure is passed via hidden r3; buffer/size are r4/r5.
- Calls `FSGetFileBlockAddressAsync` with client, block, path, output addresses,
  address capacity, address-unit output, error mask, async data. Capacity is
  16,384 addresses **total**, not per NFS. A sector-per-entry substitution would
  overflow that limit; even one entry per FAT cluster exceeds it for this title.
- It divides addresses by the returned unit, merges adjacent runs, then builds
  BE32 count, BE16 log2(unit*physicalSectorSize), repeated BE16 exponent, and
  BE32 start/count pairs. It passes exactly `8 + 8*count` bytes to
  `MCP_CompatHaiCreateCompanionFile(handle, buffer, bytes)` (coreinit IOCTL 14).
- `hai_companion.hpp` implements the physical-range serialization separately,
  with bounds/overlap/coverage checks. Host sanitizer tests cover this actual
  title's geometry, fragmented/out-of-order allocation and unsigned LBAs.
  This serializer was subsequently connected to the bridge described above.

Read-only copies of coreinit, nn_hai, nn_cmpt and system fw.img were obtained
from the console's `/storage_slc/sys/title/00050010/1000400a/code` for analysis
under ignored `artifacts/hai-inspect`. No system binaries were replaced.
`tools/inspect-rpl.py` expands RPL sections for objdump; `tools/inspect-ios.py`
extracts bounded file-backed virtual ranges for inspection, not installation.

The console's IOS matches these upstream sites (original instruction bytes):

- 050078AE: F051 FC9B, BL 050591E8, companion FSA write.
- 051001D6: F758 FF1B, BL 05059010, companion target-path formatter.
- 0500900A: F7FE FCED, BL 050079E8, add USB identity parameter.

Importantly, USB-identity addition is conditional at 05008FE8 on prepared
context+0x6C; patching only that call would not work for an MLC-context launch.
The path formatter also selects its target from prepared context. The backend
must prepare a consistent USB context, not merely patch an address query.
The title HAI firmware identifies DI2SD as Hai 0.9-svn-r569, with explicit
companion-address-unit and USB identity checks. Firmware/ticket/TMD access in
the subsequent MCP compatibility preparation still needs tracing; ordinary
PPC content redirection cannot be assumed to cover those IOS-side reads.

## 2026-09-22: r590 firmware substitution hard-freezes DKCR (do not retry)

- Rhythm Heaven Fever (SOME01) ships HAI firmware r590, which has every nfs2iso2nfs
  anchor, including `-nocc` (`78 93 21 10 2B 02 D1 B7`, bne -> b). DKCR and Xenoblade ship r569,
  which has no `-nocc` site.
- Test: DKCR staged with Rhythm Heaven Fever's stock r590 fw.img/fw.tmd, plus `-wiimote`,
  `-horizontal`, `-nocc` and the HCVA return patch. The module trace confirmed the swap.
  Result on hardware: no GamePad input, then the console hard-froze. Even holding POWER did
  nothing, so the power plug had to be pulled.
- The substitution code was removed. Each title uses only its own fw.img. The r590 `-nocc`
  edit stays for titles that natively ship r590.
- Injection (WiiFlow/WBFS) for Wii VC titles is broken too. The NFS payload stores the Wii
  partition decrypted (hashes present), but `wii_vc_image.hpp` clears disc header byte
  0x61 without re-encrypting. WiiFlow then decrypts plaintext, reads a garbage FST size,
  hits `malloc fst` -> NULL -> DSI. Fixing it means re-encrypting each 0x8000 cluster
  with the partition title key.
